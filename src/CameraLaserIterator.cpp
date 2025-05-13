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
  return (uint32_t) m_pairs.size();
}
