/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "ScanManager.hpp"
#include "ScanHead.hpp"

#include "FlatbufferMessages.hpp"
#include "RawProfileToProfile.hpp"
#include "StatusMessage.hpp"
#include "UDPBroadcastSocket.hpp"
#include "error_extended_macros.h"

#include <execution>
#include <cmath>
#include <cstring>
#include <ctime>
#include <memory>
#include <sstream>
#include <stdexcept>

using namespace joescan;

uint32_t ScanManager::m_uid_count = 0;

ScanManager::ScanManager(jsUnits units, ScanSyncManager *scansync) :
  m_scansync(scansync),
  m_state(SystemState::Disconnected),
  m_units(units),
  m_uid(0),
  m_min_scan_period_us(0),
  m_scan_period_us(0),
  m_idle_scan_period_us(0),
  m_frame_current_sequence(0),
  m_is_frame_scanning(false),
  m_is_frame_ready(false),
  m_is_user_encoder_map(false),
  m_is_encoder_dirty(true),
  m_is_idle_scan_enabled(false)
{
  m_uid = ++m_uid_count;

  Discover();

  // Initialize ScanSync encoder mapping to invalid
  for (uint32_t n = 0; n < JS_ENCODER_MAX; n++) {
    m_encoder_to_serial[(jsEncoder) n] = JS_SCANSYNC_INVALID_SERIAL;
  }
}

ScanManager::~ScanManager()
{
  {
    std::unique_lock<std::mutex> lk(m_mutex);
    m_state = SystemState::Close;
  }
  m_condition.notify_all();

  if (m_keep_alive_thread.joinable()) {
    m_keep_alive_thread.join();
  }
  if (m_heart_beat_thread.joinable()) {
    m_heart_beat_thread.join();
  }
  RemoveAllScanHeads();
}

uint32_t ScanManager::GetUID() const
{
  return m_uid;
}

int32_t ScanManager::Discover()
{
  CLEAR_ERROR();

  if (IsConnected()) {
    RETURN_ERROR("Request not allowed while connected", JS_ERROR_CONNECTED);
  }

  using namespace schema::client;
  static constexpr uint16_t kBroadcastDiscoverPort = 12347;
  auto ifaces = NetworkInterface::GetClientInterfaces();
  std::vector<std::unique_ptr<UDPBroadcastSocket>> sockets;

  /////////////////////////////////////////////////////////////////////////////
  // STEP 1: Get all available interfaces.
  /////////////////////////////////////////////////////////////////////////////
  {
    for (auto const &iface : ifaces) {
      try {
        std::unique_ptr<UDPBroadcastSocket> sock(
          new UDPBroadcastSocket(iface.ip_addr));
        sockets.push_back(std::move(sock));
      } catch (const std::runtime_error &) {
        // Failed to init socket, continue with other sockets
      }
    }

    if (sockets.size() == 0) {
      RETURN_ERROR("No network interfaces found", JS_ERROR_NETWORK);
    }
  }

  /////////////////////////////////////////////////////////////////////////////
  // STEP 2: UDP broadcast ClientDiscovery message to all scan heads.
  /////////////////////////////////////////////////////////////////////////////
  {
    using namespace schema::client;
    flatbuffers::FlatBufferBuilder builder(64);

    builder.Clear();
    uint32_t maj = API_VERSION_MAJOR;
    uint32_t min = API_VERSION_MINOR;
    uint32_t pch = API_VERSION_PATCH;
    auto msg_offset = CreateMessageClientDiscovery(builder, maj, min, pch);
    builder.Finish(msg_offset);

    // spam each network interface with our discover message
    int sendto_count = 0;
    for (auto const &socket : sockets) {
      int r = socket->Send(kBroadcastDiscoverPort, builder);
      if (0 == r) {
        // sendto succeeded in sending message through interface
        sendto_count++;
      }
    }

    if (0 >= sendto_count) {
      // no interfaces were able to send UDP broadcast
      RETURN_ERROR("UDP network error", JS_ERROR_NETWORK);
    }
  }

  // TODO: revist timeout? make it user controlled?
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  m_serial_to_discovered.clear();

  /////////////////////////////////////////////////////////////////////////////
  // STEP 3: See which (if any) scan heads responded.
  /////////////////////////////////////////////////////////////////////////////
  {
    using namespace schema::server;
    const uint32_t buf_len = 128;
    uint8_t *buf = new uint8_t[buf_len];
    uint32_t n = 0;

    for (auto const &socket : sockets) {
      // Get interface associated with socket; used for `jsDiscovered` struct
      NetworkInterface::Client iface = ifaces[n++];

      do {
        int r = socket->Read(buf, buf_len);
        if (0 >= r) {
          break;
        }

        auto verifier = flatbuffers::Verifier(buf, (uint32_t) r);
        if (!VerifyMessageServerDiscoveryBuffer(verifier)) {
          // not a flatbuffer message
          continue;
        }

        auto msg = UnPackMessageServerDiscovery(buf);
        if (nullptr == msg) {
          continue;
        }

        auto result = std::make_shared<jsDiscovered>();
        result->serial_number = msg->serial_number;
        result->type = (jsScanHeadType) msg->type;
        result->firmware_version_major = msg->version_major;
        result->firmware_version_minor = msg->version_minor;
        result->firmware_version_patch = msg->version_patch;
        result->ip_addr = msg->ip_server;
        result->client_ip_addr = iface.ip_addr;
        result->client_netmask = iface.net_mask;
        result->link_speed_mbps = msg->link_speed_mbps;
        result->state = (jsScanHeadState) msg->state;

        size_t len = 0;

        len = iface.name.copy(
          result->client_name_str,
          JS_CLIENT_NAME_STR_MAX_LEN - 1);
        result->client_name_str[len] = 0;

        len = msg->type_str.copy(
          result->type_str,
          JS_SCAN_HEAD_TYPE_STR_MAX_LEN - 1);
        result->type_str[len] = 0;

        m_serial_to_discovered[msg->serial_number] = result;
      } while (1);
    }

    delete[] buf;
  }

  return (int32_t) (m_serial_to_discovered.size());
}

int32_t ScanManager::ScanHeadsDiscovered(jsDiscovered *results,
                                         uint32_t max_results)
{
  CLEAR_ERROR();

  jsDiscovered *dst = results;

  std::map<uint32_t, std::shared_ptr<jsDiscovered>>::iterator it =
    m_serial_to_discovered.begin();

  uint32_t results_len = (uint32_t)
    (m_serial_to_discovered.size() < max_results ?
     m_serial_to_discovered.size() : max_results);

  for (uint32_t n = 0; n < results_len; n++) {
    memcpy(dst, it->second.get(), sizeof(jsDiscovered));
    dst++;
    it++;
  }

  return (int32_t) m_serial_to_discovered.size();
}

int32_t ScanManager::DiscoverScanSyncs(jsScanSyncDiscovered *discovered,
                                       uint32_t max_results)
{
  int r = 0;

  if (!IsConnected()) {
    RETURN_ERROR("Request not allowed while disconnected",
                 JS_ERROR_NOT_CONNECTED);
  }

  auto common_scansyncs = m_scansync->GetDiscovered();

  for (auto &p : m_id_to_scan_head) {
    auto sh = p.second;
    std::vector<jsScanSyncDiscovered> scanner_syncs(JS_ENCODER_MAX);

    r = sh->SendScanSyncStatusRequest(&scanner_syncs[0], JS_ENCODER_MAX);
    if (JS_ERROR_VERSION_COMPATIBILITY == r) {
      continue;
    } else if (0 >= r) {
      return r;
    }

    // If we observe that a API discovered ScanSync isn't visible from a particular
    // scanner, remove the ScanSync from the common list which initially was seen by
    // the API
    common_scansyncs.erase(
      std::remove_if(common_scansyncs.begin(), common_scansyncs.end(),
        [&scanner_syncs](const jsScanSyncDiscovered& api_sync) {
          return std::none_of(scanner_syncs.begin(), scanner_syncs.end(),
            [&api_sync](const jsScanSyncDiscovered& scanner_sync) {
                return api_sync.serial_number == scanner_sync.serial_number;
            });
        }),
      common_scansyncs.end()
    );
  }

  uint32_t results_len = (uint32_t)
    (common_scansyncs.size() < max_results ?
    common_scansyncs.size() : max_results);

  for (uint32_t i = 0; i < results_len; i++) {
    memcpy(discovered + i, &common_scansyncs[i], sizeof(jsScanSyncDiscovered));
  }

  return results_len;
}

int32_t ScanManager::SetScanSyncEncoder(uint32_t serial_main,
                                        uint32_t serial_aux1,
                                        uint32_t serial_aux2)
{
  CLEAR_ERROR();
  int r = 0;
  // Note: It is expected that the user will call this after adding all the
  // scan heads.
  if (!m_version_scan_head_lowest.IsCompatible(16, 3, 0)) {
    RETURN_ERROR("Requires firmware version v16.3.0",
                 JS_ERROR_VERSION_COMPATIBILITY);
  }

  // Make sure the serial numbers are valid and that the user isn't trying to
  // set Main and Aux2 but not Aux1.
  if (JS_SCANSYNC_INVALID_SERIAL == serial_main) {
    RETURN_ERROR("Invalid serial number for main encoder",
                 JS_ERROR_INVALID_ARGUMENT);
  } else if ((JS_SCANSYNC_INVALID_SERIAL == serial_aux1) &&
             (JS_SCANSYNC_INVALID_SERIAL != serial_aux2)) {
    RETURN_ERROR("Invalid serial number for aux1 encoder",
                 JS_ERROR_INVALID_ARGUMENT);
  }

  // Prevent the same ScanSync from being used in multiple assignments.
  if ((serial_main == serial_aux1) || (serial_main == serial_aux2)) {
    RETURN_ERROR("Duplicate encoder assignment for serial " +
                 std::to_string(serial_main),
                 JS_ERROR_INVALID_ARGUMENT);
  } else if ((JS_SCANSYNC_INVALID_SERIAL != serial_aux1) &&
             (serial_aux1 == serial_aux2)) {
    RETURN_ERROR("Duplicate encoder assignment for serial " +
                 std::to_string(serial_aux1),
                 JS_ERROR_INVALID_ARGUMENT);
  }

  std::vector<jsScanSyncDiscovered> discovered(JS_ENCODER_MAX);

  r = DiscoverScanSyncs(&discovered[0], JS_ENCODER_MAX);
  if (0 >= r) {
    return JS_ERROR_NOT_DISCOVERED;
  }

  // Push serials to vector to allow easy use of `std::find` function.
  std::vector<uint32_t> s;
  for (auto &d : discovered) {
    if (d.serial_number != 0) {
      s.push_back(d.serial_number);
    }
  }

  if (s.end() == std::find(s.begin(), s.end(), serial_main)) {
    RETURN_ERROR("ScanSync " + std::to_string(serial_main) + " not discovered",
                 JS_ERROR_NOT_DISCOVERED);
  }
  if (JS_SCANSYNC_INVALID_SERIAL != serial_aux1) {
    if (s.end() == std::find(s.begin(), s.end(), serial_aux1)) {
    RETURN_ERROR("ScanSync " + std::to_string(serial_aux1) + " not discovered",
                 JS_ERROR_NOT_DISCOVERED);
    }
  }
  if (JS_SCANSYNC_INVALID_SERIAL != serial_aux2) {
    if (s.end() == std::find(s.begin(), s.end(), serial_aux2)) {
    RETURN_ERROR("ScanSync " + std::to_string(serial_aux2) + " not discovered",
                 JS_ERROR_NOT_DISCOVERED);
    }
  }

  m_encoder_to_serial[JS_ENCODER_MAIN] = serial_main;
  m_encoder_to_serial[JS_ENCODER_AUX_1] = serial_aux1;
  m_encoder_to_serial[JS_ENCODER_AUX_2] = serial_aux2;
  m_is_user_encoder_map = true;
  m_is_encoder_dirty = true;

  return 0;
}

int32_t ScanManager::GetScanSyncEncoder(uint32_t *serial_main,
                                        uint32_t *serial_aux1,
                                        uint32_t *serial_aux2)
{
  CLEAR_ERROR();

  if (0 == m_encoder_to_serial.count(JS_ENCODER_MAIN)) {
    *serial_main = 0;
  } else {
    *serial_main = m_encoder_to_serial[JS_ENCODER_MAIN];
  }

  if (0 == m_encoder_to_serial.count(JS_ENCODER_AUX_1)) {
    *serial_aux1 = 0;
  } else {
    *serial_aux1 = m_encoder_to_serial[JS_ENCODER_AUX_1];
  }

  if (0 == m_encoder_to_serial.count(JS_ENCODER_AUX_2)) {
    *serial_aux2 = 0;
  } else {
    *serial_aux2 = m_encoder_to_serial[JS_ENCODER_AUX_2];
  }

  return 0;
}

int32_t ScanManager::SetDefaultScanSyncEncoder()
{
  CLEAR_ERROR();

  if (!IsConnected()) {
    RETURN_ERROR("Request not allowed while disconnected",
                 JS_ERROR_NOT_CONNECTED);
  }

  m_is_user_encoder_map = false;
  m_is_encoder_dirty = true;
  for (uint32_t n = 0; n < JS_ENCODER_MAX; n++) {
    m_encoder_to_serial[(jsEncoder) n] = JS_SCANSYNC_INVALID_SERIAL;
  }

  std::vector<jsScanSyncDiscovered> d(JS_ENCODER_MAX);
  int r = DiscoverScanSyncs(&d[0], JS_ENCODER_MAX);
  uint32_t sync_count = 0 > r ? 0 : r;

  for (uint32_t n = 0; n <= JS_ENCODER_MAX; n++) {
    jsEncoder e = (jsEncoder) n;
    if (n < sync_count) {
      m_encoder_to_serial[e] = d[n].serial_number;
    } else {
      m_encoder_to_serial[e] = JS_SCANSYNC_INVALID_SERIAL;
    }
  }

  return 0;
}

int32_t ScanManager::CreateScanHead(uint32_t serial_number, uint32_t id)
{
  CLEAR_ERROR();

  ScanHead *sh = nullptr;

  if (IsScanning()) {
    RETURN_ERROR("Can not create scan head while scanning", JS_ERROR_SCANNING);
  }

  if (INT_MAX < id) {
    RETURN_ERROR("Invalid scan head id", JS_ERROR_INVALID_ARGUMENT);
  }

  if (m_serial_to_scan_head.find(serial_number) !=
      m_serial_to_scan_head.end()) {
    RETURN_ERROR("Scan head already exists", JS_ERROR_ALREADY_EXISTS);
  }

  if (m_id_to_scan_head.find(id) != m_id_to_scan_head.end()) {
    RETURN_ERROR("Scan head id already in use", JS_ERROR_ALREADY_EXISTS);
  }

  if (m_serial_to_discovered.find(serial_number) ==
      m_serial_to_discovered.end()) {
    // try again
    Discover();
    if (m_serial_to_discovered.find(serial_number) ==
      m_serial_to_discovered.end()) {
      RETURN_ERROR("Scan head not discovered on network",
                   JS_ERROR_NOT_DISCOVERED);
    }
  }

  auto discovered = m_serial_to_discovered[serial_number];
  if (API_VERSION_MAJOR != discovered->firmware_version_major) {
    std::string fwver = std::to_string(discovered->firmware_version_major) +
                        std::to_string(discovered->firmware_version_minor) +
                        std::to_string(discovered->firmware_version_patch);
    RETURN_ERROR("API not compatible with firmware v" + fwver,
                 JS_ERROR_VERSION_COMPATIBILITY);
  }

  SemanticVersion version_scan_head(discovered->firmware_version_major,
                                    discovered->firmware_version_minor,
                                    discovered->firmware_version_patch);
  if (0 == m_serial_to_scan_head.size()) {
    m_version_scan_head_highest = version_scan_head;
    m_version_scan_head_lowest = version_scan_head;
  } else {
    if (m_version_scan_head_highest.IsLessThan(version_scan_head)) {
      m_version_scan_head_highest = version_scan_head;
    } else if (m_version_scan_head_lowest.IsGreaterThan(version_scan_head)) {
      m_version_scan_head_lowest = version_scan_head;
    }
  }

  sh = new ScanHead(*this, *discovered, id);
  m_serial_to_scan_head[discovered->serial_number] = sh;
  m_id_to_scan_head[id] = sh;

  return 0;
}

ScanHead *ScanManager::GetScanHeadBySerial(uint32_t serial_number)
{
  CLEAR_ERROR();

  auto res = m_serial_to_scan_head.find(serial_number);
  if (res == m_serial_to_scan_head.end()) {
    RETURN_ERROR_NULLPTR("Scan head serial " + std::to_string(serial_number) +
                         " not managed");
  }

  return res->second;
}

ScanHead *ScanManager::GetScanHeadById(uint32_t id)
{
  CLEAR_ERROR();

  auto res = m_id_to_scan_head.find(id);
  if (res == m_id_to_scan_head.end()) {
    RETURN_ERROR_NULLPTR("Scan head id " + std::to_string(id) + " not managed");
  }

  return res->second;
}

int32_t ScanManager::RemoveScanHead(uint32_t serial_number)
{
  CLEAR_ERROR();

  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  }

  auto res = m_serial_to_scan_head.find(serial_number);
  if (res == m_serial_to_scan_head.end()) {
    RETURN_ERROR("Scan head serial " + std::to_string(serial_number) +
                 " not managed",
                 JS_ERROR_INVALID_ARGUMENT);
  }

  uint32_t id = res->second->GetId();
  delete res->second;

  m_serial_to_scan_head.erase(serial_number);
  m_id_to_scan_head.erase(id);

  return 0;
}

int32_t ScanManager::RemoveScanHead(ScanHead *scan_head)
{
  CLEAR_ERROR();

  if (scan_head == nullptr) {
    RETURN_ERROR("Null scan head argument", JS_ERROR_NULL_ARGUMENT);
  }

  RemoveScanHead(scan_head->GetSerialNumber());

  return 0;
}

int32_t ScanManager::RemoveAllScanHeads()
{
  CLEAR_ERROR();

  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  }

  // We copy the serials to a new vector because the function `RemoveScanHead`
  // will modify class vectors used to hold scan heads; making it difficult
  // to iterate through vector normally.
  std::vector<uint32_t> serials;
  for (auto &res : m_serial_to_scan_head) {
    serials.push_back(res.first);
  }

  for (auto &serial : serials) {
    RemoveScanHead(serial);
  }

  return 0;
}

uint32_t ScanManager::GetNumberScanners() const
{
  return static_cast<uint32_t>(m_serial_to_scan_head.size());
}

int32_t ScanManager::PhaseClearAll()
{
  CLEAR_ERROR();

  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  }

  m_phase_table.Reset();

  return 0;
}

int32_t ScanManager::PhaseCreate()
{
  CLEAR_ERROR();

  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  }

  m_phase_table.CreatePhase();

  return 0;
}

int32_t ScanManager::PhaseInsert(ScanHead *scan_head, jsCamera camera)
{
  CLEAR_ERROR();

  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  }

  int r = m_phase_table.AddToLastPhaseEntry(scan_head, camera);
  if (0 > r) {
    RETURN_ERROR(m_phase_table.GetErrorExtended(), r);
  }

  return 0;
}

int32_t ScanManager::PhaseInsert(ScanHead *scan_head, jsLaser laser)
{
  CLEAR_ERROR();

  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  }

  int r = m_phase_table.AddToLastPhaseEntry(scan_head, laser);
  if (0 > r) {
    RETURN_ERROR(m_phase_table.GetErrorExtended(), r);
  }

  return 0;
}

int32_t ScanManager::PhaseInsert(ScanHead *scan_head, jsCamera camera,
                    jsScanHeadConfiguration *config)
{
  CLEAR_ERROR();

  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  }

  int r = m_phase_table.AddToLastPhaseEntry(scan_head, camera, config);
  if (0 > r) {
    RETURN_ERROR(m_phase_table.GetErrorExtended(), r);
  }

  return 0;
}

int32_t ScanManager::PhaseInsert(ScanHead *scan_head, jsLaser laser,
                    jsScanHeadConfiguration *config)
{
  CLEAR_ERROR();

  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  }

  int r = m_phase_table.AddToLastPhaseEntry(scan_head, laser, config);
  if (0 > r) {
    RETURN_ERROR(m_phase_table.GetErrorExtended(), r);
  }

  return 0;
}

int32_t ScanManager::SetIdleScanPeriod(uint32_t period_us)
{
  CLEAR_ERROR();

  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  }

  m_idle_scan_period_us = period_us;
  m_is_idle_scan_enabled = true;

  return 0;
}

int32_t ScanManager::DisableIdleScanning()
{
  CLEAR_ERROR();

  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  }

  m_idle_scan_period_us = 0;
  m_is_idle_scan_enabled = false;

  return 0;
}

uint32_t ScanManager::GetIdleScanPeriod()
{
  return m_idle_scan_period_us;
}

bool ScanManager::IsIdleScanningEnabled()
{
  return m_is_idle_scan_enabled;
}

int32_t ScanManager::Connect(uint32_t timeout_s)
{
  CLEAR_ERROR();

  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  }

  if (IsConnected()) {
    RETURN_ERROR("Already connected to scan heads", JS_ERROR_CONNECTED);
  }

  if (m_serial_to_scan_head.empty()) {
    RETURN_ERROR("No scan heads in scan system" , JS_ERROR_NOT_CONNECTED);
  }

  std::map<uint32_t, ScanHead *> connected;
  std::mutex mut;

  std::for_each(std::execution::par,
    m_serial_to_scan_head.begin(), m_serial_to_scan_head.end(),
    [&](auto& pair) {
      uint32_t serial = pair.first;
      ScanHead* scan_head = pair.second;
      if (0 == scan_head->Connect(timeout_s)) {
        std::scoped_lock lk(mut);
        connected[serial] = scan_head;
      }
    });

  if (connected.size() == m_serial_to_scan_head.size()) {
    m_state = SystemState::Connected;
    // JS-50 server clears ScanSync mapping on new connection
    m_is_encoder_dirty = true;

    int r = Configure();
    if (0 != r) {
      return r; // rely on previous function to set extended error
    }
  }

  std::thread keep_alive_thread(&ScanManager::KeepAliveThread, this);
  m_keep_alive_thread = std::move(keep_alive_thread);

  std::thread heart_beat_thread(&ScanManager::HeartBeatThread, this);
  m_heart_beat_thread = std::move(heart_beat_thread);
  return int32_t(connected.size());
}

int32_t ScanManager::Disconnect()
{
  CLEAR_ERROR();

  if (!IsConnected()) {
    RETURN_ERROR("Already disconnected", JS_ERROR_NOT_CONNECTED);
  }

  if (IsScanning()) {
    StopScanning();
  }

  std::for_each(std::execution::par,
    m_serial_to_scan_head.begin(), m_serial_to_scan_head.end(),
    [&](auto& pair) {
      ScanHead* scan_head = pair.second;
      scan_head->Disconnect();
    });

  m_state = SystemState::Disconnected;
  m_is_encoder_dirty = true;
  return 0;
}

int ScanManager::StartScanning(uint32_t period_us, jsDataFormat fmt,
                               bool is_frame_scanning)
{
  CLEAR_ERROR();

  int r = 0;

  if (!IsConnected()) {
    RETURN_ERROR("Request not allowed while disconnected",
                 JS_ERROR_NOT_CONNECTED);
  }

  if (IsScanning()) {
    RETURN_ERROR("Already scanning", JS_ERROR_SCANNING);
  }

  if (0 == m_phase_table.GetNumberOfPhases()) {
    RETURN_ERROR("Phase table empty", JS_ERROR_PHASE_TABLE_EMPTY);
  }

  if (m_phase_table.HasDuplicateElements() && is_frame_scanning) {
    RETURN_ERROR("Phase table with duplicate elements not compatible with "
                 "frame scanning",
                 JS_ERROR_FRAME_SCANNING_INVALID_PHASE_TABLE);
  }

  if (m_is_idle_scan_enabled && m_idle_scan_period_us <= period_us && 
      m_idle_scan_period_us != 0) {
    RETURN_ERROR("Idle scan period must be greater than the scan period",
                JS_ERROR_INVALID_ARGUMENT);
  }

  r = Configure();
  if (0 != r) {
    return r; // rely on previous function to set extended error
  }

  if (m_min_scan_period_us > period_us) {
    RETURN_ERROR("Requested scan period " + std::to_string(period_us) +
                 "us is less than minimum " +
                 std::to_string(m_min_scan_period_us) + "us",
                 JS_ERROR_INVALID_ARGUMENT);
  }

  /**
   * TODO: At some point it might be interesting to move the Scan Configuration
   * Message to the `Configure` function so that we only have to send the Start
   * Scanning Message. We currently can't do that now because the API is set up
   * such that the scan period & data format are supplied with the call to
   * start scanning; they are unknown before then.
   */
  for (auto const &pair : m_serial_to_scan_head) {
    ScanHead *sh = pair.second;
    r = sh->SendScanConfiguration(period_us, fmt, is_frame_scanning);
    if (0 != r) {
      RETURN_ERROR(sh->GetErrorExtended(), r);
    }
  }

  // NOTE: start time of `0` will cause the scan server to calculate its own
  // start time from its system clock.
  uint64_t start_time_ns = 0;
  if (m_encoder_to_serial.count(JS_ENCODER_MAIN)) {
    jsScanSyncStatus scansync_status;
    if (0 == m_scansync->GetStatus(m_encoder_to_serial[JS_ENCODER_MAIN],
                                   &scansync_status)) {
      // 20ms offset seems to work; less than that causes skipped sequences.
      const uint64_t kStartTimeOffsetNs = 20000000;
      start_time_ns = scansync_status.timestamp_ns + kStartTimeOffsetNs;
    }
  }

  if (is_frame_scanning) {
    auto const &scan_head_to_pairs = m_phase_table.GetScheduledPairsPerScanHead();
    for (auto const &pair : scan_head_to_pairs) {
      ScanHead *sh = pair.first;
      sh->GetProfileQueue()->SetValidPairs(pair.second);
    }
  }

  for (auto const &pair : m_serial_to_scan_head) {
    ScanHead *sh = pair.second;

    r = sh->StartScanning(start_time_ns, is_frame_scanning);
    if (0 != r) {
      RETURN_ERROR(sh->GetErrorExtended(), r);
    }
  }

  {
    std::unique_lock<std::mutex> lk(m_mutex);
    m_state = SystemState::Scanning;
  }
  m_is_frame_scanning = is_frame_scanning;
  m_is_frame_ready = false;
  m_frame_current_sequence = 1;
  m_scan_period_us = period_us;
  m_condition.notify_all();

  return 0;
}

uint32_t ScanManager::GetProfilesPerFrame() const
{
  uint32_t n = 0;

  for (auto& p : m_id_to_scan_head) {
    auto sh = p.second;
    n += sh->GetCameraLaserPairCount();
  }

  return n;
}

int32_t ScanManager::WaitUntilFrameAvailable(uint32_t timeout_us)
{
  CLEAR_ERROR();

  if (!IsScanning()) {
    RETURN_ERROR("Request only allowed while scanning",
                 JS_ERROR_NOT_CONNECTED);
  } else if (!m_is_frame_scanning) {
    RETURN_ERROR("Request only allowed during frame scanning",
                 JS_ERROR_NOT_CONNECTED);
  }

  // Sleep period to poll for frame status
  const int32_t sleep_us = m_scan_period_us / 4;
  int32_t time_remaining_us = timeout_us;

  do {
    int64_t seq_min = -1;
    int64_t seq_max = -1;
    int64_t sz_min = -1;
    int64_t sz_max = -1;

    for (auto &p : m_id_to_scan_head) {
      auto sh = p.second;
      auto queue = sh->GetProfileQueue();

      auto report = queue->GetReport();
      if ((-1 == seq_min) || (seq_min > report.sequence_min)) {
        seq_min = report.sequence_min;
      }
      if ((-1 == seq_max) || (seq_max < report.sequence_max)) {
        seq_max = report.sequence_max;
      }
      if ((-1 == sz_min) || (sz_min > report.size_min)) {
        sz_min = report.size_min;
      }
      if ((-1 == sz_max) || (sz_max < report.size_max)) {
        sz_max = report.size_max;
      }
    }

    // If `seq_min` is greater than or equal, then all the queues should have a
    // profile for the next frame.
    // If `sz_max` is greater than or equal to the threshold, we want to build
    // a partial frame rather than fall further behind.
    if ((seq_min >= m_frame_current_sequence) ||
        (sz_max >= kFrameSizeThreshold)) {
      // Small optimization; helps to prevent need to do another report when
      // calling `GetFrame()` as it will need to repeat the above unless it
      // is informed that a frame is has already been found to be ready.
      m_is_frame_ready = true;
      return 1; // frame ready
    }

    if (time_remaining_us > 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
      time_remaining_us -= sleep_us;
    } else {
      break;
    }
  } while (1);

  return 0; // frame not ready
}

int32_t ScanManager::GetFrame(jsProfile *profiles)
{
  CLEAR_ERROR();

  if (!IsScanning()) {
    RETURN_ERROR("Request only allowed while scanning",
                 JS_ERROR_NOT_CONNECTED);
  } else if (!m_is_frame_scanning) {
    RETURN_ERROR("Request only allowed during frame scanning",
                 JS_ERROR_NOT_CONNECTED);
  }
  int32_t r = 0;

  if (!m_is_frame_ready) {
    // User didn't call `WaitUntilFrameAvailable()` before; need to check to
    // see if a frame is actually ready or not.
    r = WaitUntilFrameAvailable(0);
    if (0 >= r) {
      return r; // rely on previous function to set extended error
    }
  }

  jsProfile *dst = profiles;
  uint32_t count = 0;
  for (auto &p : m_id_to_scan_head) {
    auto sh = p.second;
    auto queue = sh->GetProfileQueue();
    auto iter = CameraLaserIterator(sh);

    if (JS_CABLE_ORIENTATION_DOWNSTREAM == sh->GetCableOrientation()) {
      iter.reverse();
    }

    for (auto &pair : iter) {
      const jsCamera camera = pair.first;
      const jsLaser laser = pair.second;
      jsRawProfile *raw = nullptr;
      jsProfile *prev = nullptr;

      do {
        prev = dst;

        uint32_t seq = 0;
        r = queue->ReadyPeekSequence(camera, laser, &seq);
        if ((0 != r) || (seq > m_frame_current_sequence)) {
          // Missing profile; mark as invalid, but fill in some basic info
          jsProfileInit(dst);
          dst->scan_head_id = sh->GetId();
          dst->camera = camera;
          dst->laser = laser;
          dst->sequence_number = m_frame_current_sequence;
          dst++;
        } else if (seq < m_frame_current_sequence) {
          r = queue->DequeueReady(camera, laser, &raw);
          assert(0 == r);
          r = queue->EnqueueFree(camera, laser, &raw);
          assert(0 == r);
          continue;
        } else {
          r = queue->DequeueReady(camera, laser, &raw);
          assert(0 == r);
          RawProfileToProfile(raw, dst);
          r = queue->EnqueueFree(camera, laser, &raw);
          assert(0 == r);
          dst++;
          count++;
        }
      } while (prev == dst);
    }
  }

  m_frame_current_sequence++;
  m_is_frame_ready = false;

  return count;
}

int32_t ScanManager::GetFrame(jsRawProfile *profiles)
{
  CLEAR_ERROR();

  if (!IsScanning()) {
    RETURN_ERROR("Request only allowed while scanning",
                 JS_ERROR_NOT_CONNECTED);
  } else if (!m_is_frame_scanning) {
    RETURN_ERROR("Request only allowed during frame scanning",
                 JS_ERROR_NOT_CONNECTED);
  }

  int32_t r = 0;

  if (!m_is_frame_ready) {
    // User didn't call `WaitUntilFrameAvailable()` before; need to check to
    // see if a frame is actually ready or not.
    r = WaitUntilFrameAvailable(0);
    if (0 >= r) {
      return r; // rely on previous function to set extended error
    }
  }

  jsRawProfile *dst = profiles;
  uint32_t count = 0;
  for (auto &p : m_id_to_scan_head) {
    auto sh = p.second;
    auto queue = sh->GetProfileQueue();
    auto iter = CameraLaserIterator(sh);

    if (JS_CABLE_ORIENTATION_DOWNSTREAM == sh->GetCableOrientation()) {
      iter.reverse();
    }

    for (auto &pair : iter) {
      const jsCamera camera = pair.first;
      const jsLaser laser = pair.second;
      jsRawProfile *raw = nullptr;
      jsRawProfile *prev = nullptr;

      do {
        prev = dst;

        uint32_t seq = 0;
        r = queue->ReadyPeekSequence(camera, laser, &seq);
        if ((0 != r) || (seq > m_frame_current_sequence)) {
          // Either no profile to be read or the profile ready to be read is
          // for a future frame; skip this one for now and mark the slot in
          // the array as invalid.
          jsRawProfileInit(dst);
          // Fill in some basic info about the missing profile
          dst->scan_head_id = sh->GetId();
          dst->camera = camera;
          dst->laser = laser;
          dst->sequence_number = m_frame_current_sequence;
          dst++;
        } else if (seq < m_frame_current_sequence) {
          // One of the profiles got held up long enough that it's frame was
          // already read out. Possible network issue? Recycle this profile;
          // don't return it to the user.
          r = queue->DequeueReady(camera, laser, &raw);
          assert(0 == r);
          r = queue->EnqueueFree(camera, laser, &raw);
          assert(0 == r);
        } else {
          r = queue->DequeueReady(camera, laser, &raw);
          assert(0 == r);
          *dst = *raw;
          r = queue->EnqueueFree(camera, laser, &raw);
          assert(0 == r);
          dst++;
          count++;
        }
      } while (prev == dst);
    }
  }

  m_frame_current_sequence++;
  m_is_frame_ready = false;

  return count;
}

int32_t ScanManager::ClearFrames()
{
  CLEAR_ERROR();

  if (!IsScanning()) {
    RETURN_ERROR("Request only allowed while scanning",
                 JS_ERROR_NOT_CONNECTED);
  } else if (!m_is_frame_scanning) {
    RETURN_ERROR("Request only allowed during frame scanning",
                 JS_ERROR_NOT_CONNECTED);
  }

  uint32_t seq_max = 0;

  // Find out what the greatest sequence number is; this should be the most
  // recent frame has been / is being received.
  for (auto &p : m_id_to_scan_head) {
    auto sh = p.second;
    uint32_t seq = sh->GetLastSequenceNumber();
    if (seq > seq_max) seq_max = seq;
  }

  // The next frame that we'll be looking to build will be one in the future.
  m_frame_current_sequence = seq_max + 1;

  // Now clear out all the profiles buffered in the queues.
  for (auto &p : m_id_to_scan_head) {
    auto sh = p.second;
    auto queue = sh->GetProfileQueue();
    queue->Reset(ProfileQueue::MODE_MULTI);
  }

  return 0;
}

int32_t ScanManager::StopScanning()
{
  CLEAR_ERROR();

  if (!IsConnected()) {
    RETURN_ERROR("Request not allowed while disconnected",
                 JS_ERROR_NOT_CONNECTED);
  }

  if (!IsScanning()) {
    RETURN_ERROR("Already stopped scanning", JS_ERROR_NOT_SCANNING);
  }

  for (auto const &pair : m_serial_to_scan_head) {
    ScanHead *sh = pair.second;
    sh->StopScanning();
  }

  {
    std::unique_lock<std::mutex> lk(m_mutex);
    m_state = SystemState::Connected;
  }

  m_condition.notify_all();

  return 0;
}

int32_t ScanManager::Configure()
{
  CLEAR_ERROR();

  // Only run configure code if the scan system is connected & not scanning.
  if (IsScanning()) {
    RETURN_ERROR("Request not allowed while scanning", JS_ERROR_SCANNING);
  } else if (!IsConnected()) {
    RETURN_ERROR("Request not allowed while disconnected",
                 JS_ERROR_NOT_CONNECTED);
  }

  bool is_config_dirty = !IsConfigured();
  bool is_phase_table_dirty = m_phase_table.IsDirty();

  if (JS_SCANSYNC_INVALID_SERIAL == m_encoder_to_serial[JS_ENCODER_MAIN]) {
    SetDefaultScanSyncEncoder();
  }

  if (m_is_encoder_dirty) {
    if (JS_SCANSYNC_INVALID_SERIAL != m_encoder_to_serial[JS_ENCODER_MAIN]) {
      for (auto const &pair : m_serial_to_scan_head) {
        ScanHead *sh = pair.second;
        int r = sh->SendEncoders(m_encoder_to_serial[JS_ENCODER_MAIN],
                             m_encoder_to_serial[JS_ENCODER_AUX_1],
                             m_encoder_to_serial[JS_ENCODER_AUX_2]);
        if (JS_ERROR_VERSION_COMPATIBILITY == r && !m_is_user_encoder_map) {
          // User did not set an encoder mapping and the scan head does
          // not support sending a mapping. The scan head will use the default
          // mapping instead.
          r = 0;
        } else if (0 > r) {
          RETURN_ERROR(sh->GetErrorExtended(), r);
        }
      }
      m_is_encoder_dirty = false;
    }
  }

  // Skip code below if we've already configured and nothing has changed.
  if (is_config_dirty) {
    std::mutex err_mut;
    int err_serial = 0;
    int err = 0;

    std::for_each(std::execution::par,
      m_serial_to_scan_head.begin(), m_serial_to_scan_head.end(),
      [&](auto& pair) {
        ScanHead* sh = pair.second;
        int r = 0;

        r = sh->SendWindow();
        if (0 != r) {
          std::scoped_lock<std::mutex> lk(err_mut);
          err = r;
          err_serial = sh->GetSerialNumber();
          return;
        }

        r = sh->SendBrightnessCorrection();
        if ((0 > r) && (JS_ERROR_VERSION_COMPATIBILITY != r)) {
          // Only error we allow is version compatibility. This will cause the
          // head to be skipped for sending data. If the user tried to set this
          // earlier, they should have already received an error; only case left
          // to consider is sending the "default" value if unset.
          std::scoped_lock<std::mutex> lk(err_mut);
          err = r;
          err_serial = sh->GetSerialNumber();
          return;
        }

        r = sh->SendExclusionMask();
        if ((0 > r) && (JS_ERROR_VERSION_COMPATIBILITY != r)) {
          // Only error we allow is version compatibility. This will cause the
          // head to be skipped for sending data. If the user tried to set this
          // earlier, they should have already received an error; only case left
          // to consider is sending the "default" value if unset.
          std::scoped_lock<std::mutex> lk(err_mut);
          err = r;
          err_serial = sh->GetSerialNumber();
          return;
        }

        StatusMessage msg;
        r = sh->GetStatusMessage(&msg);
        if (0 != r) {
          std::scoped_lock<std::mutex> lk(err_mut);
          err = r;
          err_serial = sh->GetSerialNumber();
          return;
        }

        sh->ClearDirty();
      });

    // This only returns the last error that occurred. Most likely, all scan
    // heads will have the same error so it's not too big of a deal to not
    // report all errors at once. Maybe this can be changed in the future.
    if (0 > err) {
      auto sh = m_serial_to_scan_head[err_serial];
      RETURN_ERROR(sh->GetErrorExtended(), err);
    }
  }

  // Now that all the configuration data is sent, we can calculate the phase
  // table and by extension the minimum scan period. We have to do things this
  // way as some of the configuration data affects timing.
  //
  // NOTE: If the configuration is dirty, we need to recalculate the phase
  // table as timings might have changed.
  if (is_config_dirty || is_phase_table_dirty) {
    auto table = m_phase_table.CalculatePhaseTable();
    m_min_scan_period_us = table.total_duration_us +
                           table.camera_early_offset_us;

    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      sh->ResetScanPairs();
    }

    // Set up the scan pairs; this defines what scans and when within the phase.
    uint32_t end_offset_us = table.camera_early_offset_us;
    if (0 != table.phases.size()) {
      for (auto &phase : table.phases) {
        end_offset_us += phase.duration_us;
        for (auto &el : phase.elements) {
          ScanHead *sh = el.scan_head;
          jsCamera camera = el.camera;
          jsLaser laser = el.laser;
          jsScanHeadConfiguration &cfg = el.cfg;
          int r = sh->AddScanPair(camera, laser, cfg, end_offset_us);
          if (0 != r) {
            RETURN_ERROR(sh->GetErrorExtended(), r);
          }
        }
      }
    }

    // Now that scan pairs are set, we can send the alignment.
    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      if (0 != sh->GetScanPairsCount()) {
        int r = sh->SendScanAlignmentValue();
        if (0 != r) {
          RETURN_ERROR(sh->GetErrorExtended(), r);
        }
      }
    }

    m_phase_table.ClearDirty();
  }

  return 0;
}

uint32_t ScanManager::GetMinScanPeriod()
{
  CLEAR_ERROR();

  if (!IsConnected()) {
    return 0;
  }

  Configure();
  return m_min_scan_period_us;
}

jsUnits ScanManager::GetUnits() const
{
  return m_units;
}

bool ScanManager::IsConfigured() const
{
  bool is_configured = true;
  for (auto const &pair : m_serial_to_scan_head) {
    ScanHead *sh = pair.second;
    if (sh->IsDirty()) {
      is_configured = false;
      break;
    }
  }

  return is_configured;
}

std::string ScanManager::GetErrorExtended() const
{
  return m_error_extended_str;
}

void ScanManager::KeepAliveThread()
{
  // The server will keep itself scanning as long as it can send profile data
  // over TCP. This keep alive is really only needed to get scan head's to
  // recover in the event that they fail to send and go into idle state.
  const uint32_t keep_alive_send_ms = 1000;

  // TODO: Revisit heartbeat, we needed to get 16.3.1 out quickly
  if (false) {
    // Silently exit when we have heart beat support
    if (m_version_scan_head_lowest.IsCompatible(16, 3, 0)) {
      return;
    }
  }

  while (1) {
    std::unique_lock<std::mutex> lk(m_mutex);
    m_condition.wait_for(lk, std::chrono::milliseconds(keep_alive_send_ms));

    if (SystemState::Close == m_state) {
      return; // close the thread
    } else if (SystemState::Scanning != m_state) {
      continue;
    }

    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *scan_head = pair.second;
      scan_head->SendKeepAlive();
    }
  }
}

void ScanManager::HeartBeatThread()
{
  constexpr uint32_t SEND_INTERVAL_MS = 250;
  constexpr uint32_t DEFAULT_TIMEOUT_SEC = 0;
  constexpr uint32_t DEFAULT_TIMEOUT_USEC =
    (SEND_INTERVAL_MS * 1000 ) * 2;
  constexpr uint32_t CONNECTED_TIMEOUT_SEC = 1;
  constexpr uint32_t CONNECTED_TIMEOUT_USEC = 0;

  struct timeval timeout;
  timeout.tv_sec = DEFAULT_TIMEOUT_SEC;
  timeout.tv_usec = DEFAULT_TIMEOUT_USEC;

  // TODO: Revisit heartbeat, we needed to get 16.3.1 out quickly
  return;

  // Silently exit when we don't have compatible devices
  if (!m_version_scan_head_lowest.IsCompatible(16, 3, 0)) {
    return;
  }

  while (1) {
    std::unique_lock<std::mutex> lk(m_mutex);
    m_condition.wait_for(lk, std::chrono::milliseconds(SEND_INTERVAL_MS));

    if (SystemState::Close == m_state) {
      return; // close the thread
    } else if (SystemState::Disconnected == m_state) {
      continue;
    }

    // Set timeout based on system state
    if (SystemState::Connected == m_state) {
      // During the switch from connected to scanning,
      // scanner setup time takes a while, so just set a higher timeout
      timeout.tv_sec = CONNECTED_TIMEOUT_SEC;
      timeout.tv_usec = CONNECTED_TIMEOUT_USEC;
    } else {
      timeout.tv_sec = DEFAULT_TIMEOUT_SEC;
      timeout.tv_usec = DEFAULT_TIMEOUT_USEC;
    }

    // Send heartbeats to all scan heads
    for (const auto& pair : m_serial_to_scan_head) {
      ScanHead* scan_head = pair.second;
      scan_head->GetHeartBeat(&timeout);
    }
  }
}
