// TODO: replace strerror with strerror_s
#define _CRT_SECURE_NO_WARNINGS

#include "UDPSocket.hpp"
#include "joescan_pinchot.h"

using namespace joescan;

UDPSocket::UDPSocket(uint32_t ip, uint16_t port, uint32_t timeout_s) :
  m_timeout_s(timeout_s)
{
  SOCKET sockfd = INVALID_SOCKET;
  int r = 0;

  NetworkInterface::Open();

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (-1 == sockfd) {
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }

  int reuse = 1;
  r = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<char *>(&reuse), sizeof(reuse));
  if (0 != r){
    Close();
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(ip);

  r = bind(sockfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
  if (0 != r) {
    Close();
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }

  socklen_t len = sizeof(addr);
  r = getsockname(sockfd, reinterpret_cast<struct sockaddr *>(&addr), &len);
  if (0 != r) {
    Close();
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }

  memset(&m_iface, 0, sizeof(net_iface));
  m_iface.sockfd = sockfd;
  m_iface.ip_addr = ntohl(addr.sin_addr.s_addr);
  m_iface.port = ntohs(addr.sin_port);
}

int UDPSocket::Send(uint32_t ip_addr, uint16_t port,
                    flatbuffers::FlatBufferBuilder &builder) const
{
  return Send(ip_addr, port, builder.GetBufferPointer(), builder.GetSize());
}

int UDPSocket::Send(uint32_t ip_addr, uint16_t port, uint8_t *buf,
                    uint32_t len) const
{
  SOCKET fd = m_iface.sockfd;

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(ip_addr);
  addr.sin_port = htons(port);

  int r = sendto(fd, reinterpret_cast<const char *>(buf), len, 0,
                 reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  if (0 > r) {
    // TODO: more meaningful error handling
    return JS_ERROR_NETWORK;
  }

  return 0;
}

int UDPSocket::Read(uint8_t *buf, uint32_t len) const
{
  SOCKET fd = m_iface.sockfd;

  struct timeval tv;
  fd_set rfds;
  int nfds;
  int r;

  nfds = (int) fd + 1;
  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);
  tv.tv_sec = m_timeout_s;
  tv.tv_usec = 0;

  r = select(nfds, &rfds, NULL, NULL, &tv);
  if (0 > r) {
    return JS_ERROR_NETWORK;
  } else if (0 == r) {
    return 0;
  }

  r = recv(m_iface.sockfd, reinterpret_cast<char *>(buf), len, 0);
  if (0 > r) {
    return JS_ERROR_NETWORK;
  }

  return r;
}
