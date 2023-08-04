/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_UDPBROADCAST_SOCKET_H
#define JOESCAN_UDPBROADCAST_SOCKET_H

#include "NetworkInterface.hpp"
#include "UDPSocket.hpp"

namespace joescan {

class UDPBroadcastSocket : public UDPSocket {
 public: 
  UDPBroadcastSocket(uint32_t ip, uint16_t port=0);
  UDPBroadcastSocket() = default;
  ~UDPBroadcastSocket() = default;
  int Send(uint16_t port, flatbuffers::FlatBufferBuilder &builder) const;
  int Send(uint16_t port, uint8_t *buf, uint32_t len) const;
};

}
#endif
