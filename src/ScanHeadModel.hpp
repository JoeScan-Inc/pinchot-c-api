/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_SCAN_HEAD_MODEL_H
#define JOESCAN_SCAN_HEAD_MODEL_H

#include <utility>
#include <vector>

#include "ScanHeadSpecification_generated.h"
#include "joescan_pinchot.h"

namespace joescan {

// forward declaration
class CameraLaserIterator;

class ScanHeadModel {
 public:
  ScanHeadModel(jsScanHeadType type, uint32_t serial_number, uint32_t id);

  jsCamera CameraPortToId(uint32_t port) const;
  int32_t CameraIdToPort(jsCamera camera) const;
  jsLaser LaserPortToId(uint32_t port) const;
  int32_t LaserIdToPort(jsLaser laser) const;
  jsCamera GetPairedCamera(jsLaser laser) const;
  jsLaser GetPairedLaser(jsCamera camera) const;
  uint32_t GetCameraLaserPairCount() const;

  bool IsCameraPrimary() const;
  bool IsLaserPrimary() const;
  bool IsPairValid(jsCamera camera, jsLaser laser) const;
  bool IsCameraValid(jsCamera camera) const;
  bool IsLaserValid(jsLaser laser) const;
  bool IsConfigurationValid(jsScanHeadConfiguration &cfg) const;

  jsScanHeadType GetType() const
  {
    return m_type;
  }

  uint32_t GetSerialNumber() const
  {
    return m_serial_number;
  }

  uint32_t GetId() const
  {
    return m_id;
  }

  uint32_t GetMaxConfigurationGroups() const
  {
    return m_specification.max_configuration_groups;
  }

  uint32_t GetMaxCameraRows() const
  {
    return m_specification.max_camera_rows;
  }

  uint32_t GetMaxCameraColumns() const
  {
    return m_specification.max_camera_columns;
  }
  
  uint32_t GetMaxScanPeriod() const
  {
    return m_specification.max_scan_period_us;
  }

  uint32_t GetMinScanPeriod() const
  {
    return m_specification.min_scan_period_us;
  }

  uint32_t GetNumberOfCameras() const
  {
    return m_specification.number_of_cameras;
  }

  uint32_t GetNumberOfLasers() const
  {
    return m_specification.number_of_lasers;
  }

  uint32_t GetCameraPortCableUpstream() const
  {
    return m_specification.camera_port_cable_upstream;
  }

 protected:
  friend class CameraLaserIterator;

  static const uint32_t kMaxAverageIntensity = 255;
  static const uint32_t kMaxSaturationPercentage = 100;
  static const uint32_t kMaxSaturationThreshold = 1023;
  static const uint32_t kMaxLaserDetectionThreshold = 1023;

  schema::client::ScanHeadSpecificationT m_specification;
  jsScanHeadType m_type;
  uint32_t m_serial_number;
  uint32_t m_id;
};
}
#endif
