#ifndef _JOESCAN_DATAGRAM_HEADER
#define _JOESCAN_DATAGRAM_HEADER

#include <cstdint>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include "TcpSerializationHelpers.hpp"

namespace joescan {
/**
 * This is the fixed size header for each datagram. The bytes have been
 * packed to word size, so that we can serialize this without resorting to
 * changing the compiler struct packing. All elements are in network byte
 * order, so all elements larger than 1 byte must be converted with
 * hton/ntoh.
 */

struct DatagramHeader {              // size  byte offset
  uint16_t magic = 0;                // 2      0
  uint16_t exposure_time_us = 0;     // 2      2
  uint8_t scan_head_id = 0;          // 1      4
  uint8_t camera_port = 0;           // 1      5
  uint8_t laser_port = 0;            // 1      6
  uint8_t flags = 0;                 // 1      7
  uint64_t timestamp_ns = 0;         // 8      8
  uint16_t laser_on_time_us = 0;     // 2     16
  uint16_t data_type = 0;            // 2     18
  uint16_t data_length = 0;          // 2     20
  uint8_t number_encoders = 0;       // 1     22
  uint8_t DEPRECATED_DO_NOT_USE = 0; // 1     23
  uint32_t datagram_position = 0;    // 4     24
  uint32_t number_datagrams = 0;     // 4     28
  uint16_t start_column = 0;         // 2     32
  uint16_t end_column = 0;           // 2     34
  uint32_t sequence_number = 0;      // 4     36
                                     // total 40
  static constexpr uint32_t kSize = 40;

  void SerializeToBytes(uint8_t *dst)
  {
    uint64_t *pu64 = reinterpret_cast<uint64_t *>(dst);
    uint32_t *pu32 = reinterpret_cast<uint32_t *>(dst);
    uint16_t *pu16 = reinterpret_cast<uint16_t *>(dst);
    uint8_t *pu8 = dst;

    pu16[0] = htons(magic);
    pu16[1] = htons(exposure_time_us);
    pu8[4] = scan_head_id;
    pu8[5] = camera_port;
    pu8[6] = laser_port;
    pu8[7] = flags;
    pu64[1] = hostToNetwork<uint64_t>(timestamp_ns);
    pu16[8] = htons(laser_on_time_us);
    pu16[9] = htons(data_type);
    pu16[10] = htons(data_length);
    pu8[22] = number_encoders;
    pu8[23] = DEPRECATED_DO_NOT_USE;
    pu32[6] = htonl(datagram_position);
    pu32[7] = htonl(number_datagrams);
    pu16[16] = htons(start_column);
    pu16[17] = htons(end_column);
    pu32[9] = htonl(sequence_number);
  }
};
} // namespace joescan

#endif
