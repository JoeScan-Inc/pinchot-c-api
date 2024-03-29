#include "DynamicData.hpp"
#include "CameraLaserIterator.hpp"
#include <stdexcept>

using namespace joescan;

DynamicData::DynamicData(
  ScanHeadModel &model,
  jsUnits units)
{
  double alignment_scale = 0;
  if (JS_UNITS_INCHES == units) {
    alignment_scale = 1.0;
  } else if (JS_UNITS_MILLIMETER == units) {
    alignment_scale = 25.4;
  } else {
    throw std::runtime_error("invalid jsUnits");
  }

  auto iter = CameraLaserIterator(model);
  for (auto &pair : iter) {
    auto correction = std::make_shared<jsBrightnessCorrection_BETA>();
    correction->offset = 0;
    for (uint32_t k = 0; k < JS_SCAN_HEAD_DATA_COLUMNS_MAX_LEN; k++) {
      correction->scale_factors[k] = 1.0;
    }

    auto exclusion = std::make_shared<jsExclusionMask>();
    memset(exclusion->bitmap, 0, sizeof(exclusion->bitmap));

    jsCableOrientation cable = JS_CABLE_ORIENTATION_UPSTREAM;
    auto alignment = std::make_shared<AlignmentParams>(alignment_scale,
                                                       0.0,
                                                       0.0,
                                                       0.0,
                                                       cable);
    auto window = std::make_shared<ScanWindow>();

    m_map_alignment[pair] = alignment;
    m_map_brightness_correction[pair] = correction;
    m_map_exclusion[pair] = exclusion;
    m_map_window[pair] = window;
  }

  m_config_default.camera_exposure_time_min_us = 10000;
  m_config_default.camera_exposure_time_def_us = 500000;
  m_config_default.camera_exposure_time_max_us = 1000000;
  m_config_default.laser_on_time_min_us = 100;
  m_config_default.laser_on_time_def_us = 500;
  m_config_default.laser_on_time_max_us = 1000;
  m_config_default.laser_detection_threshold = 120;
  m_config_default.saturation_threshold = 800;
  m_config_default.saturation_percentage = 30;
  m_config = m_config_default;
  m_is_dirty = true;
}

int DynamicData::SetConfiguration(
  jsScanHeadConfiguration &config)
{
  m_config = config;
  m_is_dirty = true;
  return 0;
}

const jsScanHeadConfiguration* DynamicData::GetConfiguration()
{
  return &m_config;
}

const jsScanHeadConfiguration* DynamicData::GetDefaultConfiguration()
{
  return &m_config_default;
}

int DynamicData::SetCableOrientation(
  jsCableOrientation cable)
{
  if ((JS_CABLE_ORIENTATION_UPSTREAM != cable) &&
      (JS_CABLE_ORIENTATION_DOWNSTREAM != cable)) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  for (auto &m : m_map_alignment) {
    auto a = m.second;
    a->SetCableOrientation(cable);
  }

  m_is_dirty = true;
  return 0;
}

jsCableOrientation DynamicData::GetCableOrientation()
{
  // cable orientation is the same for all
  return m_map_alignment.begin()->second->GetAlignment()->cable;
}

const Alignment* DynamicData::GetAlignment(
  jsCamera camera,
  jsLaser laser)
{
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  return m_map_alignment[pair]->GetAlignment();
}

const Transform* DynamicData::GetTransform(
  jsCamera camera,
  jsLaser laser)
{
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  return m_map_alignment[pair]->GetTransform();
}

int DynamicData::SetAlignment(
  jsCamera camera,
  jsLaser laser,
  double roll,
  double shift_x,
  double shift_y)
{
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  if (m_map_alignment.find(pair) == m_map_alignment.end()) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  m_map_alignment[pair]->SetRollAndOffset(roll, shift_x, shift_y);
  m_is_dirty = true;
  return 0;
}

const jsExclusionMask* DynamicData::GetExclusionMask(
  jsCamera camera,
  jsLaser laser)
{
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  return m_map_exclusion[pair].get();
}

int DynamicData::SetExclusionMask(
  jsCamera camera,
  jsLaser laser,
  jsExclusionMask &mask)
{
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  if (m_map_exclusion.find(pair) == m_map_exclusion.end()) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  m_map_exclusion[pair] = std::make_shared<jsExclusionMask>(mask);
  m_is_dirty = true;
  return 0;
}

const jsBrightnessCorrection_BETA* DynamicData::GetBrightnessCorrection(
  jsCamera camera,
  jsLaser laser)
{
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  return m_map_brightness_correction[pair].get();
}

int DynamicData::SetBrightnessCorrection(
  jsCamera camera,
  jsLaser laser,
  jsBrightnessCorrection_BETA &correction)
{
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  if (m_map_brightness_correction.find(pair) ==
      m_map_brightness_correction.end()) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  m_map_brightness_correction[pair] =
    std::make_shared<jsBrightnessCorrection_BETA>(correction);
  m_is_dirty = true;
  return 0;
}

int DynamicData::SetWindow(
  jsCamera camera,
  jsLaser laser,
  ScanWindow &window)
{
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  if (m_map_window.find(pair) == m_map_window.end()) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  m_map_window[pair] = std::make_shared<ScanWindow>(window);
  m_is_dirty = true;
  return 0;
}

int DynamicData::SetPolygonWindow(jsCamera camera, jsLaser laser,
                                  jsCoordinate *points, uint32_t points_len)
{
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  if (m_map_window.find(pair) == m_map_window.end()) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  std::vector<jsCoordinate> c(points, points + points_len);
  // check for clockwise point ordering - https://stackoverflow.com/a/18472899
  double sum = 0.0;
  jsCoordinate p1 = c[points_len - 1];
  for (uint32_t n = 0; n < points_len; n++) {
    jsCoordinate p2 = c[n];
    sum += (p2.x - p1.x) * (p2.y + p1.y);
    p1 = p2;
  }

  // polygon is clockwise if sum is greater than zero
  if (0.0 >= sum) {
    return JS_ERROR_INVALID_ARGUMENT;
  }

  // check for convexity - https://stackoverflow.com/a/1881201
  // add the first two points to the end of the list
  // to make loop cleaner since we have to calculate
  // (p[N-2],p[N-1],p[0]) and (p[N-1],p[0],p[1]).
  c.push_back(c[0]);
  c.push_back(c[1]);

  std::vector<double> product;
  for (uint32_t n = 0; n < points_len; n++) {
    double dx1 = c[n + 1].x - c[n + 0].x;
    double dy1 = c[n + 1].y - c[n + 0].y;
    double dx2 = c[n + 2].x - c[n + 1].x;
    double dy2 = c[n + 2].y - c[n + 1].y;
    product.push_back((dx1 * dy2) - (dy1 * dx2));
  }

  // polygon is convex if the sign of all cross products is the same
  bool is_negative = (product[0] < 0.0) ? true : false;
  for (auto &p : product) {
    if (0.0 == p) {
      return JS_ERROR_INVALID_ARGUMENT;
    } else if (is_negative && (0.0 < p)) {
      return JS_ERROR_INVALID_ARGUMENT;
    } else if (!is_negative && (0.0 > p)) {
      return JS_ERROR_INVALID_ARGUMENT;
    }
  }

  // remove the two extra points used to check cross product
  c.pop_back();
  c.pop_back();

  m_map_window[pair] = std::make_shared<ScanWindow>(c);
  m_is_dirty = true;
  return 0;
}

const ScanWindow* DynamicData::GetWindow(
  jsCamera camera,
  jsLaser laser)
{
  std::pair<jsCamera, jsLaser> pair(camera, laser);
  return m_map_window[pair].get();
}

bool DynamicData::IsDirty()
{
  return m_is_dirty;
}

void DynamicData::ClearDirty()
{
  m_is_dirty = false;
}

void DynamicData::SetDirty()
{
  m_is_dirty = true;
}
