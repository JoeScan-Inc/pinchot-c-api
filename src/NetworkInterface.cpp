/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "NetworkIncludes.hpp"
#include "NetworkInterface.hpp"
#include "NetworkTypes.hpp"

#include <fcntl.h>
#ifdef __linux__
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

#define ERROR_STR \
  (std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " + \
   strerror(errno));

using namespace joescan;

void NetworkInterface::InitSystem(void)
{
#ifdef __linux__
#else
  WSADATA wsa;
  int result = WSAStartup(MAKEWORD(2, 2), &wsa);
  if (result != 0) {
    std::stringstream error_msg;
    error_msg << "Failed to initialize winsock: " << result;
    throw std::runtime_error(error_msg.str());
  }
#endif
}

void NetworkInterface::FreeSystem(void)
{
#ifdef __linux__
#else
  WSACleanup();
#endif
}

net_iface NetworkInterface::InitBroadcastSocket(uint32_t ip, uint16_t port)
{
  net_iface iface;
  SOCKET sockfd = INVALID_SOCKET;
  int r = 0;

  iface = InitUDPSocket(ip, port);
  sockfd = iface.sockfd;

#if __linux__
  int bcast_en = 1;
#else
  char bcast_en = 1;
#endif
  r = setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &bcast_en, sizeof(bcast_en));
  if (SOCKET_ERROR == r) {
    CloseSocket(sockfd);
    std::string e = ERROR_STR;
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

  return iface;
}

net_iface NetworkInterface::InitRecvSocket(uint32_t ip, uint16_t port)
{
  net_iface iface;
  SOCKET sockfd = INVALID_SOCKET;
  int r = 0;

  iface = InitUDPSocket(ip, port);
  sockfd = iface.sockfd;

  {
    int m = 0;
    int n = kRecvSocketBufferSize;
#ifdef __linux__
    socklen_t sz = sizeof(n);
    r = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, static_cast<void *>(&n), sz);
    if (SOCKET_ERROR != r) {
      r = getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, static_cast<void *>(&m), &sz);
    }

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
#else
    int sz = sizeof(n);
    r = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&n), sz);
    if (SOCKET_ERROR != r) {
      r = getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char *>(&m), &sz);
    }

    // WINDOWS
    DWORD tv = 1 * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
#endif
  }

  return iface;
}

net_iface NetworkInterface::InitSendSocket(uint32_t ip, uint16_t port)
{
  net_iface iface;

  iface = InitUDPSocket(ip, port);

  return iface;
}

void NetworkInterface::CloseSocket(SOCKET sockfd)
{
#ifdef __linux__
  shutdown(sockfd, SHUT_RDWR);
  close(sockfd);
#else
  int lastError = WSAGetLastError();
  shutdown(sockfd, SD_BOTH);
  closesocket(sockfd);
  WSASetLastError(lastError);
#endif
}

std::vector<uint32_t> NetworkInterface::GetActiveIpAddresses()
{
  std::vector<uint32_t> ip_addrs;

#ifdef __linux__
  {
    // BSD-style implementation
    struct ifaddrs *root_ifa;
    if (getifaddrs(&root_ifa) == 0) {
      struct ifaddrs *p = root_ifa;
      while (p) {
        struct sockaddr *a = p->ifa_addr;
        uint32_t ip_addr = ((a) && (a->sa_family == AF_INET))
                             ? ntohl((reinterpret_cast<struct sockaddr_in *>(a))->sin_addr.s_addr)
                             : 0;

        if ((0 != ip_addr) && (INADDR_LOOPBACK != ip_addr)) {
          ip_addrs.push_back(ip_addr);
        }
        p = p->ifa_next;
      }
      freeifaddrs(root_ifa);
    } else {
      throw std::runtime_error("Failed to obtain network interfaces");
    }
  }
#else
  {
    PMIB_IPADDRTABLE pIPAddrTable = nullptr;
    DWORD dwSize = 0;
    DWORD dwRetVal = 0;

    // before calling AddIPAddress we use GetIpAddrTable to get an adapter to
    // which we can add the IP
    pIPAddrTable = (MIB_IPADDRTABLE *)malloc(sizeof(MIB_IPADDRTABLE));
    if (pIPAddrTable) {
      // make an initial call to GetIpAddrTable to get the necessary size into
      // the dwSize variable
      if (GetIpAddrTable(pIPAddrTable, &dwSize, 0) ==
          ERROR_INSUFFICIENT_BUFFER) {
        free(pIPAddrTable);
        pIPAddrTable = (MIB_IPADDRTABLE *)malloc(dwSize);
        // make a second call to GetIpAddrTable to get the actual data we want
        GetIpAddrTable(pIPAddrTable, &dwSize, 0);
      }
    }

    if (pIPAddrTable) {
      // iterate over each IP address
      for (int n = 0; n < (int)pIPAddrTable->dwNumEntries; n++) {
        uint32_t ip_addr = ntohl((u_long)pIPAddrTable->table[n].dwAddr);
        if ((0 != ip_addr) && (INADDR_LOOPBACK != ip_addr)) {
          ip_addrs.push_back(ip_addr);
        }
      }
      free(pIPAddrTable);
    } else {
      throw std::runtime_error("Failed to obtain network interfaces");
    }
  }
#endif

  return ip_addrs;
}

int NetworkInterface::ResolveIpAddressMDNS(uint32_t serial_number, uint32_t *ip)
{
  struct addrinfo hints;
  uint32_t ip_addr = 0;

  std::memset(&hints, 0, sizeof(hints));
  // AF_INET means IPv4 only addresses
  hints.ai_family = AF_INET;
  hints.ai_flags = AI_NUMERICSERV;
  hints.ai_socktype = SOCK_DGRAM;

  const std::string port = std::to_string(kScanServerPort);
  std::string host = "JS-50-" + std::to_string(serial_number) + ".local";
  struct addrinfo *infoptr = nullptr;

  // TODO: switch to getaddrinfo_a before next release; should provide speedup
  // and allow us to use a timeout as well.
  int result = getaddrinfo(host.c_str(), port.c_str(), &hints, &infoptr);
  if (0 == result) {
    // resolved IP, extract address
    struct addrinfo *p;
    for (p = infoptr; p != NULL; p = p->ai_next) {
      if (AF_INET == p->ai_family) {
        struct sockaddr_in *a = reinterpret_cast<sockaddr_in*>(p->ai_addr);
        struct in_addr *addr = &(a->sin_addr);
        // this loop is probably overkill, address should be the same each pass
        ip_addr = htonl(addr->s_addr);
      }
    }
  }

  freeaddrinfo(infoptr);

  if (0 != result) {
    return -1;
  }

  *ip = ip_addr;

  return 0;
}

net_iface NetworkInterface::InitUDPSocket(uint32_t ip, uint16_t port)
{
  net_iface iface;
  SOCKET sockfd = INVALID_SOCKET;
  int r = 0;

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (-1 == sockfd) {
    std::string e = ERROR_STR;
    throw std::runtime_error(e);
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(ip);

  r = bind(sockfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
  if (0 != r) {
    CloseSocket(sockfd);
    std::string e = ERROR_STR;
    throw std::runtime_error(e);
  }

  socklen_t len = sizeof(addr);
  r = getsockname(sockfd, reinterpret_cast<struct sockaddr *>(&addr), &len);
  if (0 != r) {
    CloseSocket(sockfd);
    std::string e = ERROR_STR;
    throw std::runtime_error(e);
  }

  memset(&iface, 0, sizeof(net_iface));
  iface.sockfd = sockfd;
  iface.ip_addr = ntohl(addr.sin_addr.s_addr);
  iface.port = ntohs(addr.sin_port);

  return iface;
}

net_iface NetworkInterface::InitTCPSocket(uint32_t ip, uint16_t port,
                                          uint32_t timeout_s)
{
  net_iface iface;
  SOCKET sockfd = INVALID_SOCKET;
  int r = 0;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == sockfd) {
    std::string e = ERROR_STR;
    throw std::runtime_error(e);
  }

#ifdef __linux__
  struct timeval tv;
  tv.tv_sec = timeout_s;
  tv.tv_usec = 0;
#else
  uint32_t tv = timeout_s * 1000;
#endif

  const char *opt = reinterpret_cast<const char *>(&tv);

  r = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, opt, sizeof(tv));
  if (0 != r) {
    CloseSocket(sockfd);
    std::string e = ERROR_STR;
    throw std::runtime_error(e);
  }

  r = setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, opt, sizeof(tv));
  if (0 != r) {
    CloseSocket(sockfd);
    std::string e = ERROR_STR;
    throw std::runtime_error(e);
  }


  int one = 1;
#ifdef __linux__
  r = setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
#else
  r = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<char*>(&one), sizeof(one));
#endif
  if (0 != r) {
    CloseSocket(sockfd);
    std::string e = ERROR_STR;
    throw std::runtime_error(e);
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(ip);
  r = connect(sockfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
  if (0 != r) {
    CloseSocket(sockfd);
    std::string e = ERROR_STR;
    throw std::runtime_error(e);
  }

  socklen_t len = sizeof(addr);
  r = getsockname(sockfd, reinterpret_cast<struct sockaddr *>(&addr), &len);
  if (0 != r) {
    CloseSocket(sockfd);
    std::string e = ERROR_STR;
    throw std::runtime_error(e);
  }

  memset(&iface, 0, sizeof(net_iface));
  iface.sockfd = sockfd;
  iface.ip_addr = ntohl(addr.sin_addr.s_addr);
  iface.port = ntohs(addr.sin_port);

  return iface;
}
