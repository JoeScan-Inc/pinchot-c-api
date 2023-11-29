/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_ALIGNMENT_PARAMS_H
#define JOESCAN_ALIGNMENT_PARAMS_H

#include "Point2D.hpp"
#include "joescan_pinchot.h"

#include <cmath>

namespace joescan {

struct Alignment {
  jsCableOrientation cable = JS_CABLE_ORIENTATION_UPSTREAM;
  double roll = 0.0;
  double shift_x = 0.0;
  double shift_y = 0.0;
};

struct Transform {
  inline Point2D<int32_t> CameraToMill(Point2D<int32_t> p) const;
  inline Point2D<int32_t> CameraToMill(int32_t x, int32_t y) const;
  inline Point2D<int32_t> MillToCamera(Point2D<int32_t> p) const;
  inline Point2D<int32_t> MillToCamera(int32_t x, int32_t y) const;

 protected:
  friend class AlignmentParams;

  double shift_x_1000;
  double shift_y_1000;
  double camera_to_mill_xx;
  double camera_to_mill_xy;
  double camera_to_mill_yx;
  double camera_to_mill_yy;
  double mill_to_camera_xx;
  double mill_to_camera_xy;
  double mill_to_camera_yx;
  double mill_to_camera_yy;
  double camera_to_mill_scale;
};

class AlignmentParams {
 public:
  AlignmentParams(double camera_to_mill_scale = 1.0 , double roll = 0.0,
                  double shift_x = 0.0, double shift_y = 0.0,
                  jsCableOrientation cable=JS_CABLE_ORIENTATION_UPSTREAM);

  int SetRollAndOffset(double roll, double shift_x, double shift_y);
  int SetCableOrientation(jsCableOrientation cable);

  const Alignment* GetAlignment() const;
  const Transform* GetTransform() const;

 private:
  void CalculateTransform();
  Alignment m_alignment;
  Transform m_transform;
};

/*
 * Note: We inline these functions for performance benefits.
 */
inline Point2D<int32_t> Transform::CameraToMill(Point2D<int32_t> p) const
{
  return CameraToMill(p.x, p.y);
}

inline Point2D<int32_t> Transform::CameraToMill(int32_t x, int32_t y) const
{
  // static cast int32_t fields to doubles before doing our math
  double xd = static_cast<double>(x);
  double yd = static_cast<double>(y);

  // now calculate the mill values for both X and Y
  double xm = (xd * camera_to_mill_xx) + (yd * camera_to_mill_xy) +
              shift_x_1000;

  double ym = (xd * camera_to_mill_yx) + (yd * camera_to_mill_yy) +
              shift_y_1000;

  // now convert back to int32_t values
  int32_t xi = static_cast<int32_t>(xm);
  int32_t yi = static_cast<int32_t>(ym);

  return Point2D<int32_t>(xi, yi);
}

inline Point2D<int32_t> Transform::MillToCamera(Point2D<int32_t> p) const
{
  return MillToCamera(p.x, p.y);
}

inline Point2D<int32_t> Transform::MillToCamera(int32_t x, int32_t y) const
{
  // static cast int32_t fields to doubles before doing our math
  double xd = static_cast<double>(x);
  double yd = static_cast<double>(y);

  // now calculate the camera values for both X and Y
  double xc = ((xd - shift_x_1000) * mill_to_camera_xx) +
              ((yd - shift_y_1000) * mill_to_camera_xy);

  double yc = ((xd - shift_x_1000) * mill_to_camera_yx) +
              ((yd - shift_y_1000) * mill_to_camera_yy);

  // now convert back to int32_t values
  int32_t xi = static_cast<int32_t>(xc);
  int32_t yi = static_cast<int32_t>(yc);

  return Point2D<int32_t>(xi, yi);
}


} // namespace joescan

#endif // JOESCAN_ALIGNMENT_PARAMS_H
