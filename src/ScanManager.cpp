/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "ScanManager.hpp"
#include "ScanHead.hpp"

#include "BroadcastDiscover.hpp"
#include "FlatbufferMessages.hpp"
#include "RawProfileToProfile.hpp"
#include "StatusMessage.hpp"

#include <cmath>
#include <cstring>
#include <ctime>
#include <memory>
#include <sstream>
#include <stdexcept>

using namespace joescan;

uint32_t ScanManager::m_uid_count = 0;

ScanManager::ScanManager(jsUnits units, ScanSync *scansync) :
  m_scansync(scansync),
  m_state(SystemState::Disconnected),
  m_units(units)
{
  m_uid = ++m_uid_count;

  Discover();

  std::thread keep_alive_thread(&ScanManager::KeepAliveThread, this);
  m_keep_alive_thread = std::move(keep_alive_thread);
}

ScanManager::~ScanManager()
{
  {
    std::unique_lock<std::mutex> lk(m_mutex);
    m_state = SystemState::Close;
  }
  m_condition.notify_all();
  m_keep_alive_thread.join();
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

  return (int32_t) (m_serial_to_discovered.size());
}

int32_t ScanManager::ScanHeadsDiscovered(jsDiscovered *results,
                                         uint32_t max_results)
{
  jsDiscovered *dst = results;

  std::map<uint32_t, std::shared_ptr<jsDiscovered>>::iterator it =
    m_serial_to_discovered.begin();

  uint32_t results_len = (uint32_t) (m_serial_to_discovered.size() < max_results ?
                         m_serial_to_discovered.size() : max_results);

  for (uint32_t n = 0; n < results_len; n++) {
    memcpy(dst, it->second.get(), sizeof(jsDiscovered));
    dst++;
    it++;
  }

  return (int32_t) m_serial_to_discovered.size();
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
    m_state = SystemState::Connected;

    int r = Configure();
    if (0 != r) {
      return r;
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

int ScanManager::StartScanning(uint32_t period_us, jsDataFormat fmt,
                               bool is_frame_scanning)
{
  using namespace schema::client;

  int r = 0;

  if (!IsConnected()) {
    return JS_ERROR_NOT_CONNECTED;
  }

  if (IsScanning()) {
    return JS_ERROR_SCANNING;
  }

  r = Configure();
  if (0 != r) {
    return r;
  }

  if (m_min_scan_period_us > period_us) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  /**
   * TODO: At some point it might be interesting to move the Scan Configuration
   * Message to the `Configure` function so that we only have to send the Start
   * Scanning Message. We currently can't do that now because the API is set up
   * such that the scan period & data format are supplied with the call to
   * start scanning; they are unknown before then.
   */
  for (auto const &pair : m_serial_to_scan_head) {
    ScanHead *scan_head = pair.second;
    r = scan_head->SendScanConfiguration(period_us, fmt, is_frame_scanning);
    if (0 != r) {
      return r;
    }
  }

  scansync_data data;
  uint64_t start_time_ns = 0;
  if (0 == m_scansync->GetData(&data)) {
    // 20ms offset seems to work; less than that causes skipped sequences.
    const uint64_t kStartTimeOffsetNs = 20000000;
    start_time_ns = data.timestamp_ns + kStartTimeOffsetNs;
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
      return r;
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
  if (!IsScanning() || !m_is_frame_scanning) return JS_ERROR_NOT_SCANNING;

  // Sleep period to poll for frame status
  const int32_t sleep_us = m_scan_period_us / 4;
  int32_t time_remaining_us = timeout_us;
  int32_t r = 0;

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
      return 1;
    }

    if (time_remaining_us > 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
      time_remaining_us -= sleep_us;
    } else {
      break;
    }
  } while (1);

  return 0;
}

int32_t ScanManager::GetFrame(jsProfile *profiles)
{
  if (!IsScanning() || !m_is_frame_scanning) return JS_ERROR_NOT_SCANNING;

  int32_t r = 0;

  if (!m_is_frame_ready) {
    // User didn't call `WaitUntilFrameAvailable()` before; need to check to
    // see if a frame is actually ready or not.
    r = WaitUntilFrameAvailable(0);
    if (0 >= r) {
      return r;
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
  if (!IsScanning() || !m_is_frame_scanning) return JS_ERROR_NOT_SCANNING;

  int32_t r = 0;

  if (!m_is_frame_ready) {
    // User didn't call `WaitUntilFrameAvailable()` before; need to check to
    // see if a frame is actually ready or not.
    r = WaitUntilFrameAvailable(0);
    if (0 >= r) {
      return r;
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
  if (!IsScanning() || !m_is_frame_scanning) return JS_ERROR_NOT_SCANNING;

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

  return 0;
}

int32_t ScanManager::Configure()
{
  int r = 0;

  // Only run configure code if the scan system is connected & not scanning.
  if (IsScanning()) {
    return JS_ERROR_SCANNING;
  } else if (!IsConnected()) {
    return JS_ERROR_NOT_CONNECTED;
  }

  bool is_config_dirty = !IsConfigured();
  bool is_phase_table_dirty = m_phase_table.IsDirty();

  // Skip code below if we've already configured and nothing has changed.
  if (is_config_dirty) {
    // We break each data send into it's own loop as to avoid hitting one head
    // with a lot of data and slow down configuring the system. Breaking it down
    // into separate send loops makes this whole process pseudo parallel.
    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      r = sh->SendWindow();
      if (0 != r) {
        return r;
      }
    }

    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      r = sh->SendBrightnessCorrection();
      if (0 != r) {
        return r;
      }
    }

    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      r = sh->SendExclusionMask();
      if (0 != r) {
        return r;
      }
    }

    // grab status message for each scan head; these are currently cached
    // inside the scan head class and are used later when the phase table is
    // calculated
    // TODO: improve this functionality; it's really not apparent as to what is
    // happening and how data is propagated.
    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      StatusMessage msg;
      sh->GetStatusMessage(&msg);
    }

    // we've set up & sent all the relevant data, clear the "dirty" flag so
    // we don't go through these steps again
    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      sh->ClearDirty();
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
      ScanHead *scan_head = pair.second;
      scan_head->ResetScanPairs();
    }

    // Set up the scan pairs; this defines what scans and when within the phase.
    uint32_t end_offset_us = table.camera_early_offset_us;
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

    // Now that scan pairs are set, we can send the alignment.
    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *sh = pair.second;
      r = sh->SendScanAlignmentValue();
      if (0 != r) {
        return r;
      }
    }

    m_phase_table.ClearDirty();
  }

  return r;
}

uint32_t ScanManager::GetMinScanPeriod()
{
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

void ScanManager::KeepAliveThread()
{
  // The server will keep itself scanning as long as it can send profile data
  // over TCP. This keep alive is really only needed to get scan head's to
  // recover in the event that they fail to send and go into idle state.
  const uint32_t keep_alive_send_ms = 1000;

  while (1) {
    std::unique_lock<std::mutex> lk(m_mutex);
    m_condition.wait_for(lk, std::chrono::milliseconds(keep_alive_send_ms));

    if (SystemState::Close == m_state) {
      return;
    } else if (SystemState::Scanning != m_state) {
      continue;
    }

    for (auto const &pair : m_serial_to_scan_head) {
      ScanHead *scan_head = pair.second;
      scan_head->SendKeepAlive();
    }
  }
}
