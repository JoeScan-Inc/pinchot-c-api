#include "CameraLaserIterator.hpp"
#include "ScanHead.hpp"
#include "ScanHeadModel.hpp"
#include <algorithm>

using namespace joescan;

CameraLaserIterator::CameraLaserIterator(ScanHeadModel &model)
{
  using namespace joescan::schema::client;
  jsCamera camera = JS_CAMERA_INVALID;
  jsLaser laser = JS_LASER_INVALID;

  const uint32_t num_cameras =  model.m_specification.number_of_cameras;
  const uint32_t num_lasers =  model.m_specification.number_of_lasers;

  for (auto &grp : model.m_specification.configuration_groups) {
    camera = model.CameraPortToId(grp.camera_port());
    laser = model.LaserPortToId(grp.laser_port());
    m_pairs.push_back(std::make_pair(camera, laser));
  }
}

CameraLaserIterator::CameraLaserIterator(ScanHead &scan_head) :
  CameraLaserIterator(scan_head.m_model)
{ }

CameraLaserIterator::CameraLaserIterator(ScanHead *scan_head) :
  CameraLaserIterator(scan_head->m_model)
{ }

void CameraLaserIterator::reverse()
{
  std::reverse(m_pairs.begin(), m_pairs.end());
}

uint32_t CameraLaserIterator::count()
{
  return m_pairs.size();
}
