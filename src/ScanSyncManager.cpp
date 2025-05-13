#include "ScanSyncManager.hpp"
#include "UDPSocket.hpp"

#include "scansync_network_defines.h"
#include "joescan_pinchot.h"

using namespace joescan;

ScanSyncManager::ScanSyncManager()
{
  m_is_thread_active = true;
  std::thread recv_thread(&ScanSyncManager::RecvThread, this);
  m_thread = std::move(recv_thread);
}

ScanSyncManager::~ScanSyncManager()
{
  m_is_thread_active = false;
  m_thread.join();
}

int32_t ScanSyncManager::GetDiscoveredSize()
{
  return (int32_t) m_serial_to_info.size();
}

std::vector<jsScanSyncDiscovered> ScanSyncManager::GetDiscovered()
{
  std::vector<jsScanSyncDiscovered> discovered;

  m_mutex.lock();
  for (auto &kvp : m_serial_to_info) {
    discovered.push_back(kvp.second.discovered);
  }
  m_mutex.unlock();

  // Return ScanSyncs in ascending serial number order.
  std::sort(
    discovered.begin(),
    discovered.end(),
    [](jsScanSyncDiscovered const &lhs, jsScanSyncDiscovered const &rhs)
      { return lhs.serial_number < rhs.serial_number; });

  return discovered;
}

int32_t ScanSyncManager::GetStatus(uint32_t serial, jsScanSyncStatus *status)
{
  int r = 0;

  m_mutex.lock();
  if (m_serial_to_info.find(serial) == m_serial_to_info.end()) {
    r = JS_ERROR_INVALID_ARGUMENT;
  } else {
    *status = m_serial_to_info[serial].status;
  }

  m_mutex.unlock();

  return r;
}

int32_t ScanSyncManager::ProcessPacket(uint8_t *src, uint32_t len,
                                       scansync_info *info)
{
  uint32_t *src32 = (uint32_t*) src;
  uint16_t *src16 = (uint16_t*) src;

  memset(info, 0, sizeof(scansync_info));

  // NOTE: Packet version came in with V3. We can't rely on it being set for
  // older versions. We'll have to rely on packet size and magic values to
  // determine the version instead...
  uint32_t packet_version = 0;
  if (SCANSYNC_PACKET_V1_SIZE_BYTES > len) {
    // Not enough data to comprise a ScanSync packet.
    return -1;
  } else if (SCANSYNC_PACKET_V1_SIZE_BYTES == len) {
    // Packet V1 is the earliest ScanSync packet version; packet version was
    // not present in this data.
    packet_version = 1;
  } else if (SCANSYNC_PACKET_V3_SIZE_BYTES > len) {
    // Anything larger than V3 size guarantees that it is set, so we don't need
    // to do anything funny...
    packet_version = ntohs(src16[30]);
  } else {
    // Only packet V3 has the packet version set. However, both V2 and V3 have
    // the same size. We need to look into the packet data to see if the
    // "reserved" fields in V2 are set to their magic values...
    //
    // V2 reserved fields (60 byte offset):
    //    packet->reserved_0 = 0xAAAAAAAA;
    //    packet->reserved_1 = 0xBBBBBBBB;
    //    packet->reserved_2 = 0xCCCCCCCC;
    //    packet->reserved_3 = 0xDDDDDDDD;
    //
    // We will check one of the reserved fields and assume that if it is set
    // appropriately, the others are set as well.
    uint32_t v2_reserved_0 = ntohl(src32[15]);
    if (0xAAAAAAAA == v2_reserved_0) {
      packet_version = 2;
    } else {
      packet_version = ntohs(src16[30]);
    }
  }

  // Fill in data according to the version of the ScanSync packet.
  uint64_t timestamp_ns = 0;
  int64_t encoder = 0;
  uint32_t flags = 0;

  if (0 == packet_version) {
    // invalid packet version
    return -1;
  }

  if (1 <= packet_version) {
    info->status.serial = ntohl(src32[0]);
    info->discovered.serial_number = info->status.serial;

    timestamp_ns = 0;
    timestamp_ns = ntohl(src32[2]);
    timestamp_ns *= 1000000000;
    timestamp_ns += ntohl(src32[3]);
    info->status.timestamp_ns = timestamp_ns;

    encoder = 0;
    encoder = ntohl(src32[6]);
    encoder <<= 32;
    encoder |= ntohl(src32[7]);
    info->status.encoder = encoder;
  }

  if (2 <= packet_version) {
    flags = ntohl(src32[8]);
    info->status.is_fault_a = (flags & FLAG_BIT_MASK_FAULT_A) ? true : false;
    info->status.is_fault_b = (flags & FLAG_BIT_MASK_FAULT_B) ? true : false;
    info->status.is_index_z = (flags & FLAG_BIT_MASK_INDEX_Z) ? true : false;
    info->status.is_sync = (flags & FLAG_BIT_MASK_SYNC) ? true : false;
    info->status.is_aux_y = (flags & FLAG_BIT_MASK_AUX_Y) ? true : false;

    timestamp_ns = 0;
    timestamp_ns = ntohl(src32[9]);
    timestamp_ns *= 1000000000;
    timestamp_ns += ntohl(src32[10]);
    info->status.aux_y_timestamp_ns = timestamp_ns;

    timestamp_ns = ntohl(src32[11]);
    timestamp_ns *= 1000000000;
    timestamp_ns += ntohl(src32[12]);
    info->status.index_z_timestamp_ns = timestamp_ns;

    timestamp_ns = ntohl(src32[13]);
    timestamp_ns *= 1000000000;
    timestamp_ns += ntohl(src32[14]);
    info->status.sync_timestamp_ns = timestamp_ns;
  }

  if (3 <= packet_version) {
    info->discovered.firmware_version_major = ntohs(src16[31]);
    info->discovered.firmware_version_minor = ntohs(src16[32]);
    info->discovered.firmware_version_patch = ntohs(src16[33]);
  }

  if (4 <= packet_version) {
    timestamp_ns = ntohl(src32[17]);
    timestamp_ns *= 1000000000;
    timestamp_ns += ntohl(src32[18]);
    info->status.is_laser_disable = (flags & FLAG_BIT_MASK_LASER_DISABLE) ? true : false;
    info->status.laser_disable_timestamp_ns = timestamp_ns;
  }

  return 0;
}

void ScanSyncManager::RecvThread()
{
  uint8_t buf[SCANSYNC_PACKET_MAX_SIZE_BYTES];
  int r;

  UDPSocket udp(INADDR_ANY, SCANSYNC_UDP_PORT, 1);

  const uint32_t kPollStatusTimeSec = 1;
  const uint32_t kTimeoutSec = 1;
  std::chrono::steady_clock::time_point time_current, time_previous; 
  time_current = std::chrono::steady_clock::now();
  time_previous = time_current;

  while (1) {
    if (!m_is_thread_active) {
      return;
    }

    time_current = std::chrono::steady_clock::now();
    uint32_t t_sec = (uint32_t)
      std::chrono::duration_cast<std::chrono::seconds>(
        time_current - time_previous).count();

    if (kPollStatusTimeSec < t_sec) {
      auto it = m_serial_to_last_seen.begin();
      while (it != m_serial_to_last_seen.end()) {
        auto serial_number = it->first;
        auto time_last_seen = it->second;

        t_sec = (uint32_t)
          std::chrono::duration_cast<std::chrono::seconds>(
          time_current - time_last_seen).count();

        if (kTimeoutSec < t_sec) {
          // ScanSync has disappeared from the network, let's remove it
          m_mutex.lock();
          it = m_serial_to_last_seen.erase(it);
          m_serial_to_info.erase(serial_number);
          m_mutex.unlock();
        } else {
          // ScanSync still here, check the next one...
          it++;
        }
      }
      time_previous = time_current;
    }

    // NOTE: ScanSyncManager packets are sent every 1ms.

    // We will only read out the maximum amount of data that we can parse from
    // the ScanSync packet. Given that the data is sent out as UDP packets, this
    // will automatically frame the data, and cause any bytes left unread to be
    // discarded by the operating system.
    sockaddr addr;
    r = udp.Read(buf, SCANSYNC_PACKET_MAX_SIZE_BYTES, &addr);
    if (r > 0) {

      scansync_info info;
      r = ProcessPacket(buf, r, &info);
      if ((0 == r) && (m_is_thread_active)) {
        // udp.Read will return a network error if packet is not from an IPV4 address
        sockaddr_in *addr_in = (sockaddr_in*) &addr;
        info.discovered.ip_addr = ntohl(addr_in->sin_addr.s_addr);

        m_mutex.lock();
        uint32_t serial = info.status.serial;
        m_serial_to_info[serial] = info;
        m_serial_to_last_seen[serial] = std::chrono::steady_clock::now();
        m_mutex.unlock();
      }
    }
  }
}
