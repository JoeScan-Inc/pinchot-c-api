/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "ScanManager.hpp"
#include "ScanHead.hpp"

#include "BroadcastDiscover.hpp"
#include "NetworkInterface.hpp"
#include "NetworkTypes.hpp"
#include "ProfileBuilder.hpp"
#include "StatusMessage.hpp"

#include "MessageClient_generated.h"

#include <cmath>
#include <cstring>
#include <ctime>
#include <memory>
#include <sstream>
#include <stdexcept>

using namespace joescan;

uint32_t ScanManager::m_uid_count = 0;

ScanManager::ScanManager(jsUnits units) :
  m_state(SystemState::Disconnected),
  m_units(units)
{
  m_uid = ++m_uid_count;

  Discover();
}

ScanManager::~ScanManager()
{
  RemoveAllScanHeads();
}

uint32_t ScanManager::GetUID()
{
  return m_uid;
}

int32_t ScanManager::Discover()
{
  if (IsConnected()) {
    return JS_ERROR_CONNECTED;
  }

  int r = BroadcastDiscover(m_serial_to_discovered);
  if (0 != r) {
    return r;
  }

  return m_serial_to_discovered.size();
}

int32_t ScanManager::ScanHeadsDiscovered(jsDiscovered *results,
                                         uint32_t max_results)
{
  jsDiscovered *dst = results;

  std::map<uint32_t, std::shared_ptr<jsDiscovered>>::iterator it =
    m_serial_to_discovered.begin();

  uint32_t results_len = m_serial_to_discovered.size() < max_results ?
                         m_serial_to_discovered.size() : max_results;

  for (uint32_t n = 0; n < results_len; n++) {
    memcpy(dst, it->second.get(), sizeof(jsDiscovered));
    dst++;
    it++;
  }

  return m_serial_to_discovered.size();
}

PhaseTable *ScanManager::GetPhaseTable()
{
  return &m_phase_table;
}

int32_t ScanManager::CreateScanHead(uint32_t serial_number, uint32_t id)
{
  ScanHead *sh = nullptr;

  if (IsScanning()) {
    return JS_ERROR_SCANNING;
  }

  if (INT_MAX < id) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  if (m_serial_to_scan_head.find(serial_number) !=
      m_serial_to_scan_head.end()) {
    return JS_ERROR_ALREADY_EXISTS;
  }

  if (m_id_to_scan_head.find(id) != m_id_to_scan_head.end()) {
    return JS_ERROR_ALREADY_EXISTS;
  }

  if (m_serial_to_discovered.find(serial_number) ==
      m_serial_to_discovered.end()) {
    // try again
    Discover();
    if (m_serial_to_discovered.find(serial_number) ==
      m_serial_to_discovered.end()) {
      return JS_ERROR_NOT_DISCOVERED;
    }
  }

  auto discovered = m_serial_to_discovered[serial_number];
  if (API_VERSION_MAJOR != discovered->firmware_version_major) {
    return JS_ERROR_VERSION_COMPATIBILITY;
  }

  sh = new ScanHead(*this, *discovered, id);
  m_serial_to_scan_head[discovered->serial_number] = sh;
  m_id_to_scan_head[id] = sh;

  return 0;
}

ScanHead *ScanManager::GetScanHeadBySerial(uint32_t serial_number)
{
  auto res = m_serial_to_scan_head.find(serial_number);

  if (res == m_serial_to_scan_head.end()) {
    return nullptr;
  }

  return res->second;
}

ScanHead *ScanManager::GetScanHeadById(uint32_t id)
{
  auto res = m_id_to_scan_head.find(id);

  if (res == m_id_to_scan_head.end()) {
    return nullptr;
  }

  return res->second;
}

int32_t ScanManager::RemoveScanHead(uint32_t serial_number)
{
  if (IsScanning()) {
    return JS_ERROR_SCANNING;
  }

  auto res = m_serial_to_scan_head.find(serial_number);
  if (res == m_serial_to_scan_head.end()) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  uint32_t id = res->second->GetId();
  delete res->second;

  m_serial_to_scan_head.erase(serial_number);
  m_id_to_scan_head.erase(id);

  return 0;
}

int32_t ScanManager::RemoveScanHead(ScanHead *scan_head)
{
  if (scan_head == nullptr) {
    return JS_ERROR_NULL_ARGUMENT;
  }

  RemoveScanHead(scan_head->GetSerialNumber());

  return 0;
}

void ScanManager::RemoveAllScanHeads()
{
  if (IsScanning()) {
    std::string error_msg = "Can not remove scanners while scanning";
    throw std::runtime_error(error_msg);
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
}

uint32_t ScanManager::GetNumberScanners()
{
  return static_cast<uint32_t>(m_serial_to_scan_head.size());
}

int32_t ScanManager::Connect(uint32_t timeout_s)
{
  using namespace schema::client;

  if (IsScanning()) {
    return JS_ERROR_SCANNING;
  }

  if (IsConnected()) {
    return JS_ERROR_CONNECTED;
  }

  std::map<uint32_t, ScanHead *> connected;
  if (m_serial_to_scan_head.empty()) {
    return 0;
  }

  int32_t timeout_ms = timeout_s * 1000;
  for (auto const &pair : m_serial_to_scan_head) {
    uint32_t serial = pair.first;
    ScanHead *scan_head = pair.second;

    if (0 != scan_head->Connect(timeout_s)) {
      continue;
    }

    connected[serial] = scan_head;
  }

  if (connected.size() == m_serial_to_scan_head.size()) {
    int r = 0;

    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      sh->SendExclusionMask();
    }

    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      sh->SendWindow();
    }

    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      sh->SendBrightnessCorrection();
    }

    // get new status messages for each scan head so we can get an accurate
    // max scan rate
    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      StatusMessage msg;

      if (0 != sh->GetStatusMessage(&msg)) {
        connected.erase(sh->GetSerialNumber());
      }
    }

    if (connected.size() == m_serial_to_scan_head.size()) {
      m_state = SystemState::Connected;
    }
  }

  return int32_t(connected.size());
}

void ScanManager::Disconnect()
{
  using namespace schema::client;

  if (!IsConnected()) {
    std::string error_msg = "Not connected.";
    throw std::runtime_error(error_msg);
  }

  if (IsScanning()) {
    std::string error_msg = "Can not disconnect wile still scanning";
    throw std::runtime_error(error_msg);
  }

  for (auto const &pair : m_serial_to_scan_head) {
    ScanHead *scan_head = pair.second;
    scan_head->Disconnect();
  }

  m_state = SystemState::Disconnected;
}

int ScanManager::StartScanning(uint32_t period_us, jsDataFormat fmt)
{
  using namespace schema::client;

  int r = 0;

  if (!IsConnected()) {
    return JS_ERROR_NOT_CONNECTED;
  }

  if (IsScanning()) {
    return JS_ERROR_SCANNING;
  }

  auto table = m_phase_table.CalculatePhaseTable();

  if (table.total_duration_us > period_us) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  for (auto const &pair : m_serial_to_scan_head) {
    ScanHead *scan_head = pair.second;
    scan_head->ResetScanPairs();
  }

  // TODO: what do we do if the user never specifies a phase table? Do we create
  // a default table here? Lets possibly pursue this later on in another ticket.
  uint32_t end_offset_us =
    std::ceil(((double)kCameraStartEarlyOffsetNs) / 1000.0);

  if (0 != table.phases.size()) {
    for (auto &phase : table.phases) {
      end_offset_us += phase.duration_us;
      for (auto &el : phase.elements) {
        ScanHead *scan_head = el.scan_head;
        jsCamera camera = el.camera;
        jsLaser laser = el.laser;
        jsScanHeadConfiguration &cfg = el.cfg;
        scan_head->AddScanPair(camera, laser, cfg, end_offset_us);
      }
    }
  }

  for (auto const &pair : m_serial_to_scan_head) {
    ScanHead *scan_head = pair.second;

    r = scan_head->SetScanPeriod(period_us);
    if (0 != r) {
      return r;
    }

    r = scan_head->SetDataFormat(fmt);
    if (0 != r) {
      return r;
    }

    r = scan_head->SendScanConfiguration();
    if (0 != r) {
      return r;
    }
  }

  for (auto const &pair : m_serial_to_scan_head) {
    ScanHead *scan_head = pair.second;

    r = scan_head->StartScanning();
    if (0 != r) {
      return r;
    }
  }

  m_state = SystemState::Scanning;
  std::thread keep_alive_thread(&ScanManager::KeepAliveThread, this);
  m_keep_alive_thread = std::move(keep_alive_thread);

  return 0;
}

int32_t ScanManager::StopScanning()
{
  using namespace schema::client;

  if (!IsConnected()) {
    return JS_ERROR_NOT_CONNECTED;
  }

  if (!IsScanning()) {
    return JS_ERROR_NOT_SCANNING;
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
  m_keep_alive_thread.join();

  return 0;
}

uint32_t ScanManager::GetMinScanPeriod()
{
  const uint32_t camera_offset_us =
    std::ceil(((double)kCameraStartEarlyOffsetNs) / 1000.0);

  if (IsConnected()) {
    // user can send scan window after connecting now so we need to check the
    // scan head to see what the min period is
    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      StatusMessage msg;
      sh->GetStatusMessage(&msg);
    }
  }

  auto table = m_phase_table.CalculatePhaseTable();

  return camera_offset_us + table.total_duration_us;
}

jsUnits ScanManager::GetUnits() const
{
  return m_units;
}

void ScanManager::KeepAliveThread()
{
  // The server will keep itself scanning as long as it can send profile data
  // over TCP. This keep alive is really only needed to get scan head's to
  // recover in the event that they fail to send and go into idle state.
  const uint32_t keep_alive_send_ms = 1000;

  while (1) {
    std::unique_lock<std::mutex> lk(m_mutex);
    m_condition.wait_for(lk, std::chrono::milliseconds(keep_alive_send_ms));

    if (SystemState::Scanning != m_state) {
      return;
    }

    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *scan_head = pair.second;
      scan_head->SendKeepAlive();
    }
  }
}
