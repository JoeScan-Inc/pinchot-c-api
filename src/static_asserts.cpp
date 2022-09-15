/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "ScanHeadType_generated.h"
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

