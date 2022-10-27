#ifndef _JOESCAN_BROADCAST_DISCOVER_H
#define _JOESCAN_BROADCAST_DISCOVER_H

#ifndef NO_PINCHOT_INTERFACE
#include <map>
#include <chrono>
#include <memory>
#include <thread>

#include "joescan_pinchot.h"
#include "BroadcastDiscover.hpp"
#include "NetworkInterface.hpp"
#include "MessageDiscoveryClient_generated.h"
#include "MessageDiscoveryServer_generated.h"
#include "Version.hpp"
#endif

namespace joescan {

static constexpr uint16_t kBroadcastDiscoverPort = 12347;

#ifndef NO_PINCHOT_INTERFACE

/**
 * @brief Performs network UDP broadcast discover to find all available scan
 * heads on network interfaces of the client PC.
 *
 * @param discovered Map of serial numbers to discovery info populated with
 * scan head responses.
 * @returns `0` on success, negative value mapping to `jsError` on error.
 */
static int32_t BroadcastDiscover(
  std::map<uint32_t, std::shared_ptr<jsDiscovered>> &discovered)
{
  using namespace schema::client;
  std::vector<net_iface> ifaces;

  /////////////////////////////////////////////////////////////////////////////
  // STEP 1: Get all available interfaces.
  /////////////////////////////////////////////////////////////////////////////
  {
    const uint16_t port = 0;
    auto ip_addrs = NetworkInterface::GetActiveIpAddresses();
    for (auto const &ip_addr : ip_addrs) {
      try {
        net_iface iface = NetworkInterface::InitBroadcastSocket(ip_addr, port);
        ifaces.push_back(iface);
      } catch (const std::runtime_error &) {
        // Failed to init socket, continue since there might be other ifaces
      }
    }

    if (ifaces.size() == 0) {
      return JS_ERROR_NETWORK;
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
    for (auto const &iface : ifaces) {
      uint32_t ip_addr = iface.ip_addr;
      SOCKET fd = iface.sockfd;

      sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
      addr.sin_port = htons(kBroadcastDiscoverPort);

      uint8_t *buf = builder.GetBufferPointer();
      uint32_t size = builder.GetSize();
      int r = sendto(fd, reinterpret_cast<const char *>(buf), size, 0,
                     reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
      if (0 < r) {
        // sendto succeeded in sending message through interface
        sendto_count++;
      }
    }

    if (0 >= sendto_count) {
      // no interfaces were able to send UDP broadcast
      return JS_ERROR_NETWORK;
    }
  }

  // TODO: revist timeout? make it user controlled?
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  /////////////////////////////////////////////////////////////////////////////
  // STEP 3: See which (if any) scan heads responded.
  /////////////////////////////////////////////////////////////////////////////
  {
    using namespace schema::server;
    const uint32_t buf_len = 128;
    uint8_t *buf = new uint8_t[buf_len];

    for (auto const &iface : ifaces) {
      SOCKET fd = iface.sockfd;

      do {
        int r = recv(fd, reinterpret_cast<char *>(buf), buf_len, 0);
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
        result->ip_addr = msg->ip_server;
        result->link_speed_mbps = msg->link_speed_mbps;
        result->type = (jsScanHeadType) msg->type;
        result->firmware_version_major = msg->version_major;
        result->firmware_version_minor = msg->version_minor;
        result->firmware_version_patch = msg->version_patch;
        strncpy(result->type_str,
                msg->type_str.c_str(),
                JS_SCAN_HEAD_TYPE_STR_MAX_LEN - 1);

        discovered[msg->serial_number] = result;
      } while (1);
    }

    delete[] buf;
  }

  for (auto const &iface : ifaces) {
    NetworkInterface::CloseSocket(iface.sockfd);
  }

  return 0;
}

#endif

}

#endif
