/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "AlignmentParams.hpp"
#include <cmath>
#include <stdexcept>

using namespace joescan;

AlignmentParams::AlignmentParams(
  double camera_to_mill_scale,
  double roll,
  double shift_x,
  double shift_y,
  jsCableOrientation cable)
{
  m_transform.camera_to_mill_scale = camera_to_mill_scale;
  m_alignment.cable = cable;
  m_alignment.roll = roll;
  m_alignment.shift_x = shift_x;
  m_alignment.shift_y = shift_y;

  CalculateTransform();
}

int AlignmentParams::SetRollAndOffset(
  double roll,
  double shift_x,
  double shift_y)
{
  m_alignment.roll = roll;
  m_alignment.shift_x = shift_x;
  m_alignment.shift_y = shift_y;
  CalculateTransform();
  return 0;
}

int AlignmentParams::SetCableOrientation(jsCableOrientation cable)
{
  if ((JS_CABLE_ORIENTATION_DOWNSTREAM != cable) &&
      (JS_CABLE_ORIENTATION_UPSTREAM != cable)) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  m_alignment.cable = cable;
  CalculateTransform();
  return 0;
}

const Alignment* AlignmentParams::GetAlignment() const
{
  return &m_alignment;
}

const Transform* AlignmentParams::GetTransform() const
{
  return &m_transform;
}

void AlignmentParams::CalculateTransform()
{
  const double rho = (std::atan(1) * 4) / 180.0; // pi / 180
  const double yaw =
    (JS_CABLE_ORIENTATION_DOWNSTREAM == m_alignment.cable) ? 0.0 : 180.0;
  const double sin_roll = std::sin(m_alignment.roll * rho);
  const double cos_roll = std::cos(m_alignment.roll * rho);
  const double cos_yaw = std::cos(yaw * rho);
  const double sin_neg_roll = std::sin(-1.0 * m_alignment.roll * rho);
  const double cos_neg_roll = std::cos(-1.0 * m_alignment.roll * rho);
  const double cos_neg_yaw = std::cos(-1.0 * yaw * rho);

  Transform& t = m_transform;

  t.shift_x_1000 = m_alignment.shift_x * 1000.0;
  t.shift_y_1000 = m_alignment.shift_y * 1000.0;

  t.camera_to_mill_xx = cos_yaw * cos_roll * t.camera_to_mill_scale;
  t.camera_to_mill_xy = -sin_roll * t.camera_to_mill_scale;
  t.camera_to_mill_yx = cos_yaw * sin_roll * t.camera_to_mill_scale;
  t.camera_to_mill_yy = cos_roll * t.camera_to_mill_scale;
  t.mill_to_camera_xx = cos_neg_yaw * cos_neg_roll / t.camera_to_mill_scale;
  t.mill_to_camera_xy = cos_neg_yaw * -sin_neg_roll / t.camera_to_mill_scale;
  t.mill_to_camera_yx = sin_neg_roll / t.camera_to_mill_scale;
  t.mill_to_camera_yy = cos_neg_roll / t.camera_to_mill_scale;

}
