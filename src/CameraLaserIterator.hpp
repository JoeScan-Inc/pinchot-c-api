/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_CAMERA_LASER_ITERATOR_H
#define JOESCAN_CAMERA_LASER_ITERATOR_H

#include <utility>
#include <vector>

#include "ScanHeadSpecification_generated.h"
#include "joescan_pinchot.h"

namespace joescan {

// forward declaration
class ScanHead;
class ScanHeadModel;

class CameraLaserIterator {
 private:
  typedef std::vector<std::pair<jsCamera, jsLaser>> CameraLaserPairVector;
  CameraLaserPairVector m_pairs;

 public:
  CameraLaserIterator(ScanHeadModel &model);
  CameraLaserIterator(ScanHead &scan_head);
  CameraLaserIterator(ScanHead *scan_head);

  void reverse();
  uint32_t count();

  typedef CameraLaserPairVector::iterator iterator;
  typedef CameraLaserPairVector::reverse_iterator reverse_iterator;
  typedef CameraLaserPairVector::const_iterator const_iterator;
  typedef CameraLaserPairVector::const_reverse_iterator const_reverse_iterator;
  iterator begin() { return m_pairs.begin(); }
  iterator end() { return m_pairs.end(); }
  const_iterator cbegin() { return m_pairs.cbegin(); }
  const_iterator cend() { return m_pairs.cend(); }
  reverse_iterator rbegin() { return m_pairs.rbegin(); }
  reverse_iterator rend() { return m_pairs.rend(); }
  const_reverse_iterator crbegin() { return m_pairs.crbegin(); }
  const_reverse_iterator crend() { return m_pairs.crend(); }
};

}
#endif
