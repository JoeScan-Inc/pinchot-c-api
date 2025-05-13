/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_NETWORK_INCLUDES_H
#define JOESCAN_NETWORK_INCLUDES_H

/// Macro to append file, line number, and errno message to a string
#define NETWORK_TRACE \
  (std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " + \
   strerror(errno));

#ifdef _WIN32

// TODO: inet_addr is deprecated in Windows
// suggested to use either inet_pton or InetPton
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#else

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
// Windows specific includes missing in Linux
#ifndef SOCKET
#define SOCKET int
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#endif

#endif
