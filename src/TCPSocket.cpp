// TODO: replace strerror with strerror_s
#define _CRT_SECURE_NO_WARNINGS

#include "TCPSocket.hpp"
#include "joescan_pinchot.h"

using namespace joescan;

TCPSocket::TCPSocket(std::string client_name, uint32_t client_ip, uint32_t ip,
                     uint16_t port, uint32_t timeout_s) :
  m_timeout_s(timeout_s)
{
  SOCKET sockfd = INVALID_SOCKET;
  int r = 0;

  NetworkInterface::Open();

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == sockfd) {
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }

  // Force all network traffic to go through the specific interface.
  struct sockaddr_in client_addr;
  client_addr.sin_family = AF_INET;
  client_addr.sin_addr.s_addr = htonl(client_ip);
  client_addr.sin_port = 0;
  r = bind(sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr));
  if (0 > r) {
    NetworkInterface::Close();
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }

#ifndef _WIN32
  if (!client_name.empty()) {
    // For Linux, the IP address is owned by the host, rather that the
    // interface, so routing still can get confused. By binding to the
    // device itself by the name, we can ensure no routing issues.
    r = setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, client_name.c_str(),
                   client_name.length());
    if (0 > r) {
      NetworkInterface::Close();
      std::string e = NETWORK_TRACE;
      throw std::runtime_error(e);
    }
  }
#endif

  // socket is open, assign to member variable to close on error by destructor
  m_iface.sockfd = sockfd;
  m_iface.ip_addr = 0;
  m_iface.port = 0;

#ifdef _WIN32
  unsigned long mode = 1;
  r = ioctlsocket(sockfd, FIONBIO, &mode);
  if (0 != r) {
    NetworkInterface::Close();
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }
#else
  int flags = fcntl(sockfd, F_GETFL, 0);
  r = fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
  if (0 != r) {
    NetworkInterface::Close();
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }
#endif

  int one = 1;
#ifdef _WIN32
  r = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<char*>(&one), sizeof(one));
#else
  r = setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
  if (0 != r) {
    NetworkInterface::Close();
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(ip);
  r = connect(sockfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
  if (0 != r) {
    int err = 0;
    int check = 0;
#ifdef _WIN32
    // connect function (winsock2.h): The socket is marked as nonblocking and
    // the connection cannot be completed immediately.
    check = WSAEWOULDBLOCK;
    err = WSAGetLastError();
#else
    check = EINPROGRESS;
    err = errno;
#endif
    if (err != check) {
      NetworkInterface::Close();
      std::string e = std::to_string(errno);
      throw std::runtime_error(e);
    }
  }

  r = SelectWaitWrite();
  if ((0 != r) && (EINPROGRESS != errno)) {
    NetworkInterface::Close();
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }

  // get socket information
  socklen_t len = sizeof(addr);
  r = getsockname(sockfd, reinterpret_cast<struct sockaddr *>(&addr), &len);
  if (0 != r) {
    NetworkInterface::Close();
    std::string e = NETWORK_TRACE;
    throw std::runtime_error(e);
  }
  m_iface.ip_addr = ntohl(addr.sin_addr.s_addr);
  m_iface.port = ntohs(addr.sin_port);
}

TCPSocket::TCPSocket(uint32_t ip, uint16_t port, uint32_t timeout_s)
{
  std::string empty_string;
  uint32_t ip_addr_any = 0;

  // This constructor will have code in place to handle empty client interface
  TCPSocket(empty_string, ip_addr_any, ip, port, timeout_s);
}

int TCPSocket::Send(flatbuffers::FlatBufferBuilder &builder)
{
  return Send(builder.GetBufferPointer(), builder.GetSize());
}

int TCPSocket::Send(uint8_t *buf, uint32_t len)
{
  SOCKET fd = m_iface.sockfd;
  char *src;
  uint32_t n;
  int r;

  if (INVALID_SOCKET == fd) {
    return JS_ERROR_NETWORK;
  }

  // NOTE: sending little-endian as to keep with approach used by Flatbuffers
  src = reinterpret_cast<char *>(&len);
  n = 0;
  errno = 0;
  do {
    r = SelectWaitWrite();
    if (0 != r) {
      return r;
    }

    r = send(fd, src + n, sizeof(uint32_t) - n, 0);
    if (0 > r) {
      int err = 0;
      int check = 0;
#ifdef _WIN32
      // connect function (winsock2.h): The socket is marked as nonblocking and
      // the connection cannot be completed immediately.
      check = WSAEWOULDBLOCK;
      err = WSAGetLastError();
#else
      check = EINPROGRESS;
      err = errno;
#endif
      if (err != check) {
        NetworkInterface::Close();
        return JS_ERROR_NETWORK;
      }
    } else if (r > 0) {
      n += r;
    }
  } while (n < sizeof(uint32_t));

  src = reinterpret_cast<char *>(buf);
  n = 0;
  do {
    r = SelectWaitWrite();
    if (0 != r) {
      return r;
    }

    r = send(fd, src + n, len - n, 0);
    if (0 > r) {
      int err = 0;
      int check = 0;
#ifdef _WIN32
      // connect function (winsock2.h): The socket is marked as nonblocking and
      // the connection cannot be completed immediately.
      check = WSAEWOULDBLOCK;
      err = WSAGetLastError();
#else
      check = EINPROGRESS;
      err = errno;
#endif
      if (err != check) {
        NetworkInterface::Close();
        return JS_ERROR_NETWORK;
      }
    } else if (r > 0) {
      n += r;
    }
  } while (n < len);

  return 0;
}

