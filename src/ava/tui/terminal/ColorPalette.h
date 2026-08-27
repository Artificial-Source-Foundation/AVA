#pragma once

#include "CIEDE2000.h"
#include "Color.h"
#include "ava/debug/print_members_on.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::tui::terminal {

class Context;
struct ColorPaletteTestAccess;

class ColorPalette
{
  friend struct ColorPaletteTestAccess;

 private:
  std::vector<CIEDE2000::LAB> palette_;         // Live color palette as CIELAB values.

 private:
  // Construct a ColorPalette from an r-value reference to a vector with LAB values. Called from ColorPalette::create.
  ColorPalette(std::vector<CIEDE2000::LAB>&& palette) : palette_(std::move(palette)) { }

  // Build OSC 4 queries for palette indices in `[first_index, past_last_index)`.
  static std::string osc4_queries(int first_index, int past_last_index);

  // Parse one complete OSC 4 `response` into its palette index and concrete sRGB color.
  static std::optional<std::pair<int, Color>> parse_osc4_response(std::string_view response);

  // Parse `responses` and return exactly `number_of_colors` CIELAB entries ordered by palette index.
  //
  // Unrelated bytes are ignored. Missing, duplicate, out-of-range, or malformed OSC 4 responses fail the complete probe.
  static std::optional<std::vector<CIEDE2000::LAB>> parse_osc4_responses(std::string_view responses, int number_of_colors);

 public:
  // Convert the concrete packed-sRGB `rgb` color to D65-relative CIELAB.
  //
  // The terminal-default Color has no concrete RGB value and must not be passed.
  static CIEDE2000::LAB rgb_to_lab(Color rgb);

  // Convert the finite D65-relative CIELAB color `lab` to a packed-sRGB Color.
  //
  // Values outside the sRGB gamut are clipped to the nearest RGB-cube boundary before 8-bit quantization.
  static Color lab_to_rgb(CIEDE2000::LAB const& lab);

  // Probe stdscr of `context` with OSC 4 to find the `number_of_colors` sRGB values of its live color palette.
  //
  // Returns null when output fails, the terminal does not reply before the bounded timeout, or any palette response is malformed,
  // duplicated, missing, or out of range. The probe temporarily disables keypad decoding and restores stdscr's prior timeout.
  static std::unique_ptr<ColorPalette> create(Context& context, int number_of_colors);

  // Accessor.
  std::vector<CIEDE2000::LAB> const& palette() const { return palette_; }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
