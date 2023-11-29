/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_NETWORK_INTERFACE_H
#define JOESCAN_NETWORK_INTERFACE_H


#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "NetworkIncludes.hpp"
#include "flatbuffers/flatbuffers.h"

namespace joescan {

class NetworkInterface {
 public:
  /** @brief Struct used to hold client network interface values. */
  struct Client {
    std::string name;
    uint32_t ip_addr;
    uint32_t net_mask;
    Client() : ip_addr(0), net_mask(0) {}
  };

  NetworkInterface();
  ~NetworkInterface();

  static int ResolveIpAddressMDNS(uint32_t serial_number, uint32_t *ip);
  static uint32_t parseIPV4string(char* ip_str);
  static std::vector<Client> GetClientInterfaces();

  void Open();
  void Close();
  bool IsOpen();

 protected:
  /**
   * @brief The `net_iface` struct is a container struct that helps group
   * data relating to a network interface.
   */
  struct net_iface {
    SOCKET sockfd;
    uint32_t ip_addr;
    uint16_t port;
  };

  net_iface m_iface;

 private:
  static std::mutex m_mutex;
  static int m_ref_count;
};

} // namespace joescan

#endif
