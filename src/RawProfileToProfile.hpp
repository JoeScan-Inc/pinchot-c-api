/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_RAW_PROFILE_TO_PROFILE_H
#define JOESCAN_RAW_PROFILE_TO_PROFILE_H

#include "joescan_pinchot.h"

namespace joescan {

/// Copies the contents in a `jsRawProfile` and converts them into a `jsProfile`
inline void RawProfileToProfile(jsRawProfile *src, jsProfile *dst)
{
  dst->scan_head_id = src->scan_head_id;
  dst->camera = src->camera;
  dst->laser = src->laser;
  dst->timestamp_ns = src->timestamp_ns;
  dst->flags = src->flags;
  dst->sequence_number = src->sequence_number;
  dst->laser_on_time_us = src->laser_on_time_us;
  dst->format = src->format;
  dst->packets_received = src->packets_received;
  dst->packets_expected = src->packets_expected;
  dst->num_encoder_values = src->num_encoder_values;
  // Note: encoder values beyond `num_encoder_values` should be set to 
  // `JS_SCANSYNC_INVALID_ENCODER` in the raw profile
  memcpy(dst->encoder_values, src->encoder_values,
         JS_ENCODER_MAX * sizeof(uint64_t));

  unsigned int stride = 0;
  unsigned int len = 0;

  switch (dst->format) {
    case JS_DATA_FORMAT_XY_BRIGHTNESS_FULL:
    case JS_DATA_FORMAT_XY_FULL:
      stride = 1;
      break;
    case JS_DATA_FORMAT_XY_BRIGHTNESS_HALF:
    case JS_DATA_FORMAT_XY_HALF:
      stride = 2;
      break;
    case JS_DATA_FORMAT_XY_BRIGHTNESS_QUARTER:
    case JS_DATA_FORMAT_XY_QUARTER:
      stride = 4;
      break;
    case JS_DATA_FORMAT_INVALID:
    default:
      stride = 0;
      assert(0 != stride);
      break;
  }

  for (unsigned int i = 0; i < src->data_len; i += stride) {
    if ((JS_PROFILE_DATA_INVALID_XY != src->data[i].x) ||
        (JS_PROFILE_DATA_INVALID_XY != src->data[i].y)) {
      // Note: Only need to check X/Y since we only support data types with
      // X/Y coordinates alone or X/Y coordinates with brightness.
      dst->data[len++] = src->data[i];
    }
  }
  dst->data_len = len;
}

}

#endif
