#pragma once

#include "ava/debug/print_members_on.h"
#include "utils/Badge.h"

#include <cstdint>

namespace ava::tui::terminal {

// Forward declaration.
class Context;
class BasicWindow;
struct ConvertToColorPair;

// class ColorPair
//
// Wrapper around an index to a foreground/background color pair.
// A ColorPair must be created using Context::create_color_pair.
//
class ColorPair
{
 private:
  uint32_t index_;

 public:
  // Construct a ColorPair with a given index.
  ColorPair(utils::Badge<Context, BasicWindow, ConvertToColorPair>, uint32_t index) : index_(index) { }

  // Construct a default colors ColorPair.
  ColorPair() : index_(0) { }

  // Accessor.
  uint32_t index() const { return index_; }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
