#include "ScanSync.hpp"
#include "UDPSocket.hpp"

#include "scansync_network_defines.h"
#include "joescan_pinchot.h"

using namespace joescan;

ScanSync::ScanSync() : m_serial(kInvalidSerial)
{
  m_is_thread_active = true;
  std::thread recv_thread(&ScanSync::RecvThread, this);
  m_thread = std::move(recv_thread);
}

ScanSync::~ScanSync()
{
  m_is_thread_active = false;
  m_thread.join();
}

int32_t ScanSync::GetData(scansync_data *data)
{
  if (kInvalidSerial == m_serial) {
    // TODO: New error code to report back there is no ScanSync?
    return JS_ERROR_NETWORK;
  }

  m_mutex.lock();
  data->serial = m_serial;
  data->timestamp_ns = m_timestamp_ns;
  data->encoder = m_encoder;
  m_mutex.unlock();

  return 0;
}

void ScanSync::RecvThread()
{
  uint8_t buf[SCANSYNC_PACKET_V2_SIZE_BYTES];
  int r;

  UDPSocket udp(INADDR_ANY, SCANSYNC_UDP_PORT, 1);

  while (1) {
    if (!m_is_thread_active) {
      return;
    }

    // NOTE: ScanSync packets are sent every 1ms.
    r = udp.Read(buf, SCANSYNC_PACKET_V2_SIZE_BYTES);
    if (r < SCANSYNC_PACKET_V1_SIZE_BYTES) {
      // Read something other than a ScanSync packet?
      // TODO: Is there something we should do? Exit?
      continue;
    }

    // Read enough data to guarantee we should have valid ScanSync data

    uint32_t *src = (uint32_t *)buf;
    uint32_t serial = htonl(src[0]);

    // TODO: Support multiple ScanSyncs / encoders. For now, we will keep to
    // the approach we've somewhat standardized on for internal use where the
    // lowest numbered serial number will determine which ScanSync is the
    // master that reports back `JS_ENCODER_MAIN`.
    if (serial > m_serial) {
      // More than one Scansync on the network, discard...
      continue;
    }

    m_mutex.lock();
    m_serial = serial;

    uint64_t timestamp_ns = 0;
    timestamp_ns = htonl(src[2]);
    timestamp_ns *= 1000000000;
    timestamp_ns += htonl(src[3]);
    m_timestamp_ns = timestamp_ns;

    int64_t encoder = 0;
    encoder = htonl(src[6]);
    encoder <<= 32;
    encoder |= htonl(src[7]);
    m_encoder = encoder;
    m_mutex.unlock();
  }
}
