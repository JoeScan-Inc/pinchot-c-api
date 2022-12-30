/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_PROFILE_BUILDER_H
#define JOESCAN_PROFILE_BUILDER_H

#include <cassert>
#include <memory>

#include "DataPacket.hpp"
#include "NetworkTypes.hpp"
#include "Point2D.hpp"
#include "joescan_pinchot.h"

namespace joescan {
struct ProfileBuilder {
  ProfileBuilder() : raw(nullptr), m_source(0), m_timestamp(0)
  {
  }

  ProfileBuilder(jsCamera camera, jsLaser laser, DataPacket& packet,
                 jsDataFormat format)
  {
    raw = std::make_shared<jsRawProfile>();
    raw->scan_head_id = packet.m_hdr.scan_head_id;
    raw->camera = camera;
    raw->laser = laser;
    raw->timestamp_ns = packet.m_hdr.timestamp_ns;
    raw->flags = packet.m_hdr.flags;
    raw->sequence_number = packet.m_hdr.sequence_number;
    raw->laser_on_time_us = packet.m_hdr.laser_on_time_us;
    raw->format = format;
    raw->data_len = JS_RAW_PROFILE_DATA_LEN;
    raw->data_valid_brightness = 0;
    raw->data_valid_xy = 0;
    raw->num_encoder_values = 0;

#ifdef _DEBUG
    assert(packet.m_encoders.size() < JS_ENCODER_MAX);
#endif

    for (uint32_t n = 0; n < packet.m_encoders.size(); n++) {
      raw->encoder_values[n] = packet.m_encoders[n];
      raw->num_encoder_values++;
    }

    for (uint32_t n = 0; n < JS_RAW_PROFILE_DATA_LEN; n++) {
      raw->data[n].x = JS_PROFILE_DATA_INVALID_XY;
      raw->data[n].y = JS_PROFILE_DATA_INVALID_XY;
      raw->data[n].brightness = JS_PROFILE_DATA_INVALID_BRIGHTNESS;
    }

    m_source = packet.GetSourceId();
    m_timestamp = raw->timestamp_ns;
  }

  inline void SetPacketInfo(uint32_t received, uint32_t expected)
  {
    raw->packets_received = received;
    raw->packets_expected = expected;
  }

  inline void InsertBrightness(uint32_t idx, uint8_t value)
  {
    raw->data[idx].brightness = static_cast<int32_t>(value);
    raw->data_valid_brightness++;
  }

  inline void InsertPoint(uint32_t idx, Point2D<int32_t> value)
  {
    raw->data[idx].x = value.x;
    raw->data[idx].y = value.y;
    raw->data_valid_xy++;
  }

  inline void InsertPointAndBrightness(uint32_t idx, Point2D<int32_t> point,
                                       uint8_t brightness)
  {
    raw->data[idx].x = point.x;
    raw->data[idx].y = point.y;
    raw->data[idx].brightness = brightness;
    raw->data_valid_xy++;
    raw->data_valid_brightness++;
  }

  inline bool IsEmpty()
  {
    return (nullptr == raw) ? true : false;
  }

  uint64_t Timestamp()
  {
    return m_timestamp;
  }

  uint32_t Source()
  {
    return m_source;
  }

  std::shared_ptr<jsRawProfile> raw;

 private:
  uint32_t m_source;
  uint64_t m_timestamp;
};
} // namespace joescan

#endif // JOESCAN_PROFILE_H
