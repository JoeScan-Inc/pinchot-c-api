/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

/**
 * @file jsScanApplication.h
 * @author JoeScan
 * @brief This file contains a simple C++ class that wraps the Pinchot C API.
 * It is primarily provided for illustrative purposes and to reduce boilerplate
 * code in debug applications.
 */

#ifndef _JS_SCAN_APP_HPP
#define _JS_SCAN_APP_HPP

#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "joescan_pinchot.h"

namespace joescan {

class ApiError : public std::runtime_error {
 private:
  jsError m_return_code;

 public:
  ApiError(const char* what, int32_t return_code) : std::runtime_error(what)
  {
    if ((0 < return_code) || (JS_ERROR_UNKNOWN > return_code)) {
      m_return_code = JS_ERROR_UNKNOWN;
    } else {
      m_return_code = (jsError) return_code;
    }
  }

  jsError return_code() const
  {
    return m_return_code;
  }

  void print() const
  {
    std::cout << "ERROR: " << this->what() << std::endl;

    const char *err_str = nullptr;
    jsError err = m_return_code;

    if (JS_ERROR_NONE != err) {
      jsGetError(err, &err_str);
      std::cout << "jsError (" << err << "): " << err_str << std::endl;
    }
  }
};


class ScanApplication {
 public:
  ScanApplication() : m_is_phase_table_set(false)
  {
    m_threads = nullptr;

    const char *version_str;
    jsGetAPIVersion(&version_str);
    std::cout << "joescanapi " << version_str << std::endl;

    m_scan_system = jsScanSystemCreate(JS_UNITS_INCHES);
    if (0 > m_scan_system) {
      throw ApiError("failed to create scan system", m_scan_system);
    }

    m_config.camera_exposure_time_min_us = 10000;
    m_config.camera_exposure_time_def_us = 47000;
    m_config.camera_exposure_time_max_us = 900000;
    m_config.laser_on_time_min_us = 1000;
    m_config.laser_on_time_def_us = 1000;
    m_config.laser_on_time_max_us = 1000;
    m_config.laser_detection_threshold = 1;
    m_config.saturation_threshold = 800;
    m_config.saturation_percentage = 30;

    m_top = 40;
    m_bottom = -40;
    m_left = -40;
    m_right = 40;
  }

  ~ScanApplication()
  {
    jsScanSystemFree(m_scan_system);
  }
  
  void SetSerialNumber(std::vector<uint32_t> &serial_numbers)
  {
    m_serial_numbers = serial_numbers;
  }

  void SetSerialNumber(uint32_t serial_number)
  {
    m_serial_numbers.push_back(serial_number);
  }

  void SetLaserOn(uint32_t def_us, uint32_t min_us=0, uint32_t max_us=0)
  {
    if (0 == min_us) {
      min_us = def_us;
    }

    if (0 == max_us) {
      max_us = def_us;
    }

    m_config.laser_on_time_def_us = def_us;
    m_config.laser_on_time_min_us = def_us;
    m_config.laser_on_time_max_us = def_us;
  }

  void SetWindow(double top, double bottom, double left, double right)
  {
    m_top = top;
    m_bottom = bottom;
    m_left = left;
    m_right = right;
  }

  void SetThreshold(uint32_t threshold)
  {
    m_config.laser_detection_threshold = threshold;
  }

  void Configure()
  {
    int32_t r = 0;

    // Create scan head for each serial number passed in on the command line.
    for (uint32_t i = 0; i < m_serial_numbers.size(); i++) {
      uint32_t serial = m_serial_numbers[i];
      auto scan_head = jsScanSystemCreateScanHead(m_scan_system, serial, i);
      if (0 > scan_head) {
        std::string e = "failed to create scan head " + std::to_string(serial);
        throw ApiError(e.c_str(), scan_head);
      }
      m_scan_heads.push_back(scan_head);

      // We'll use the same configuration here for each scan head.
      r = jsScanHeadSetConfiguration(scan_head, &m_config);
      if (0 > r) {
        throw ApiError("failed to configure scan head", r);
      }

      r = jsScanHeadSetWindowRectangular(scan_head,
                                         m_top,
                                         m_bottom,
                                         m_left,
                                         m_right);
      if (0 != r) {
        throw ApiError("failed to set scan window", r);
      }

      r = jsScanHeadSetAlignment(scan_head, 0.0, 0.0, 0.0);
      if (0 != r) {
        throw ApiError("failed to set alignment", r);
      }

      r = jsScanHeadSetCableOrientation(scan_head,
                                        JS_CABLE_ORIENTATION_UPSTREAM);
      if (0 != r) {
        throw ApiError("failed to set cable orientation", r);
      }
    }

    if (!m_is_phase_table_set) {
      ConfigureDistinctElementPhaseTable();
    }
  }

  void Connect()
  {
    int32_t r = jsScanSystemConnect(m_scan_system, 10);
    if (0 > r) {
      // This error condition indicates that something wrong happened during
      // the connection process itself and should be understood by extension
      // that none of the scan heads are connected.
      throw ApiError("failed to connect", r);
    } else if (jsScanSystemGetNumberScanHeads(m_scan_system) != r) {
      // On this error condition, connection was successful to some of the scan
      // heads in the system. We can query the scan heads to determine which
      // one successfully connected and which ones failed.
      for (auto scan_head : m_scan_heads) {
        if (false == jsScanHeadIsConnected(scan_head)) {
          uint32_t serial = jsScanHeadGetSerial(scan_head);
          std::cout << serial << " is NOT connected" << std::endl;
        }
      }
      throw ApiError("failed to connect to all scan heads", 0);
    }
  }

  void StartScanning(uint32_t period_us=0,
                     jsDataFormat fmt=JS_DATA_FORMAT_XY_BRIGHTNESS_FULL,
                     std::function<void(jsScanHead)> func=nullptr)
  {
    int32_t min_period_us = jsScanSystemGetMinScanPeriod(m_scan_system);
    if (0 >= min_period_us) {
      throw ApiError("failed to read min scan period", min_period_us);
    }
    std::cout << "min scan period is " << min_period_us << " us" << std::endl;
    
    if (0 == period_us) {
      period_us = min_period_us;
    }
    std::cout << "scan period is " << period_us << std::endl;

    std::cout << "start scanning" << std::endl;
    int32_t r = jsScanSystemStartScanning(m_scan_system, period_us, fmt);
    if (0 > r) {
      throw ApiError("failed to start scanning", r);
    }

    if (nullptr != func) {
      m_threads = new std::thread[m_scan_heads.size()];
      for (unsigned long i = 0; i < m_scan_heads.size(); i++) {
        m_threads[i] = std::thread(func, m_scan_heads[i]);
      }
    }
  }

  void StopScanning()
  {
    std::cout << "stop scanning" << std::endl;
    int32_t r = jsScanSystemStopScanning(m_scan_system);
    if (0 > r) {
      throw ApiError("failed to stop scanning", r);
    }

    if (nullptr != m_threads) {
      for (unsigned long i = 0; i < m_scan_heads.size(); i++) {
        m_threads[i].join();
      }
      delete[] m_threads;
    }
  }

  void Disconnect()
  {
    std::cout << "disconnect" << std::endl;
    int32_t r = jsScanSystemDisconnect(m_scan_system);
    if (0 > r) {
      throw ApiError("failed to disconnect", r);
    }
  }

  std::vector<jsScanHead>& GetScanHeads()
  {
    return m_scan_heads;
  }

  void ConfigureDistinctElementPhaseTable()
  {
    int32_t r = 0;

    // Assume that the system is comprised of scan heads that are all the same.
    jsScanHeadType type = jsScanHeadGetType(m_scan_heads[0]);

    r = jsScanSystemPhaseClearAll(m_scan_system);
    if (0 != r) {
      throw ApiError("failed to clear phase table", r);
    }

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

      for (auto scan_head : m_scan_heads) {
        r = jsScanSystemPhaseCreate(m_scan_system);
        if (0 != r) {
          throw ApiError("failed to create phase", r);
        }

        r = jsScanSystemPhaseInsertLaser(m_scan_system, scan_head,
                                          JS_LASER_1);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }

        r = jsScanSystemPhaseCreate(m_scan_system);
        if (0 != r) {
          throw ApiError("failed to create phase", r);
        }

        r = jsScanSystemPhaseInsertLaser(m_scan_system, scan_head,
                                          JS_LASER_4);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }

        r = jsScanSystemPhaseCreate(m_scan_system);
        if (0 != r) {
          throw ApiError("failed to create phase", r);
        }

        r = jsScanSystemPhaseInsertLaser(m_scan_system, scan_head,
                                          JS_LASER_2);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }

        r = jsScanSystemPhaseCreate(m_scan_system);
        if (0 != r) {
          throw ApiError("failed to create phase", r);
        }

        r = jsScanSystemPhaseInsertLaser(m_scan_system, scan_head,
                                          JS_LASER_5);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }

        r = jsScanSystemPhaseCreate(m_scan_system);
        if (0 != r) {
          throw ApiError("failed to create phase", r);
        }

        r = jsScanSystemPhaseInsertLaser(m_scan_system, scan_head,
                                          JS_LASER_3);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }

        r = jsScanSystemPhaseCreate(m_scan_system);
        if (0 != r) {
          throw ApiError("failed to create phase", r);
        }

        r = jsScanSystemPhaseInsertLaser(m_scan_system, scan_head,
                                          JS_LASER_6);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }
      }
      break;

    case (JS_SCAN_HEAD_JS50WSC):
    case (JS_SCAN_HEAD_JS50MX):
      for (auto scan_head : m_scan_heads) {
        r = jsScanSystemPhaseCreate(m_scan_system);
        if (0 != r) {
          throw ApiError("failed to create phase", r);
        }

        r = jsScanSystemPhaseInsertCamera(m_scan_system, scan_head,
                                          JS_CAMERA_A);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }
      }
      break;

    case (JS_SCAN_HEAD_JS50WX):
      for (auto scan_head : m_scan_heads) {
        r = jsScanSystemPhaseCreate(m_scan_system);
        if (0 != r) {
          throw ApiError("failed to create phase", r);
        }

        r = jsScanSystemPhaseInsertCamera(m_scan_system, scan_head,
                                          JS_CAMERA_A);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }

        r = jsScanSystemPhaseCreate(m_scan_system);
        if (0 != r) {
          throw ApiError("failed to create phase", r);
        }

        r = jsScanSystemPhaseInsertCamera(m_scan_system, scan_head,
                                          JS_CAMERA_B);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }
      }
      break;

    case (JS_SCAN_HEAD_INVALID_TYPE):
    default:
      throw ApiError("invalid scan head type", 0);
    }

    m_is_phase_table_set = true;
  }

  void ConfigureGenericPhaseTable()
  {
    int32_t r = 0;

    // Assume that the system is comprised of scan heads that are all the same.
    jsScanHeadType type = jsScanHeadGetType(m_scan_heads[0]);

    r = jsScanSystemPhaseClearAll(m_scan_system);
    if (0 != r) {
      throw ApiError("failed to clear phase table", r);
    }

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
        r = jsScanSystemPhaseCreate(m_scan_system);
        if (0 != r) {
          throw ApiError("failed to create phase", r);
        }

        laser = (jsLaser) (JS_LASER_1 + n);
        for (auto scan_head : m_scan_heads) {
          r = jsScanSystemPhaseInsertLaser(m_scan_system, scan_head, laser);
          if (0 != r) {
            throw ApiError("failed to insert into phase", r);
          }
        }

        // Lasers associated with Camera A
        r = jsScanSystemPhaseCreate(m_scan_system);
        if (0 != r) {
          throw ApiError("failed to create phase", r);
        }

        laser = (jsLaser) (JS_LASER_4 + n);
        for (auto scan_head : m_scan_heads) {
          r = jsScanSystemPhaseInsertLaser(m_scan_system, scan_head, laser);
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

      r = jsScanSystemPhaseCreate(m_scan_system);
      if (0 != r) {
        throw ApiError("failed to create phase", r);
      }

      for (auto scan_head : m_scan_heads) {
        r = jsScanSystemPhaseInsertCamera(m_scan_system, scan_head,
                                          JS_CAMERA_A);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }
      }
      break;

    case (JS_SCAN_HEAD_JS50WX):
      // Phase | Laser | Camera
      //   1   |   1   |   A
      //   2   |   1   |   B

      r = jsScanSystemPhaseCreate(m_scan_system);
      if (0 != r) {
        throw ApiError("failed to create phase", r);
      }

      for (auto scan_head : m_scan_heads) {
        r = jsScanSystemPhaseInsertCamera(m_scan_system, scan_head,
                                          JS_CAMERA_A);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }
      }

      r = jsScanSystemPhaseCreate(m_scan_system);
      if (0 != r) {
        throw ApiError("failed to create phase", r);
      }

      for (auto scan_head : m_scan_heads) {
        r = jsScanSystemPhaseInsertCamera(m_scan_system, scan_head,
                                          JS_CAMERA_B);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }
      }
      break;

    case (JS_SCAN_HEAD_INVALID_TYPE):
    default:
      throw ApiError("invalid scan head type", 0);
    }

    m_is_phase_table_set = true;
  }

 private:
  std::thread *m_threads;
  std::vector<uint32_t> m_serial_numbers;
  std::vector<jsScanHead> m_scan_heads;
  jsScanSystem m_scan_system;
  jsScanHeadConfiguration m_config;
  bool m_is_phase_table_set;
  double m_top;
  double m_bottom;
  double m_left;
  double m_right;
};

}

#endif
