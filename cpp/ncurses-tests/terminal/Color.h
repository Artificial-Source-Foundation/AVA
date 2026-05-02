#pragma once

#include <cstdint>
#include "debug.h"

namespace terminal {

// class Color
//
// A wrapper for the direct-color RGB terminal values.
// Unfortunately, it depends on the terminal how such a color is displayed,
// as well as any Attributes that are in effect of course.
//
// Because on direct-color terminals (where COLORS is 16777216) this
// value will used as direct color index, meaning that the values
// 1 through 7 have a special meaning (COLOR_RED through COLOR_WHITE).
// To avoid confusion, this class will not store such values, but instead
// replaces those with 0 (black).
//
class Color
{
 private:
  uint32_t rgb_;

 public:
  Color(uint32_t rgb) : rgb_(rgb)
  {
    if (rgb_ < 8)
      rgb_ = 0;

    // Invalid color.
    ASSERT(rgb_ < 0x1000000);
  }

  int as_int() const
  {
    return static_cast<int>(rgb_);
  }
};

} // namespace terminal
