/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JSCANAPI_SCAN_WINDOW_H
#define JSCANAPI_SCAN_WINDOW_H

#include <vector>
#include "SetWindowMessage.hpp"
#include "WindowConstraint.hpp"

namespace joescan {
class ScanWindow {
 public:
  /**
   * Set the window at which a camera will look for the laser. Note the
   * `bottom` must not be greater than the `top` and the `left` must not be
   * greater than the `right`.
   *
   *     // set window to 10 inches on top, -10 inches on bottom
   *     // 10 inches on right, -10 inches on left.
   *     ScanWindow window(10.0, -10.0, -10.0, 10.0);
   *
   * @param top The top window dimension in inches.
   * @param bottom The bottom window dimension in inches.
   * @param left The left window dimension in inches.
   * @param right The right window dimension in inches.
   */
  ScanWindow(double top, double bottom, double left, double right);

  /**
   * Initializes a scan window to the same values held by another
   * `ScanWindow` object.
   *
   * @param other Reference to `ScanWindow` with initialization values.
   */
  ScanWindow(const ScanWindow &other) = default;

  /**
   * Returns the `WindowConstraints` of the scan window. Note that the
   * constraints are expressed in 1/1000 inches; the window dimensions passed
   * in during the initialization of `ScanWindow` are automatically converted
   * to 1/1000 inches to ease use in calculations within the client API.
   *
   * @return Vector of constraints expressed in 1/1000 inches.
   */
  std::vector<WindowConstraint> Constraints() const;

 private:
  /// Vector of constraints in 1/1000 inches.
  std::vector<WindowConstraint> constraints;
};
} // namespace joescan

#endif // JSCANAPI_SCAN_WINDOW_H