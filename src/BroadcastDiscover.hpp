#ifndef _JOESCAN_BROADCAST_DISCOVER_H
#define _JOESCAN_BROADCAST_DISCOVER_H

// TODO: replace strerror with strerror_s
#define _CRT_SECURE_NO_WARNINGS

#ifndef NO_PINCHOT_INTERFACE
#include <map>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "joescan_pinchot.h"
#include "FlatbufferMessages.hpp"
#include "UDPBroadcastSocket.hpp"
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
    for (auto const &socket : sockets) {
      int r = socket->Send(kBroadcastDiscoverPort, builder);
      if (0 == r) {
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

        discovered[msg->serial_number] = result;
      } while (1);
    }

    delete[] buf;
  }

  return 0;
}

#endif

}

#endif
