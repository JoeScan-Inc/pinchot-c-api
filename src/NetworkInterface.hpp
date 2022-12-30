/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_NETWORK_INTERFACE_H
#define JOESCAN_NETWORK_INTERFACE_H

#include <cstdint>
#include <vector>

#include "NetworkIncludes.hpp"

namespace joescan {
/**
 * @brief The `net_iface` struct is a container struct that helps group
 * data relating to a network interface.
 */
struct net_iface {
  SOCKET sockfd;
  uint32_t ip_addr;
  uint16_t port;
};

/**
 * @brief The `NetworkInterface` class provides operating system agnostic
 * calls for creating socket interfaces used by the client code.
 */
class NetworkInterface {
 public:
  static void InitSystem(void);
  static void FreeSystem(void);
  static net_iface InitBroadcastSocket(uint32_t ip, uint16_t port);
  static net_iface InitRecvSocket(uint32_t ip, uint16_t port);
  static net_iface InitSendSocket(uint32_t ip, uint16_t port);
  static net_iface InitTCPSocket(uint32_t ip, uint16_t port,
                                 uint32_t timeout_s);
  static void CloseSocket(SOCKET sockfd);

  static std::vector<uint32_t> GetActiveIpAddresses();
  static int ResolveIpAddressMDNS(uint32_t serial_number, uint32_t *ip);

 private:
  static net_iface InitUDPSocket(uint32_t ip, uint16_t port);
  // The OS buffer size in bytes for a UDP data socket connection.
  static const int kRecvSocketBufferSize = 0x400000;
};
} // namespace joescan

#endif
