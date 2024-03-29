/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_DYNAMIC_DATA_H
#define JOESCAN_DYNAMIC_DATA_H

#include "AlignmentParams.hpp"
#include "Point2D.hpp"
#include "ScanHeadModel.hpp"
#include "ScanWindow.hpp"
#include "joescan_pinchot.h"

#include <map>
#include <vector>

namespace joescan {

struct DynamicData {
 public:
  DynamicData(ScanHeadModel &model, jsUnits units);
  const jsScanHeadConfiguration* GetDefaultConfiguration();

  int SetConfiguration(jsScanHeadConfiguration &config);
  const jsScanHeadConfiguration* GetConfiguration();

  int SetCableOrientation(jsCableOrientation cable);
  jsCableOrientation GetCableOrientation();

  int SetAlignment(jsCamera camera, jsLaser laser, double roll,
                   double shift_x, double shift_y);
  const Alignment* GetAlignment(jsCamera camera, jsLaser laser);
  const Transform* GetTransform(jsCamera camera, jsLaser laser);

  int SetExclusionMask(jsCamera camera, jsLaser laser, jsExclusionMask &mask);
  const jsExclusionMask* GetExclusionMask(jsCamera camera, jsLaser laser);

  int SetBrightnessCorrection(jsCamera camera,
                              jsLaser laser,
                              jsBrightnessCorrection_BETA &correction);
  const jsBrightnessCorrection_BETA* GetBrightnessCorrection(jsCamera camera,
                                                             jsLaser laser);

  int SetWindow(jsCamera camera, jsLaser laser, ScanWindow &window);
  int SetPolygonWindow(jsCamera camera, jsLaser laser, jsCoordinate *points,
                       uint32_t points_len);
  const ScanWindow* GetWindow(jsCamera camera, jsLaser laser);

  bool IsDirty();
  void ClearDirty();
  void SetDirty();

 private:
  jsScanHeadConfiguration m_config_default;
  jsScanHeadConfiguration m_config;
  bool m_is_dirty;

  std::map<
    std::pair<jsCamera,jsLaser>,
    std::shared_ptr<AlignmentParams>> m_map_alignment;
  std::map<
    std::pair<jsCamera,jsLaser>,
    std::shared_ptr<jsBrightnessCorrection_BETA>> m_map_brightness_correction;
  std::map<
    std::pair<jsCamera,jsLaser>,
    std::shared_ptr<jsExclusionMask>> m_map_exclusion;
  std::map<
    std::pair<jsCamera,jsLaser>,
    std::shared_ptr<ScanWindow>> m_map_window;
};
}

#endif
