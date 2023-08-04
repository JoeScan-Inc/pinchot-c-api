/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_NETWORK_TYPES_H
#define JOESCAN_NETWORK_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

#include "DatagramHeader.hpp"

#ifdef __linux__
#include <arpa/inet.h>
#else
#include <WinSock2.h>
#endif

namespace joescan {
/**
 * The maximum payload of an ethernet frame is 1500 bytes; since we want to
 * limit our datagrams to be conatined in a single ethernet frame, we split
 * all data into datagrams with a maximum of 1500 octets/bytes. Reserve 32
 * bytes for the IP & UDP headers.
 */
static const int kMaxFramePayload = 1468;

/// TCP buffer size set on the scan data streaming Tcp connection
static const int kTcpSendBufferSize = 4194304;

/// The port used to send commands to the server running on the scan head.
static const uint16_t kScanServerCtrlPort = 12346;
/// The port used to send scan data to the client
static const uint16_t kScanServerDataPort = 12348;
/// Identifier for Status message from scan server.
static const uint16_t kResponseMagic = 0xFACE;
/// Identifier for Data Packet message from scan server.
static const uint16_t kDataMagic = 0xFACD;
/// Identifier for Command messages from client.
static const uint16_t kCommandMagic = kResponseMagic; // Why share value?

using Datagram = std::vector<uint8_t>;

// The DataType is a bit field, it has the flag set for all data types
// present to be sent.
enum DataType : uint16_t {
  Brightness = 0x1,
  XYData = 0x2,
  Width = 0x4,
  SecondMoment = 0x8,
  Subpixel = 0x10,
  // others here, this is extensible
};

static int GetSizeFor(DataType data_type)
{
  switch (data_type) {
    case XYData: {
      return 2 * sizeof(uint16_t);
    }
    case Width:
    case SecondMoment:
    case Subpixel: {
      return sizeof(uint16_t);
    }
    case Brightness:
    default:
      return sizeof(uint8_t);
  }
}

inline DataType operator|(DataType original, DataType additionalType)
{
  return static_cast<DataType>(static_cast<uint16_t>(original) |
                               static_cast<uint16_t>(additionalType));
}

/**
 * This is the header for any packet that is _not_ a profile
 * or image data packet. This header should never change.
 */
#pragma pack(push, 1)
struct InfoHeader {
  InfoHeader() = default;
  InfoHeader(uint8_t *buf)
  {
    magic = htons(*reinterpret_cast<uint16_t *>(&buf[0]));
    size = buf[2];
    type = buf[3];
  }

  uint16_t magic;
  uint8_t size;
  uint8_t type;
};
#pragma pack(pop)

} // namespace joescan

#endif
