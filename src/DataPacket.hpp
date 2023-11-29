/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_DATA_PACKET_H
#define JOESCAN_DATA_PACKET_H

#include <bitset>
#include <cassert>
#include <vector>

#include "NetworkTypes.hpp"
#include "TcpSerializationHelpers.hpp"
#include "joescan_pinchot.h"

namespace joescan {
struct DataPacket {
  DataPacket() = default;
  DataPacket(const DataPacket &other) = default;

  DataPacket(uint8_t *bytes)
  {
    uint64_t *pu64 = reinterpret_cast<uint64_t *>(bytes);
    uint32_t *pu32 = reinterpret_cast<uint32_t *>(bytes);
    uint16_t *pu16 = reinterpret_cast<uint16_t *>(bytes);
    uint8_t *pu8 = bytes;

    header.magic = ntohs(pu16[0]);
    header.exposure_time_us = ntohs(pu16[1]);
    header.scan_head_id = pu8[4];
    header.camera_port = pu8[5];
    header.laser_port = pu8[6];
    header.flags = pu8[7];
    header.timestamp_ns = hostToNetwork<uint64_t>(pu64[1]);
    header.laser_on_time_us = ntohs(pu16[8]);
    header.data_type = ntohs(pu16[9]);
    header.data_length = ntohs(pu16[10]);
    header.number_encoders = pu8[22];
    header.datagram_position = ntohl(pu32[6]);
    header.number_datagrams = ntohl(pu32[7]);
    header.start_column = ntohs(pu16[16]);
    header.end_column = ntohs(pu16[17]);
    header.sequence_number = ntohl(pu32[9]);

    // This should always be `1` with when using TCP
    assert(1 == header.number_datagrams);
    assert(0 == header.datagram_position);

    // We assume the stride is consistent for all datatypes held in the profile
    data_stride = ntohs(pu16[DatagramHeader::kSize / sizeof(uint16_t)]);
    data_count = (header.end_column - header.start_column + 1) / data_stride;

    std::bitset<8 * sizeof(uint16_t)> contents_bits(header.data_type);
    const uint32_t num_data_types = static_cast<int>(contents_bits.count());

    uint32_t offset = 0;

    // NOTE: The order of deserialization of the data is *very* important. Be
    // extremely careful reordering any of the code below.

    offset = DatagramHeader::kSize + (num_data_types * sizeof(uint16_t));
    int64_t *p_enc = reinterpret_cast<int64_t *>(&bytes[offset]);
    for (uint32_t i = 0; i < header.number_encoders; i++) {
      encoders.push_back(hostToNetwork<int64_t>(*p_enc++));
      offset += sizeof(int64_t);
    }

    data_brightness = nullptr;
    if (header.data_type & uint32_t(DataType::Brightness)) {
      data_brightness = &bytes[offset];
      offset += sizeof(uint8_t) * data_count;
    }

    data_xy = nullptr;
    if (header.data_type & uint32_t(DataType::XYData)) {
      data_xy = reinterpret_cast<int16_t *>(&bytes[offset]);
      offset += 2 * sizeof(int16_t) * data_count;
    }

    data_subpixel = nullptr;
    if (header.data_type & uint32_t(DataType::Subpixel)) {
      // not used, skip...
    }
  }

  DatagramHeader header;
  std::vector<int64_t> encoders;
  uint32_t data_stride;
  uint32_t data_count;
  int16_t* data_xy;
  uint8_t* data_brightness;
  uint16_t* data_subpixel;
};

} // namespace joescan

#endif // JOESCAN_DATA_PACKET_H
