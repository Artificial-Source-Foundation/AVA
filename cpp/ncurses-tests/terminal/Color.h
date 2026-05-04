#pragma once

#include <cstdint>
#include <limits>
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
  static constexpr uint32_t default_terminal_color = std::numeric_limits<std::uint32_t>::max();

  uint32_t rgb_;

 public:
  // Construct a color meaning "the default terminal color" (for either foreground or background).
  Color() : rgb_(default_terminal_color) { }

  // Construct a Color by RGB value (0x000000 (black) till 0xffffff (white)).
  Color(uint32_t rgb) : rgb_(rgb)
  {
    if (rgb_ < 8)
      rgb_ = 0;

    // Invalid color.
    ASSERT(rgb_ < 0x1000000);
  }

  int as_int() const
  {
    return static_cast<int>(static_cast<int32_t>(rgb_));
  }
};

} // namespace terminal
