/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_UDPCAST_SOCKET_H
#define JOESCAN_UDPCAST_SOCKET_H

#include "NetworkInterface.hpp"

namespace joescan {

 class UDPSocket : public NetworkInterface {
 public:
  UDPSocket(uint32_t ip, uint16_t port=0, uint32_t timeout_s=0);
  UDPSocket() = default;
  ~UDPSocket() = default;
  int Send(uint32_t ip, uint16_t port,
           flatbuffers::FlatBufferBuilder &builder) const;
  int Send(uint32_t ip, uint16_t port, uint8_t *buf, uint32_t len) const;
  int Read(uint8_t *buf, uint32_t len, sockaddr *addr = nullptr) const;

 private:
   uint32_t m_timeout_s;
};

}
#endif
