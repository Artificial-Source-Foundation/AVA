#pragma once

#include "CIEDE2000.h"
#include "Color.h"
#include "ava/debug/print_members_on.h"

#include <memory>
#include <vector>

namespace ava::tui::terminal {

class Context;

class ColorPalette
{
 private:
  std::vector<CIEDE2000::LAB> palette_;         // Live color palette as CIELAB values.

 public:
  // Convert the concrete packed-sRGB `rgb` color to D65-relative CIELAB.
  //
  // The terminal-default Color has no concrete RGB value and must not be passed.
  static CIEDE2000::LAB rgb_to_lab(Color rgb);

  // Convert the finite D65-relative CIELAB color `lab` to a packed-sRGB Color.
  //
  // Values outside the sRGB gamut are clipped to the nearest RGB-cube boundary before 8-bit quantization.
  static Color lab_to_rgb(CIEDE2000::LAB const& lab);

  // Probe stdscr of `context` with OSC 4 to find the `number_of_colors` sRGB values of its live color palette and store them as LAB values in `palette_`.
  static std::unique_ptr<ColorPalette> create(Context& context, int number_of_colors);

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
