/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */
#ifndef _SCANSYNC_MANAGER_H
#define _SCANSYNC_MANAGER_H

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "joescan_pinchot.h"

namespace joescan {

/// Monitors ScanSyncs on the client computer
class ScanSyncManager {
 public:
  ScanSyncManager();
  ~ScanSyncManager();

  int32_t GetDiscoveredSize();
  std::vector<jsScanSyncDiscovered> GetDiscovered();
  int32_t GetStatus(uint32_t serial, jsScanSyncStatus *status);

 private:
  struct scansync_info {
    jsScanSyncDiscovered discovered;
    jsScanSyncStatus status;
  };

  int32_t ProcessPacket(uint8_t *src, uint32_t len, scansync_info *info);
  void RecvThread();

  std::map<uint32_t, scansync_info> m_serial_to_info;
  std::map<uint32_t, std::chrono::steady_clock::time_point> m_serial_to_last_seen;
  std::mutex m_mutex;
  std::thread m_thread;

  volatile bool m_is_thread_active;
};
} // namespace joescan

#endif
