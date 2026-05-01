#pragma once

#include "Attributes.h"
#include <cstdint>

namespace terminal {

// ComplexChar
//
// Combines a (potentially empty) (variable width) character with Attributes and a color pair (by index).
class ComplexChar
{
 private:
  static constexpr int CCHARW_MAX = 5;
  static constexpr wchar_t const* empty_ = L" ";

  wchar_t	variable_width_character_[CCHARW_MAX];          // Character to display.
  Attributes    attributes_;                                    // Text attributes that apply.
  int		color_pair_;                                    // Color pair index of the foreground/background colors to use.

 public:
  // vwch = variable width character (UTF8 encoded).
  ComplexChar(wchar_t const* vwch, Attributes attributes, int color_pair);

  // Use default colors.
  ComplexChar(wchar_t const* vwch, Attributes attributes);

  // No attributes.
  ComplexChar(wchar_t const* vwch, int color_pair);

  // Use default colors and no attributes.
  ComplexChar(wchar_t const* vwch);

  // No variable width character.
  ComplexChar(Attributes attributes, int color_pair) : ComplexChar(empty_, attributes, color_pair) { }
  ComplexChar(Attributes attributes) : ComplexChar(empty_, attributes) { }
  ComplexChar(int color_pair) : ComplexChar(empty_, color_pair) { }
  ComplexChar() : ComplexChar(empty_) { }

  // Accessors.
  wchar_t const* character() const { return variable_width_character_; }
  Attributes attributes() const { return attributes_; }
  int color_pair() const { return color_pair_; }

  // Manipulators.
  void set_color_pair(int color_pair) { color_pair_ = color_pair; }
};

} // namespace terminal
