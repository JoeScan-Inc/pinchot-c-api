/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_TCPSOCKET_H
#define JOESCAN_TCPSOCKET_H

#include <string>
#include "NetworkInterface.hpp"
#include "joescan_pinchot.h"

namespace joescan {

class TCPSocket : public NetworkInterface {
 public:
  TCPSocket(std::string client_name, uint32_t client_ip, uint32_t ip,
            uint16_t port, uint32_t timeout_s=0);
  TCPSocket(uint32_t ip, uint16_t port, uint32_t timeout_s=0);
  TCPSocket() = default;
  ~TCPSocket() = default;
  int Send(flatbuffers::FlatBufferBuilder &builder);
  int Send(uint8_t *buf, uint32_t len);
  inline int Read(uint8_t *buf, uint32_t len, volatile bool *is_cancel_flag=nullptr,
                  struct timeval *timeout=nullptr);

 private:
  inline int SelectWaitRead(struct timeval *timeout=nullptr);
  inline int SelectWaitWrite();

  uint32_t m_timeout_s;
};

/**
 * inline this function to get slightly faster profile reads
 */
inline int TCPSocket::Read(uint8_t *buf, uint32_t len,
                      volatile bool *is_read_active, struct timeval *timeout)
{
  SOCKET fd = m_iface.sockfd;
  char *dst;
  uint32_t msg_len;
  uint32_t n;
  int r;

  if (INVALID_SOCKET == fd) {
    return JS_ERROR_NETWORK;
  }

  // first, read the sync word indicating the TCP data length to be read out
  // NOTE: receiving little-endian as to keep with approach used by Flatbuffers
  dst = reinterpret_cast<char *>(&msg_len);
  n = 0;
  do {
    r = SelectWaitRead(timeout);
    if (0 >= r) {
      return r;
    }

    r = recv(fd, dst + n, sizeof(uint32_t) - n, 0);
    if (0 > r) {
      if ((nullptr != is_read_active) && !(*is_read_active)) {
        // received signal that we need to stop reading, used if thread calling
        // needs to be closed.
        return 0;
      }

#ifdef _WIN32
      int err = WSAGetLastError();
      if ((err == WSAEWOULDBLOCK) || (err == WSAEINTR)) {
        continue;
      }
#else
      if ((errno == EAGAIN) || (errno == EINTR) || (errno == 0)) {
        continue;
      }
#endif
      NetworkInterface::Close();
      return JS_ERROR_NETWORK;
    } else if (0 == r) {
      // connection closed
      return 0;
    }

    n += r;
  } while (n < sizeof(uint32_t));

  // somehow we ended up with the sync word saying that the message exceeds
  // the total memory ready to be read in; this is a big problem.
  if (msg_len > len) {
    throw std::runtime_error("Recv Out of Sync Requested " +
                              std::to_string(msg_len) + " bytes");
  }

  // second, read out the actual TCP data sent to client
  dst = reinterpret_cast<char *>(buf);
  n = 0;
  do {
    r = SelectWaitRead(timeout);
    if (0 > r) {
      return r;
    }

    r = recv(fd, dst + n, msg_len - n, 0);
    if (0 > r) {
      if ((nullptr != is_read_active) && !(*is_read_active)) {
      // received signal that we need to stop reading, used if thread calling
      // needs to be closed.
        return n;
      }
#ifdef _WIN32
      int err = WSAGetLastError();
      if ((err == WSAEWOULDBLOCK) || (err == WSAEINTR)) {
        continue;
      }
#else
      if ((errno == EAGAIN) || (errno == EINTR) || (errno == 0)) {
        continue;
      }
#endif
      NetworkInterface::Close();
      return JS_ERROR_NETWORK;
    } else if (0 == r) {
      // connection closed
      return 0;
    }

    n += r;
  } while (n < msg_len);

  return n;
}

inline int TCPSocket::SelectWaitRead(struct timeval *timeout)
{
  SOCKET sockfd = m_iface.sockfd;
  fd_set fds;
  struct timeval *ptv;
  struct timeval tv;
  int r = 0;

  while (true) {
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);
    if (timeout != nullptr) {
      tv.tv_sec = timeout->tv_sec;
      tv.tv_usec = timeout->tv_usec;
    } else {
      tv.tv_sec = 1;
      tv.tv_usec = 0;
    }

    ptv = &tv;
    r = select(int(sockfd) + 1, &fds, NULL, NULL, ptv);
    if (0 < r) {
      // activity on file descriptor
      return r;
    } else if (0 == r) {
      // we timed out after 1 second,
      // connection is very likely severed
      return 0;
    }
    // no activity on file descriptor

    int err = 0;
#ifdef _WIN32
    err = WSAGetLastError();
    if (err == WSAEINTR) {
        continue;
    }
#else
    err = errno;
    if (err == EINTR) {
        continue;
    }
#endif

    // fatal error that we don't handle
    NetworkInterface::Close();
    return JS_ERROR_NETWORK;
  }
}

inline int TCPSocket::SelectWaitWrite()
{
  SOCKET sockfd = m_iface.sockfd;
  fd_set fds;
  struct timeval *ptv;
  struct timeval tv;
  int r = 0;

  while(true) {
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);
    tv.tv_sec = m_timeout_s;
    tv.tv_usec = 0;
    ptv = (0 != m_timeout_s) ? &tv : nullptr;

    r = select(int(sockfd) + 1, NULL, &fds, NULL, ptv);
    if (0 < r) {
      // activity on file descriptor
      return r;
    } else if (0 == r) {
      // we timed out after 1 second,
      // connection is very likely severed
      return 0;
    }

    // no activity on file descriptor

    int err = 0;
#ifdef _WIN32
    err = WSAGetLastError();
    if (err == WSAEINTR) {
        continue;
    }
#else
    err = errno;
    if (err == EINTR) {
        continue;
    }
#endif

    // fatal error that we don't handle
    NetworkInterface::Close();
    return JS_ERROR_NETWORK;
  };
}

}
#endif
