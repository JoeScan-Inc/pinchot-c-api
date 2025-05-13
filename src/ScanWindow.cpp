/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "ScanWindow.hpp"
#include "Point2D.hpp"

#include <cstring>
#include <stdexcept>

using namespace joescan;

ScanWindow::ScanWindow(): m_top(0.0), m_bottom(0.0), m_left(0.0),
                          m_right(0.0), m_type(JS_SCAN_WINDOW_UNCONSTRAINED)
{
}

ScanWindow::ScanWindow(double top, double bottom, double left, double right)
  : m_top(top), m_bottom(bottom), m_left(left), m_right(right),
  m_type(JS_SCAN_WINDOW_RECTANGULAR)
{
  if (top <= bottom) {
    throw std::range_error("Window top must be greater than window bottom");
  }

  if (right <= left) {
    throw std::range_error("Window right must be greater than window left");
  }

  // convert from units to 1/1000 of a unit
  // units are either inches or millimeter
  int32_t top1000 = static_cast<int32_t>(top * 1000.0);
  int32_t bottom1000 = static_cast<int32_t>(bottom * 1000.0);
  int32_t left1000 = static_cast<int32_t>(left * 1000.0);
  int32_t right1000 = static_cast<int32_t>(right * 1000.0);

  m_constraints.push_back(
    WindowConstraint(Point2D<int64_t>(left1000, top1000),
                     Point2D<int64_t>(right1000, top1000)));

  m_constraints.push_back(
    WindowConstraint(Point2D<int64_t>(right1000, bottom1000),
                     Point2D<int64_t>(left1000, bottom1000)));

  m_constraints.push_back(
    WindowConstraint(Point2D<int64_t>(right1000, top1000),
                     Point2D<int64_t>(right1000, bottom1000)));

  m_constraints.push_back(
    WindowConstraint(Point2D<int64_t>(left1000, bottom1000),
                     Point2D<int64_t>(left1000, top1000)));
  
  jsCoordinate point;
  point.x = left;
  point.y = top;
  m_coordinates.push_back(point);

  point.x = right;
  point.y = top;
  m_coordinates.push_back(point);

  point.x = right;
  point.y = bottom;
  m_coordinates.push_back(point);

  point.x = left;
  point.y = bottom;
  m_coordinates.push_back(point);
}

ScanWindow::ScanWindow(std::vector<jsCoordinate> coordinates)
  : m_coordinates(coordinates), m_top(0.0), m_bottom(0.0), m_left(0.0),
    m_right(0.0), m_type(JS_SCAN_WINDOW_POLYGONAL)
{
  for (uint32_t n = 0; n < (coordinates.size() - 1); n++) {
    int32_t x0 = (int32_t) (coordinates[n + 0].x * 1000);
    int32_t y0 = (int32_t) (coordinates[n + 0].y * 1000);
    int32_t x1 = (int32_t) (coordinates[n + 1].x * 1000);
    int32_t y1 = (int32_t) (coordinates[n + 1].y * 1000);
    m_constraints.push_back(
      WindowConstraint(Point2D<int64_t>(x0, y0), Point2D<int64_t>(x1, y1)));
  }

  // connect first & last points together
  {
    int32_t x0 = (int32_t) (coordinates[coordinates.size() - 1].x * 1000);
    int32_t y0 = (int32_t) (coordinates[coordinates.size() - 1].y * 1000);
    int32_t x1 = (int32_t) (coordinates[0].x * 1000);
    int32_t y1 = (int32_t) (coordinates[0].y * 1000);
    m_constraints.push_back(
      WindowConstraint(Point2D<int64_t>(x0, y0), Point2D<int64_t>(x1, y1)));
  }
}

std::vector<WindowConstraint> ScanWindow::GetConstraints() const
{
  return m_constraints;
}

std::vector<jsCoordinate> ScanWindow::GetCoordinates() const
{
  return m_coordinates;
}

jsScanWindowType ScanWindow::GetType() const
{
  return m_type;
}

double ScanWindow::GetTop() const
{
  return m_top;
}

double ScanWindow::GetBottom() const
{
  return m_bottom;
}

double ScanWindow::GetLeft() const
{
  return m_left;
}

double ScanWindow::GetRight() const
{
  return m_right;
}
