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
  inline int Read(uint8_t *buf, uint32_t len, volatile bool *is_cancel_flag=nullptr);

 private:
  inline int SelectWaitRead();
  inline int SelectWaitWrite();

  uint32_t m_timeout_s;
};

/**
 * inline this function to get slightly faster profile reads
 */
inline int TCPSocket::Read(uint8_t *buf, uint32_t len, volatile bool *is_read_active)
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
    r = SelectWaitRead();
    if (0 != r) {
      return r;
    }

    r = recv(fd, dst + n, sizeof(uint32_t) - n, 0);
    if (0 > r) {
      if ((nullptr != is_read_active) && !(*is_read_active)) {
        // received signal that we need to stop reading, used if thread calling
        // needs to be closed.
        return 0;
      }

#ifdef __linux__
      if ((errno == EAGAIN) || (errno == EINTR) || (errno == 0)) {
        continue;
      }
#else
      int err = WSAGetLastError();
      if ((err == WSAEWOULDBLOCK) || (err == WSAEINTR)) {
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
    r = SelectWaitRead();
    if (0 != r) {
      return r;
    }

    r = recv(fd, dst + n, msg_len - n, 0);
    if (0 > r) {
      if ((nullptr != is_read_active) && !(*is_read_active)) {
      // received signal that we need to stop reading, used if thread calling
      // needs to be closed.
        return n;
      }
#ifdef __linux__
      if ((errno == EAGAIN) || (errno == EINTR) || (errno == 0)) {
        continue;
      }
#else
      int err = WSAGetLastError();
      if ((err == WSAEWOULDBLOCK) || (err == WSAEINTR)) {
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

inline int TCPSocket::SelectWaitRead()
{
  SOCKET sockfd = m_iface.sockfd;
  fd_set fds;
  struct timeval *ptv;
  struct timeval tv;
  int r = 0;

  do {
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    ptv = (0 != tv.tv_sec) ? &tv : nullptr;

    r = select(int(sockfd) + 1, &fds, NULL, NULL, ptv);
    if (0 < r) {
      // activity on file descriptor
      break;
    } else if (0 == r) {
      return 0;
    }

    // no activity on file descriptor
    int check = 0;
    int err = 0;
#ifdef __linux__
    check = EINTR;
    err = errno;
#else
    check = WSAEINTR;
    err = WSAGetLastError();
#endif
    if (err != check) {
      // timeout or error we don't handle
      NetworkInterface::Close();
      return JS_ERROR_NETWORK;
    }
  } while (1);

  return 0;
}

inline int TCPSocket::SelectWaitWrite()
{
  SOCKET sockfd = m_iface.sockfd;
  fd_set fds;
  struct timeval *ptv;
  struct timeval tv;
  int r = 0;

  do {
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);
    tv.tv_sec = m_timeout_s;
    tv.tv_usec = 0;
    ptv = (0 != m_timeout_s) ? &tv : nullptr;

    r = select(int(sockfd) + 1, NULL, &fds, NULL, ptv);
    if (0 < r) {
      // activity on file descriptor
      break;
    } else if (0 == r) {
      return 0;
    }

    // no activity on file descriptor
    int check = 0;
    int err = 0;
#ifdef __linux__
    check = EINTR;
    err = errno;
#else
    check = WSAEINTR;
    err = WSAGetLastError();
#endif
    if (err != check) {
      // timeout or error we don't handle
      NetworkInterface::Close();
      return JS_ERROR_NETWORK;
    }
  } while (1);

  return 0;
}

}
#endif
