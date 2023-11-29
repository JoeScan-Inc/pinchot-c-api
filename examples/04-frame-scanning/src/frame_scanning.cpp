/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

/**
 * @file frame_scanning.cpp
 * @brief
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "joescan_pinchot.h"

#ifdef _WIN32
#include "Windows.h"
#include "processthreadsapi.h"
#endif

static std::atomic<bool> _is_scanning(false);
static std::atomic<uint32_t> _frame_count(0);
static std::atomic<uint32_t> _profile_count(0);
static std::atomic<uint32_t> _invalid_count(0);

class ApiError : public std::runtime_error {
 private:
  jsError m_return_code;

 public:
  ApiError(const char* what, int32_t return_code) : std::runtime_error(what)
  {
    if ((0 < return_code) || (JS_ERROR_UNKNOWN > m_return_code)) {
      m_return_code = JS_ERROR_UNKNOWN;
    } else {
      m_return_code = (jsError) return_code;
    }
  }

  jsError return_code() const
  {
    return m_return_code;
  }
};

/**
 * @brief Initializes and configures scan heads.
 *
 * @param scan_system Reference to the scan system.
 * @param scan_heads Reference to vector to be filled with created scan heads.
 * @param serial_numbers Reference to vector of serial numbers of scan heads
 * to initialize.
 */
void initialize_scan_heads(jsScanSystem &scan_system,
                           std::vector<jsScanHead> &scan_heads,
                           std::vector<uint32_t> &serial_numbers)
{
  int32_t r = 0;

  jsScanHeadConfiguration config;
  config.camera_exposure_time_min_us = 10000;
  config.camera_exposure_time_def_us = 47000;
  config.camera_exposure_time_max_us = 900000;
  config.laser_on_time_min_us = 100;
  config.laser_on_time_def_us = 100;
  config.laser_on_time_max_us = 1000;
  config.laser_detection_threshold = 120;
  config.saturation_threshold = 800;
  config.saturation_percentage = 30;

  // Create a scan head for each serial number passed in on the command line
  // and configure each one with the same parameters. Note that there is
  // nothing stopping users from configuring each scan head independently.
  for (unsigned int i = 0; i < serial_numbers.size(); i++) {
    uint32_t serial = serial_numbers[i];
    auto scan_head = jsScanSystemCreateScanHead(scan_system, serial, i);
    if (0 > scan_head) {
      throw ApiError("failed to create scan head", (int32_t) scan_head);
    }
    scan_heads.push_back(scan_head);

    uint32_t major, minor, patch;
    r = jsScanHeadGetFirmwareVersion(scan_head, &major, &minor, &patch);
    if (0 > r) {
      throw ApiError("failed to read firmware version", r);
    }

    std::cout << serial << " v" << major << "." << minor << "." << patch
              << std::endl;

    r = jsScanHeadSetConfiguration(scan_head, &config);
    if (0 > r) {
      throw ApiError("failed to set scan head configuration", r);
    }

    r = jsScanHeadSetWindowRectangular(scan_head, 30.0, -30.0, -30.0, 30.0);
    if (0 > r) {
      throw ApiError("failed to set window", r);
    }

    r = jsScanHeadSetAlignment(scan_head, 0.0, 0.0, 0.0);
    if (0 > r) {
      throw ApiError("failed to set alignment", r);
    }

    r = jsScanHeadSetCableOrientation(scan_head, JS_CABLE_ORIENTATION_UPSTREAM);
    if (0 > r) {
      throw ApiError("failed to set cable orientation", r);
    }
  }
}

/**
 * @brief Creates a basic phase table using all the scan heads managed by the
 * scan system.
 *
 * @param scan_system Reference to the scan system.
 * @param scan_heads Reference to vector of all created scan heads.
 */
void initialize_phase_table(jsScanSystem &scan_system,
                            std::vector<jsScanHead> &scan_heads)
{
  int32_t r = 0;

  // Assume that the system is comprised of scan heads that are all the same.
  jsScanHeadType type = jsScanHeadGetType(scan_heads[0]);

  // For this example we will create a phase table that interleaves lasers
  // seen by Camera A and Camera B. This allows fast and efficient scanning
  // by allowing one camera to scan while the other has the scan data read out
  // & processed; if the same camera is used back to back, a time penalty
  // will be incurred to ensure scan data isn't overwritten.
  switch (type) {
  case (JS_SCAN_HEAD_JS50X6B20):
  case (JS_SCAN_HEAD_JS50X6B30):
    // Phase | Laser | Camera
    //   1   |   1   |   B
    //   2   |   4   |   A
    //   3   |   2   |   B
    //   4   |   5   |   A
    //   5   |   3   |   B
    //   6   |   6   |   A

    for (int n = 0; n < 3; n++) {
      jsLaser laser = JS_LASER_INVALID;

      // Lasers associated with Camera B
      r = jsScanSystemPhaseCreate(scan_system);
      if (0 != r) {
        throw ApiError("failed to create phase", r);
      }

      laser = (jsLaser) (JS_LASER_1 + n);
      for (auto scan_head : scan_heads) {
        r = jsScanSystemPhaseInsertLaser(scan_system, scan_head, laser);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }
      }

      // Lasers associated with Camera A
      r = jsScanSystemPhaseCreate(scan_system);
      if (0 != r) {
        throw ApiError("failed to create phase", r);
      }

      laser = (jsLaser) (JS_LASER_4 + n);
      for (auto scan_head : scan_heads) {
        r = jsScanSystemPhaseInsertLaser(scan_system, scan_head, laser);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }
      }
    }
    break;

  case (JS_SCAN_HEAD_JS50Z820):
  case (JS_SCAN_HEAD_JS50Z830):
    // Phase | Laser | Camera
    //   1   |   1   |   B
    //   2   |   5   |   A
    //   3   |   2   |   B
    //   4   |   6   |   A
    //   5   |   3   |   B
    //   6   |   7   |   A
    //   7   |   4   |   B
    //   8   |   8   |   A

    for (int n = 0; n < 4; n++) {
      jsLaser laser = JS_LASER_INVALID;

      // Lasers associated with Camera B
      r = jsScanSystemPhaseCreate(scan_system);
      if (0 != r) {
        throw ApiError("failed to create phase", r);
      }

      laser = (jsLaser) (JS_LASER_1 + n);
      for (auto scan_head : scan_heads) {
        r = jsScanSystemPhaseInsertLaser(scan_system, scan_head, laser);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }
      }

      // Lasers associated with Camera A
      r = jsScanSystemPhaseCreate(scan_system);
      if (0 != r) {
        throw ApiError("failed to create phase", r);
      }

      laser = (jsLaser) (JS_LASER_5 + n);
      for (auto scan_head : scan_heads) {
        r = jsScanSystemPhaseInsertLaser(scan_system, scan_head, laser);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }
      }
    }
    break;

  case (JS_SCAN_HEAD_JS50WSC):
  case (JS_SCAN_HEAD_JS50MX):
    // Phase | Laser | Camera
    //   1   |   1   |   A

    r = jsScanSystemPhaseCreate(scan_system);
    if (0 != r) {
      throw ApiError("failed to create phase", r);
    }

    for (auto scan_head : scan_heads) {
      r = jsScanSystemPhaseInsertCamera(scan_system, scan_head, JS_CAMERA_A);
      if (0 != r) {
        throw ApiError("failed to insert into phase", r);
      }
    }
    break;

  case (JS_SCAN_HEAD_JS50WX):
    // Phase | Laser | Camera
    //   1   |   1   |   A
    //   2   |   1   |   B

    r = jsScanSystemPhaseCreate(scan_system);
    if (0 != r) {
      throw ApiError("failed to create phase", r);
    }

    for (auto scan_head : scan_heads) {
      r = jsScanSystemPhaseInsertCamera(scan_system, scan_head, JS_CAMERA_A);
      if (0 != r) {
        throw ApiError("failed to insert into phase", r);
      }
    }

    r = jsScanSystemPhaseCreate(scan_system);
    if (0 != r) {
      throw ApiError("failed to create phase", r);
    }

    for (auto scan_head : scan_heads) {
      r = jsScanSystemPhaseInsertCamera(scan_system, scan_head, JS_CAMERA_B);
      if (0 != r) {
        throw ApiError("failed to insert into phase", r);
      }
    }
    break;

  case (JS_SCAN_HEAD_INVALID_TYPE):
  default:
    throw ApiError("invalid scan head type", 0);
  }
}

/**
 * @brief This function receives profile data from the scan system as a scan
 * frame. The frame will contain profile data from all the scan heads.
 *
 * @param scan_system Reference to the scan sysetm to recieve profile data from.
 */
static void receiver(jsScanSystem scan_system,
                     std::vector<uint32_t> serial_numbers)
{
  // For applications with heavy CPU load, it is advised to boost the priority
  // of the thread reading out the frame data. If the thread reading out the
  // scan data falls behind, data will be dropped, causing problems later on
  // when trying to analyze what was scanned.
#ifdef _WIN32
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
  jsRawProfile* profiles = nullptr;

  try {
    int32_t r = jsScanSystemGetProfilesPerFrame(scan_system);
    if (0 >= r) {
      throw ApiError("failed to read frame size", r);
    }

    uint32_t profiles_per_frame = uint32_t(r);
    profiles = new jsRawProfile[uint32_t(profiles_per_frame)];

    while (_is_scanning) {
      r = jsScanSystemWaitUntilFrameAvailable(scan_system, 1000000);
      if (0 == r) {
        continue;
      }
      else if (0 > r) {
        throw ApiError("failed to wait for frame", r);
      }

      r = jsScanSystemGetRawFrame(scan_system, profiles);
      if (0 >= r) {
        throw ApiError("failed to read frame", r);
      }

      _profile_count += r;
      _frame_count += 1;
      uint32_t valid_count = 0;
      for (uint32_t n = 0; n < profiles_per_frame; n++) {
        if (jsRawProfileIsValid(profiles[n])) {
          valid_count++;
        }
        else {
          _invalid_count++;
          std::cout << "Invalid: " << profiles[n].sequence_number << " "
            << serial_numbers[profiles[n].scan_head_id];
          if (JS_CAMERA_A == profiles[n].camera) {
            std::cout << ".A.";
          }
          else {
            std::cout << ".B.";
          }
          std::cout << profiles[n].laser << std::endl;
        }
      }
      if (valid_count != profiles_per_frame) {
        std::cout << "received " << valid_count << " of " << profiles_per_frame
          << std::endl;
      }
    }
  }
  catch (ApiError& e) {
    std::cout << "ERROR: " << e.what() << std::endl;
    const char* err_str = nullptr;
    jsError err = e.return_code();
    if (JS_ERROR_NONE != err) {
      jsGetError(err, &err_str);
      std::cout << "jsError (" << err << "): " << err_str << std::endl;
    }
  }

  if (nullptr != profiles) {
    delete[] profiles;
  }
}

int main(int argc, char *argv[])
{
  jsScanSystem scan_system;
  std::vector<jsScanHead> scan_heads;
  std::vector<uint32_t> serial_numbers;
  std::thread thread;
  int32_t r = 0;

  if (2 > argc) {
    std::cout << "Usage: " << argv[0] << " SERIAL..." << std::endl;
    return 1;
  }

  // Grab the serial number(s) passed in through the command line.
  for (int i = 1; i < argc; i++) {
    serial_numbers.emplace_back(strtoul(argv[i], NULL, 0));
  }

  {
    const char *version_str;
    jsGetAPIVersion(&version_str);
    std::cout << "joescanapi " << version_str << std::endl;
  }

  try {
    scan_system = jsScanSystemCreate(JS_UNITS_INCHES);
    if (0 > scan_system) {
      throw ApiError("failed to create scan system", (int32_t) scan_system);
    }

    // Initialize & configure all the scan heads.
    initialize_scan_heads(scan_system, scan_heads, serial_numbers);

    r = jsScanSystemConnect(scan_system, 10);
    if (0 > r) {
      throw ApiError("failed to connect", r);
    } else if (jsScanSystemGetNumberScanHeads(scan_system) != r) {
      for (auto scan_head : scan_heads) {
        if (false == jsScanHeadIsConnected(scan_head)) {
          uint32_t serial = jsScanHeadGetSerial(scan_head);
          std::cout << serial << " is NOT connected" << std::endl;
        }
      }
      throw ApiError("failed to connect to all scan heads", 0);
    }

    initialize_phase_table(scan_system, scan_heads);

    int32_t min_period_us = jsScanSystemGetMinScanPeriod(scan_system);
    if (0 >= min_period_us) {
      throw ApiError("failed to read min scan period", min_period_us);
    }
    std::cout << "min scan period is " << min_period_us << " us" << std::endl;


    std::cout << "start scanning" << std::endl;
    jsDataFormat data_format = JS_DATA_FORMAT_XY_BRIGHTNESS_FULL;
    r = jsScanSystemStartFrameScanning(scan_system,
                                       min_period_us,
                                       data_format);
    if (0 > r) {
      throw ApiError("failed to start scanning", r);
    }

    _is_scanning = true;
    thread = std::thread(receiver, scan_system, serial_numbers);

    // Put this thread to sleep until the total scan time is done.
    unsigned long int scan_time_sec = 10;
    std::this_thread::sleep_for(std::chrono::seconds(scan_time_sec));
  } catch (ApiError &e) {
    std::cout << "ERROR: " << e.what() << std::endl;
    r = 1;

    const char *err_str = nullptr;
    jsError err = e.return_code();
    if (JS_ERROR_NONE != err) {
      jsGetError(err, &err_str);
      std::cout << "jsError (" << err << "): " << err_str << std::endl;
    }
  }

  _is_scanning = false;
  if (thread.joinable()) {
    thread.join();
  }

  std::cout << "stop scanning" << std::endl;
  r = jsScanSystemStopScanning(scan_system);
  if (0 > r) {
    std::cout << "ERROR: failed to stop scanning" << std::endl;
  }

  std::cout << "read " << _frame_count << " frames (" << _profile_count
            << " profiles, " << _invalid_count << " invalid)" << std::endl;

  r = jsScanSystemDisconnect(scan_system);
  if (0 > r) {
    std::cout << "ERROR: failed to disconnect" << std::endl;
  }

  jsScanSystemFree(scan_system);

  return (0 == r) ? 0 : 1;
}
