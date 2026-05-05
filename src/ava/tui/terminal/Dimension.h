#pragma once

#include <cstdint>

namespace terminal {

// class Dimension
//
// The size of a block of terminal cell-characters in rows x cols.
//
class Dimension
{
 private:
  uint32_t height_;     // The height in rows.
  uint32_t width_;      // The width in columns.

 public:
  Dimension(uint32_t height, uint32_t width) : height_(height), width_(width) { }

  // Accessors.
  uint32_t height() const { return height_; }
  uint32_t width() const { return width_; }

  // Note: all arthimetic rounds the results *down* to the nearest integer.

  Dimension& operator*=(float n)
  {
    height_ *= n;
    width_ *= n;
    return *this;
  }

  Dimension& operator/=(float n)
  {
    height_ /= n;
    width_ /= n;
    return *this;
  }

  friend Dimension operator*(Dimension d, float n) { return d *= n; }
  friend Dimension operator/(Dimension d, float n) { return d /= n; }
};

} // namespace terminal
