// TODO: replace strerror with strerror_s
#define _CRT_SECURE_NO_WARNINGS

#include "UDPBroadcastSocket.hpp"
#include <fcntl.h>

using namespace joescan;

UDPBroadcastSocket::UDPBroadcastSocket(uint32_t ip, uint16_t port) :
  UDPSocket(ip, port)
{
  SOCKET sockfd = m_iface.sockfd;
  int r = 0;

  // call to `NetworkInterface::Open()` is done in `UDPSocket::UDPSocket()`

#if __linux__
  int bcast_en = 1;
#else
  char bcast_en = 1;
#endif
  r = setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &bcast_en, sizeof(bcast_en));
  if (SOCKET_ERROR == r) {
    Close();
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }

#if __linux__
  int flags = fcntl(sockfd, F_GETFL, 0);
  assert(flags != -1);
  fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
#else
  u_long mode = 1; // 1 to enable non-blocking socket
  ioctlsocket(sockfd, FIONBIO, &mode);
#endif
}

int UDPBroadcastSocket::Send(uint16_t port,
                             flatbuffers::FlatBufferBuilder &builder) const
{
  uint32_t ip = INADDR_BROADCAST;
  return UDPSocket::Send(ip, port, builder.GetBufferPointer(),
                         builder.GetSize());
}

int UDPBroadcastSocket::Send(uint16_t port, uint8_t *buf, uint32_t len) const
{
  uint32_t ip = INADDR_BROADCAST;
  return UDPSocket::Send(ip, port, buf, len);
}
