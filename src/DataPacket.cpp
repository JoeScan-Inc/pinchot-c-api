/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include <bitset>
#include <map>
#include <stdexcept>

#include "DataPacket.hpp"
#include "TcpSerializationHelpers.hpp"

#ifdef __linux__
#include <arpa/inet.h>
#else
#include <WinSock2.h>
#endif

using namespace joescan;

DataPacket::DataPacket(uint8_t *bytes, uint32_t num_bytes,
                       uint64_t received_timestamp)
{
  uint64_t *pu64 = reinterpret_cast<uint64_t *>(bytes);
  uint32_t *pu32 = reinterpret_cast<uint32_t *>(bytes);
  uint16_t *pu16 = reinterpret_cast<uint16_t *>(bytes);
  uint8_t *pu8 = bytes;

  // TODO: Check datagram size
  (void)num_bytes;

  m_raw = bytes;
  m_raw_len = num_bytes;

  m_hdr.magic = ntohs(pu16[0]);
  m_hdr.exposure_time_us = ntohs(pu16[1]);
  m_hdr.scan_head_id = pu8[4];
  m_hdr.camera_port = pu8[5];
  m_hdr.laser_port = pu8[6];
  m_hdr.flags = pu8[7];
  m_hdr.timestamp_ns = hostToNetwork<uint64_t>(pu64[1]);
  m_hdr.laser_on_time_us = ntohs(pu16[8]);
  m_hdr.data_type = ntohs(pu16[9]);
  m_hdr.data_length = ntohs(pu16[10]);
  m_hdr.number_encoders = pu8[22];
  m_hdr.datagram_position = ntohl(pu32[6]);
  m_hdr.number_datagrams = ntohl(pu32[7]);
  m_hdr.start_column = ntohs(pu16[16]);
  m_hdr.end_column = ntohs(pu16[17]);
  m_hdr.sequence_number = ntohl(pu32[9]);

  std::bitset<8 * sizeof(uint16_t)> contents_bits(m_hdr.data_type);
  m_num_content_types = static_cast<int>(contents_bits.count());

  unsigned int offset = DatagramHeader::kSize;
  const uint32_t encoder_offset = offset + (m_num_content_types * 2);
  unsigned int data_offset = encoder_offset + (m_hdr.number_encoders * 8);

  int64_t *p_enc = reinterpret_cast<int64_t *>(&bytes[encoder_offset]);
  for (uint32_t i = 0; i < m_hdr.number_encoders; i++) {
    m_encoders.push_back(hostToNetwork<int64_t>(*p_enc++));
  }

  for (int i = 1; i <= m_hdr.data_type; i <<= 1) {
    if ((m_hdr.data_type & i) != 0) {
      FragmentLayout layout;
      DataType data_type = static_cast<DataType>(i);
      layout.step = ntohs(*(reinterpret_cast<uint16_t *>(&bytes[offset])));
      layout.offset = data_offset;

      // All processed data types are sent in datagrams which must fit within
      // an ethernet frame. If multiple datagrams are required for a profile,
      // the data will be distributed among the datagrams such that if we
      // lose a datagram, we lose resolution, but won't have large holes in
      // the data.
      auto num_cols = m_hdr.end_column - m_hdr.start_column + 1;
      layout.num_vals = num_cols / (m_hdr.number_datagrams * layout.step);
      // If the data doesn't divide evenly into the DataPackets, each
      // DataPacket starting from the first will have 1 additional value of
      // the type in question.
      if (((num_cols / layout.step) % m_hdr.number_datagrams) >
          m_hdr.datagram_position) {
        layout.num_vals++;
      }
      layout.payload_size = GetSizeFor(data_type) * layout.num_vals;

      data_offset += layout.payload_size;
      offset += sizeof(uint16_t);
      fragment_layouts[data_type] = layout;
    }
  }
}

DatagramHeader DataPacket::GetHeader() const
{
  return m_hdr;
}

uint32_t DataPacket::GetSourceId() const
{
  return (m_hdr.scan_head_id << 16) | (m_hdr.camera_port << 8) |
         (m_hdr.laser_port);
}

uint8_t DataPacket::GetScanHeadId() const
{
  return m_hdr.scan_head_id;
}

uint32_t DataPacket::GetCameraPort() const
{
  return m_hdr.camera_port;
}

uint32_t DataPacket::GetLaserPort() const
{
  return m_hdr.laser_port;
}

uint64_t DataPacket::GetTimeStamp() const
{
  return m_hdr.timestamp_ns;
}

uint32_t DataPacket::GetSequenceNumber() const
{
  return m_hdr.sequence_number;
}

uint32_t DataPacket::GetPartNum() const
{
  return m_hdr.datagram_position;
}

uint32_t DataPacket::GetNumParts() const
{
  return m_hdr.number_datagrams;
}

int DataPacket::GetPayloadLength() const
{
  return m_hdr.data_length;
}

uint8_t DataPacket::NumEncoderVals() const
{
  return m_hdr.number_encoders;
}

uint16_t DataPacket::GetContents() const
{
  return m_hdr.data_type;
}

int DataPacket::GetNumContentTypes() const
{
  return m_num_content_types;
}

std::vector<int64_t> DataPacket::GetEncoderValues() const
{
  return m_encoders;
}

uint16_t DataPacket::GetLaserOnTime() const
{
  return m_hdr.laser_on_time_us;
}

uint16_t DataPacket::GetExposureTime() const
{
  return m_hdr.exposure_time_us;
}

uint8_t *DataPacket::GetRawBytes(uint32_t *byte_len) const
{
  *byte_len = m_raw_len;
  return m_raw;
}
