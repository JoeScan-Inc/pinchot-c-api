/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "FlatbufferMessages.hpp"
#include "joescan_pinchot.h"
#include "scansync_network_defines.h"

using namespace joescan;

// compile time checks to ensure functionality is maintained

static_assert((jsScanHeadType)schema::ScanHeadType_JS50WX ==
               JS_SCAN_HEAD_JS50WX,
              "JS50WX");

static_assert((jsScanHeadType)schema::ScanHeadType_JS50WSC ==
               JS_SCAN_HEAD_JS50WSC,
               "JS50WSC");

static_assert((jsScanHeadType)schema::ScanHeadType_JS50X6B20 ==
               JS_SCAN_HEAD_JS50X6B20,
               "JS50X6B20");

static_assert((jsScanHeadType)schema::ScanHeadType_JS50X6B30 ==
               JS_SCAN_HEAD_JS50X6B30,
               "JS50X6B30");

static_assert((jsScanHeadType)schema::ScanHeadType_JS50MX ==
               JS_SCAN_HEAD_JS50MX,
               "JS50MX");

static_assert((jsScanHeadType)schema::ScanHeadType_JS50Z820 ==
               JS_SCAN_HEAD_JS50Z820,
               "JS50XZ820");

static_assert((jsScanHeadType)schema::ScanHeadType_JS50Z830 ==
               JS_SCAN_HEAD_JS50Z830,
               "JS50XZ830");

static_assert((jsScanHeadState)schema::server::ScanHeadState_INVALID ==
               JS_SCAN_HEAD_STATE_INVALID,
               "JS_SCAN_HEAD_STATE_INVALID");

static_assert((jsScanHeadState)schema::server::ScanHeadState_STANDBY ==
               JS_SCAN_HEAD_STATE_STANDBY,
               "JS_SCAN_HEAD_STATE_STANDBY");

static_assert((jsScanHeadState)schema::server::ScanHeadState_CONNECTED ==
               JS_SCAN_HEAD_STATE_CONNECTED,
               "JS_SCAN_HEAD_STATE_CONNECTED");

static_assert((jsScanHeadState)schema::server::ScanHeadState_SCANNING ==
               JS_SCAN_HEAD_STATE_SCANNING,
               "JS_SCAN_HEAD_STATE_SCANNING");

static_assert((jsScanHeadState)schema::server::ScanHeadState_SCANNING_IDLE ==
               JS_SCAN_HEAD_STATE_SCANNING_IDLE,
               "JS_SCAN_HEAD_STATE_SCANNING_IDLE");

static_assert(true == std::is_trivially_copyable<jsRawProfile>::value,
              "jsRawProfile not trivially copyable");

static_assert(1 == JS_CAMERA_A, "JS_CAMERA_A");
static_assert(2 == JS_CAMERA_B, "JS_CAMERA_B");

static_assert(1 == JS_LASER_1, "JS_LASER_1");
static_assert(2 == JS_LASER_2, "JS_LASER_2");
static_assert(3 == JS_LASER_3, "JS_LASER_3");
static_assert(4 == JS_LASER_4, "JS_LASER_4");
static_assert(5 == JS_LASER_5, "JS_LASER_5");
static_assert(6 == JS_LASER_6, "JS_LASER_6");
static_assert(7 == JS_LASER_7, "JS_LASER_7");
static_assert(8 == JS_LASER_8, "JS_LASER_8");

static_assert(sizeof(int32_t) == sizeof(jsUnits),
              "sizeof(jsUnits)");
static_assert(sizeof(int32_t) == sizeof(jsCableOrientation),
              "sizeof(jsCableOrientation)");
static_assert(sizeof(int32_t) == sizeof(jsScanHeadType),
              "sizeof(int32_t) == sizeof(jsScanHeadType)");
static_assert(sizeof(int32_t) == sizeof(jsCamera),
              "sizeof(int32_t) == sizeof(jsCamera)");
static_assert(sizeof(int32_t) == sizeof(jsLaser),
              "sizeof(int32_t) == sizeof(jsLaser)");
static_assert(sizeof(int32_t) == sizeof(jsEncoder),
              "sizeof(int32_t) == sizeof(jsEncoder)");
static_assert(sizeof(int32_t) == sizeof(jsDataFormat),
              "sizeof(int32_t) == sizeof(jsDataFormat)");

static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_A == FLAG_BIT_MASK_FAULT_A,
              "JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_A");
static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_B == FLAG_BIT_MASK_FAULT_B,
              "JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_B");
static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_Y == FLAG_BIT_MASK_FAULT_Y,
              "JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_Y");
static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_Z == FLAG_BIT_MASK_FAULT_Z,
              "JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_Z");
static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_OVERRUN == FLAG_BIT_MASK_OVERRUN,
              "JS_PROFILE_FLAG_ENCODER_MAIN_OVERRUN");
static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_TERMINATION_ENABLE ==
              FLAG_BIT_MASK_TERMINATION_ENABLE,
              "JS_PROFILE_FLAG_ENCODER_MAIN_TERMINATION_ENABLE");
static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_INDEX_Z == FLAG_BIT_MASK_INDEX_Z,
              "JS_PROFILE_FLAG_ENCODER_MAIN_INDEX_Z");
static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_SYNC == FLAG_BIT_MASK_SYNC,
              "JS_PROFILE_FLAG_ENCODER_MAIN_SYNC");
static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_AUX_Y == FLAG_BIT_MASK_AUX_Y,
              "JS_PROFILE_FLAG_ENCODER_MAIN_AUX_Y");
static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_SYNC ==
              FLAG_BIT_MASK_FAULT_SYNC,
              "JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_SYNC");
static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_LASER_DISABLE ==
              FLAG_BIT_MASK_LASER_DISABLE,
              "JS_PROFILE_FLAG_ENCODER_MAIN_LASER_DISABLE");
static_assert(JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_LASER_DISABLE ==
              FLAG_BIT_MASK_FAULT_LASER_DISABLE,
              "JS_PROFILE_FLAG_ENCODER_MAIN_FAULT_LASER_DISABLE");
