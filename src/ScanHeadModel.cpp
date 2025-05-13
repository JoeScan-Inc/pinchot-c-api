/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "ScanHeadModel.hpp"
#include "js50_spec_bin.h"
#include <stdexcept>

using namespace joescan;

ScanHeadModel::ScanHeadModel(jsScanHeadType type,
                             uint32_t serial_number,
                             uint32_t id) :
  m_type(type),
  m_serial_number(serial_number),
  m_id(id)
{
  uint8_t *bin;
  uint32_t bin_len;

  switch (type) {
  case (JS_SCAN_HEAD_JS50WX):
    bin = (uint8_t *)js50wx_spec;
    bin_len = js50wx_spec_len;
    break;
  case (JS_SCAN_HEAD_JS50WSC):
    bin = (uint8_t *)js50wsc_spec;
    bin_len = js50wsc_spec_len;
    break;
  case (JS_SCAN_HEAD_JS50X6B20):
    bin = (uint8_t *)js50x6b20_spec;
    bin_len = js50x6b20_spec_len;
    break;
  case (JS_SCAN_HEAD_JS50X6B30):
    bin = (uint8_t *)js50x6b30_spec;
    bin_len = js50x6b30_spec_len;
    break;
  case (JS_SCAN_HEAD_JS50MX):
    bin = (uint8_t *)js50mx_spec;
    bin_len = js50mx_spec_len;
    break;
  case (JS_SCAN_HEAD_JS50Z820):
    bin = (uint8_t *)js50z820_spec;
    bin_len = js50z820_spec_len;
    break;
  case (JS_SCAN_HEAD_JS50Z830):
    bin = (uint8_t *)js50z830_spec;
    bin_len = js50z830_spec_len;
    break;
  default:
    throw std::runtime_error("invalid jsScanHeadType");
  }

  {
    using namespace schema::client;
    auto msg = GetScanHeadSpecification(bin);
    msg->UnPackTo(&m_specification);
  }
}

jsCamera ScanHeadModel::CameraPortToId(uint32_t port) const
{
  if (port >= m_specification.camera_port_to_id.size()) {
    return JS_CAMERA_INVALID;
  }

  return (jsCamera)m_specification.camera_port_to_id[port];
}

int32_t ScanHeadModel::CameraIdToPort(jsCamera camera) const
{
  uint32_t id = (uint32_t)camera;
  auto it = std::find(m_specification.camera_port_to_id.begin(),
                      m_specification.camera_port_to_id.end(), id);
  if (it == m_specification.camera_port_to_id.end()) {
    return -1;
  }

  // assumes that the position in the array indicates the port
  return (int32_t) (it - m_specification.camera_port_to_id.begin());
}

jsLaser ScanHeadModel::LaserPortToId(uint32_t port) const
{
  if (port >= m_specification.laser_port_to_id.size()) {
    return JS_LASER_INVALID;
  }

  return (jsLaser)m_specification.laser_port_to_id[port];
}

int32_t ScanHeadModel::LaserIdToPort(jsLaser laser) const
{
  auto it = std::find(m_specification.laser_port_to_id.begin(),
                      m_specification.laser_port_to_id.end(), laser);
  if (it == m_specification.laser_port_to_id.end()) {
    return -1;
  }

  // assumes that the position in the array indicates the port
  return (int32_t) (it - m_specification.laser_port_to_id.begin());
}

jsCamera ScanHeadModel::GetPairedCamera(jsLaser laser) const
{
  using namespace joescan::schema::client;
  if (ConfigurationGroupPrimary_CAMERA == 
      m_specification.configuration_group_primary) {
    return JS_CAMERA_INVALID;
  }

  if (false == IsLaserValid(laser)) {
    return JS_CAMERA_INVALID;
  }

  uint32_t laser_port = LaserIdToPort(laser);
  jsCamera camera = JS_CAMERA_INVALID;
  for (auto &grp : m_specification.configuration_groups) {
    if (grp.laser_port() == laser_port) {
      camera = CameraPortToId(grp.camera_port());
    }
  }

  return camera;
}

jsLaser ScanHeadModel::GetPairedLaser(jsCamera camera) const
{
  using namespace joescan::schema::client;
  if (ConfigurationGroupPrimary_LASER ==
      m_specification.configuration_group_primary) {
    return JS_LASER_INVALID;
  }

  if (false == IsCameraValid(camera)) {
    return JS_LASER_INVALID;
  }

  uint32_t camera_port = CameraIdToPort(camera);
  jsLaser laser = JS_LASER_INVALID;
  for (auto &grp : m_specification.configuration_groups) {
    if (grp.camera_port() == camera_port) {
      laser = LaserPortToId(grp.laser_port());
    }
  }

  return laser;
}

uint32_t ScanHeadModel::GetCameraLaserPairCount() const
{
  return (uint32_t) m_specification.configuration_groups.size();
}

bool ScanHeadModel::IsCameraPrimary() const
{
  return (joescan::schema::client::ConfigurationGroupPrimary_CAMERA ==
    m_specification.configuration_group_primary);
}

bool ScanHeadModel::IsLaserPrimary() const
{
  return (joescan::schema::client::ConfigurationGroupPrimary_LASER ==
    m_specification.configuration_group_primary);
}

bool ScanHeadModel::IsPairValid(jsCamera camera, jsLaser laser) const
{
  int32_t tmp = CameraIdToPort(camera);
  if (0 > tmp) {
    return false;
  }
  uint32_t camera_port = (uint32_t)tmp;

  tmp = LaserIdToPort(laser);
  if (0 > tmp) {
    return false;
  }
  uint32_t laser_port = (uint32_t)tmp;

  for (uint32_t n = 0; n < m_specification.configuration_groups.size(); n++) {
    uint32_t camera_cmp = m_specification.configuration_groups[n].camera_port();
    uint32_t laser_cmp = m_specification.configuration_groups[n].laser_port();

    if ((camera_port == camera_cmp) && (laser_port == laser_cmp)) {
      return true;
    }
  }

  return false;
}

bool ScanHeadModel::IsCameraValid(jsCamera camera) const
{
  if (JS_CAMERA_INVALID >= camera) {
    return false;
  }

  // subtract to account for valid cameras begining at 1
  uint32_t val = ((uint32_t)camera) - 1;
  return ((val < m_specification.number_of_cameras) ? true : false);
}

bool ScanHeadModel::IsLaserValid(jsLaser laser) const
{
  if (JS_LASER_INVALID >= laser) {
    return false;
  }

  // subtract to account for valid lasers begining at 1
  uint32_t val = ((uint32_t)laser) - 1;
  return ((val < m_specification.number_of_lasers) ? true : false);
}

bool ScanHeadModel::IsConfigurationValid(jsScanHeadConfiguration &cfg) const
{
  const uint32_t max_laser_on = m_specification.max_laser_on_time_us;
  const uint32_t min_laser_on = m_specification.min_laser_on_time_us;

  if ((cfg.laser_on_time_max_us > max_laser_on) ||
      (cfg.laser_on_time_min_us < min_laser_on) ||
      (cfg.laser_on_time_max_us < cfg.laser_on_time_def_us) ||
      (cfg.laser_on_time_max_us < cfg.laser_on_time_min_us) ||
      (cfg.laser_on_time_def_us < cfg.laser_on_time_min_us)) {
    return false;
  }

  if (cfg.laser_detection_threshold > kMaxLaserDetectionThreshold) {
    return false;
  }

  if (cfg.saturation_threshold > kMaxSaturationThreshold) {
    return false;
  }

  if (cfg.saturation_percentage > kMaxSaturationPercentage) {
    return false;
  }

  return true;
}
