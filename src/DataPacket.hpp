/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_DATA_PACKET_H
#define JOESCAN_DATA_PACKET_H

#include <map>
#include <memory>
#include <vector>

#include "NetworkTypes.hpp"
#include "joescan_pinchot.h"

namespace joescan {
struct FragmentLayout {
  unsigned int step = 0;
  unsigned int num_vals = 0;
  unsigned int offset = 0;
  unsigned int payload_size = 0;
};

class DataPacket {
 public:
  DataPacket() = default;
  DataPacket(uint8_t *bytes, uint32_t num_bytes, uint64_t received_timestamp);
  DataPacket(const DataPacket &other) = default;

  DatagramHeader GetHeader() const;
  uint32_t GetSourceId() const;
  uint8_t GetScanHeadId() const;
  uint32_t GetCameraPort() const;
  uint32_t GetLaserPort() const;
  uint64_t GetTimeStamp() const;
  uint32_t GetSequenceNumber() const;

  /**
   * Profile data can be transmitted over the wire through multiple UDP
   * packets if the payload is large enough. This function returns the
   * packet's sequential number within the total number of packets to be sent.
   *
   * @return The packet's number within all packets needed for the Profile.
   */
  uint32_t GetPartNum() const;

  /**
   * Profile data can be transmitted over the wire through multiple UDP
   * packets if the payload is large enough. This function returns the
   * total number of packets that comprise a given Profile's data.
   *
   * @return The total number of packets required for the Profile.
   */
  uint32_t GetNumParts() const;
  int GetPayloadLength() const;
  uint8_t NumEncoderVals() const;
  uint16_t GetContents() const;
  int GetNumContentTypes() const;
  // inline these two functions for small optimization, gprof indicates they
  // are called frequently so inlining them saves some call overhead
  inline uint16_t GetStartColumn() const;
  inline uint16_t GetEndColumn() const;

  std::vector<int64_t> GetEncoderValues() const;
  uint16_t GetLaserOnTime() const;
  uint16_t GetExposureTime() const;

  inline FragmentLayout GetFragmentLayout(DataType type) const;
  uint8_t *GetRawBytes(uint32_t *byte_len) const;

 private:
  std::map<DataType, FragmentLayout> fragment_layouts;
  DatagramHeader m_hdr;
  uint8_t *m_raw;
  uint32_t m_raw_len;
  int m_num_content_types;
  std::vector<int64_t> m_encoders;

  friend struct ProfileBuilder;
};

inline FragmentLayout DataPacket::GetFragmentLayout(DataType type) const
{
  auto iter = fragment_layouts.find(type);
  if (iter != fragment_layouts.end()) {
    return iter->second;
  }

  return FragmentLayout();
}

inline uint16_t DataPacket::GetStartColumn() const
{
  return m_hdr.start_column;
}

inline uint16_t DataPacket::GetEndColumn() const
{
  return m_hdr.end_column;
}
} // namespace joescan

#endif // JOESCAN_DATA_PACKET_H
