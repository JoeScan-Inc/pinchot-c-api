/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "FlatbufferMessages.hpp"
#include "joescan_pinchot.h"

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

static_assert((jsScanHeadState)schema::server::ScanHeadState_IDLE ==
               JS_SCAN_HEAD_STATE_IDLE,
               "JS_SCAN_HEAD_STATE_IDLE");

static_assert((jsScanHeadState)schema::server::ScanHeadState_CONNECTED ==
               JS_SCAN_HEAD_STATE_CONNECTED,
               "JS_SCAN_HEAD_STATE_CONNECTED");

static_assert((jsScanHeadState)schema::server::ScanHeadState_SCANNING ==
               JS_SCAN_HEAD_STATE_SCANNING,
               "JS_SCAN_HEAD_STATE_SCANNING");

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
