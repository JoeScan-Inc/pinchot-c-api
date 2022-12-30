/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_SCAN_WINDOW_H
#define JOESCAN_SCAN_WINDOW_H

#include <vector>
#include "joescan_pinchot.h"
#include "WindowConstraint.hpp"

namespace joescan {
class ScanWindow {
 public:
  /**
   * Set the window at which a camera will look for the laser. Note the
   * `bottom` must not be greater than the `top` and the `left` must not be
   * greater than the `right`.
   *
   * @param top The top window dimension in scan system units.
   * @param bottom The bottom window dimension in scan system units.
   * @param left The left window dimension in scan system units.
   * @param right The right window dimension in scan system units.
   */
  ScanWindow(double top = 30.0, double bottom = -30.0, double left = -30.0,
             double right = 30.0);

  ScanWindow(std::vector<jsCoordinate> coordinates);

  /**
   * Initializes a scan window to the same values held by another
   * `ScanWindow` object.
   *
   * @param other Reference to `ScanWindow` with initialization values.
   */
  ScanWindow(const ScanWindow &other) = default;

  /**
   * Returns the `WindowConstraints` of the scan window. Note that the
   * constraints are expressed in 1/1000 scan system units; the window
   * dimensions passed in during the initialization of `ScanWindow` are
   * automatically converted to 1/1000 scan system units to ease use in
   * calculations within the client API.
   *
   * @return Vector of constraints expressed in 1/1000 scan system units.
   */
  std::vector<WindowConstraint> GetConstraints() const;

  std::vector<jsCoordinate> GetCoordinates() const;

  double GetTop() const;
  double GetBottom() const;
  double GetLeft() const;
  double GetRight() const;

 private:
  /// Vector of constraints in 1/1000 scan system units.
  std::vector<WindowConstraint> m_constraints;
  std::vector<jsCoordinate> m_coordinates;
  double m_top;
  double m_bottom;
  double m_left;
  double m_right;
};
} // namespace joescan

#endif // JOESCAN_SCAN_WINDOW_H
