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
  int last_mutable_palette_index_;              // Exclusive upper bound of the palette prefix assumed to be mutable, or zero.

 private:
  // Construct a ColorPalette from an r-value reference to LAB values and the exclusive `last_mutable_palette_index` bound.
  // Called from ColorPalette::create.
  ColorPalette(std::vector<CIEDE2000::LAB>&& palette, int last_mutable_palette_index)
      : palette_(std::move(palette)), last_mutable_palette_index_(last_mutable_palette_index)
  {
    DoutEntering(dc::terminal, "ColorPalette::ColorPalette(" << palette_ << ", " << last_mutable_palette_index << ")");
  }

  // Build one OSC 4 query for `color_index`.
  static std::string osc4_query(int color_index);

  // Build `number_of_colors` consecutive OSC 4 queries starting at `first_color_index`.
  static std::string osc4_queries(int first_color_index, int number_of_colors);

  // Build one OSC 4 command that assigns the concrete sRGB `color` to `color_index`.
  static std::string osc4_set_color(int color_index, Color color);

  // Parse one complete OSC 4 `response` into its palette index and concrete sRGB color.
  static std::optional<std::pair<int, Color>> parse_osc4_response(std::string_view response);

  // Probe and return the live sRGB Color at `color_index` through the terminal streams owned by `context`.
  //
  // stdscr must have a bounded input timeout and keypad decoding disabled. Returns no value on output failure, timeout, malformed
  // response, an unexpected response index, non-byte input, or an oversized response.
  static std::optional<Color> probe_color(Context& context, int color_index);

  // Probe and return `number_of_colors` consecutive live sRGB colors starting at `first_color_index`.
  //
  // All queries are emitted in one write and replies may arrive in any order. stdscr has the same preconditions and failures as
  // probe_color; success returns colors ordered by palette index.
  static std::vector<Color> probe_colors(Context& context, int first_color_index, int number_of_colors);

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

  // Return the exclusive upper bound of the palette prefix that callers may assume is mutable.
  //
  // Zero means that no tested palette prefix was mutable.
  int last_mutable_palette_index() const { return last_mutable_palette_index_; }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
