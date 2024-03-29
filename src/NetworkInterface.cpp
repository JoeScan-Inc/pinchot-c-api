/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#if defined(WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "NetworkIncludes.hpp"
#include "NetworkInterface.hpp"
#include "NetworkTypes.hpp"
#include "joescan_pinchot.h"

using namespace joescan;

std::mutex NetworkInterface::m_mutex;
int NetworkInterface::m_ref_count = 0;

NetworkInterface::NetworkInterface()
{
  m_iface.sockfd = INVALID_SOCKET;
  m_iface.ip_addr = 0;
  m_iface.port = 0;
}

NetworkInterface::~NetworkInterface()
{
  Close();
}

void NetworkInterface::Open()
{
  m_mutex.lock();
  if (0 <= m_ref_count) {
#ifdef _WIN32
    WSADATA wsa;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (result != 0) {
      std::stringstream error_msg;
      error_msg << "Failed to initialize winsock: " << result;
      throw std::runtime_error(error_msg.str());
    }
#endif
  }
  m_ref_count++;
  m_mutex.unlock();
}

void NetworkInterface::Close()
{
  if (INVALID_SOCKET == m_iface.sockfd) {
    return;
  }

#ifdef _WIN32
  int lastError = WSAGetLastError();
  shutdown(m_iface.sockfd, SD_BOTH);
  closesocket(m_iface.sockfd);
  WSASetLastError(lastError);
#else
  shutdown(m_iface.sockfd, SHUT_RDWR);
  close(m_iface.sockfd);
#endif
  m_iface.sockfd = INVALID_SOCKET;
  m_iface.ip_addr = 0;
  m_iface.port = 0;

  m_mutex.lock();
  m_ref_count--;

  if (0 >= m_ref_count) {
#ifdef _WIN32
    WSACleanup();
#endif
  }
  m_mutex.unlock();
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

  const std::string port = std::to_string(kScanServerCtrlPort);
  std::string host = "JS-50-" + std::to_string(serial_number) + ".local";
  struct addrinfo* infoptr = nullptr;

  // TODO: switch to getaddrinfo_a before next release; should provide speedup
  // and allow us to use a timeout as well.
  int result = getaddrinfo(host.c_str(), port.c_str(), &hints, &infoptr);
  if (0 == result) {
    // resolved IP, extract address
    struct addrinfo* p;
    for (p = infoptr; p != NULL; p = p->ai_next) {
      if (AF_INET == p->ai_family) {
        struct sockaddr_in* a = reinterpret_cast<sockaddr_in*>(p->ai_addr);
        struct in_addr* addr = &(a->sin_addr);
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

uint32_t NetworkInterface::parseIPV4string(char *ip_str)
{
  uint32_t ipbytes[4];
  int r = sscanf(ip_str,
                 "%d.%d.%d.%d",
                 &ipbytes[3], &ipbytes[2], &ipbytes[1], &ipbytes[0]);
  // disregard return value
  (void)r;
  return ipbytes[0] | ipbytes[1] << 8 | ipbytes[2] << 16 | ipbytes[3] << 24;
}

std::vector<NetworkInterface::Client> NetworkInterface::GetClientInterfaces()
{
  std::vector<Client> ifaces;

#ifdef _WIN32
  {
    PIP_ADAPTER_INFO pAdapterInfo = nullptr;
    pAdapterInfo = (IP_ADAPTER_INFO*)malloc(sizeof(IP_ADAPTER_INFO));
    ULONG buflen = sizeof(IP_ADAPTER_INFO);

    if (ERROR_BUFFER_OVERFLOW == GetAdaptersInfo(pAdapterInfo, &buflen)) {
      free(pAdapterInfo);
      pAdapterInfo = (IP_ADAPTER_INFO*)malloc(buflen);
    }

    if (NO_ERROR == GetAdaptersInfo(pAdapterInfo, &buflen)) {
      PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
      while (pAdapter) {
        Client iface;
        iface.name = std::string(pAdapter->Description);
        iface.ip_addr =
          parseIPV4string(pAdapter->IpAddressList.IpAddress.String);
        iface.net_mask =
          parseIPV4string(pAdapter->IpAddressList.IpMask.String);

        if ((0 != iface.ip_addr) && (INADDR_LOOPBACK != iface.ip_addr)) {
          ifaces.push_back(iface);
        }
        pAdapter = pAdapter->Next;
      }
    } else {
      throw std::runtime_error("Failed to obtain network interfaces");
    }

    if (nullptr != pAdapterInfo) {
      free(pAdapterInfo);
    }
  }
#else
  {
    // BSD-style implementation
    struct ifaddrs *root_ifa;
    if (getifaddrs(&root_ifa) == 0) {
      struct ifaddrs *p = root_ifa;
      while (p) {
        Client iface;
        struct sockaddr *a = p->ifa_addr;
        iface.name = std::string(p->ifa_name);
        iface.ip_addr =
          ((a) && (a->sa_family == AF_INET)) ?
          ntohl((reinterpret_cast<struct sockaddr_in *>(a))->sin_addr.s_addr) :
          0;
        iface.net_mask =
          ((a) && (a->sa_family == AF_INET)) ?
          ntohl(((struct sockaddr_in *)(p->ifa_netmask))->sin_addr.s_addr) :
          0;

        if ((0 != iface.ip_addr) && (INADDR_LOOPBACK != iface.ip_addr)) {
          ifaces.push_back(iface);
        }
        p = p->ifa_next;
      }
      freeifaddrs(root_ifa);
    } else {
      throw std::runtime_error("Failed to obtain network interfaces");
    }
  }
#endif

  return ifaces;
}

bool NetworkInterface::IsOpen()
{
  return (INVALID_SOCKET != m_iface.sockfd);
}
