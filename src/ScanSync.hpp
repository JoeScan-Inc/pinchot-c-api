/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */
#ifndef SCANSYNC_H
#define SCANSYNC_H

#include <cstdint>
#include <mutex>
#include <thread>

namespace joescan {

/// TODO: Consider making this available to end users?
struct scansync_data {
  uint32_t serial;
  uint64_t timestamp_ns;
  int64_t encoder;
};

class ScanSync {
 public:
  ScanSync();
  ~ScanSync();
  int32_t GetData(scansync_data *data);

 private:
  void RecvThread();

  const uint32_t kInvalidSerial = 0xFFFFFFFF;

  std::mutex m_mutex;
  std::thread m_thread;
  uint32_t m_serial;
  uint64_t m_timestamp_ns;
  int64_t m_encoder;
  volatile bool m_is_thread_active;
};
} // namespace joescan

#endif
