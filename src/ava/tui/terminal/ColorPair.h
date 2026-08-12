#pragma once

#include "ava/debug/print_members_on.h"

#include <cstdint>

namespace ava::tui::terminal {

// Forward declaration.
class Context;

// class ColorPair
//
// Wrapper around an index to a foreground/background color pair.
// A ColorPair must be created using Context::create_color_pair.
//
class ColorPair
{
 private:
  uint32_t index_;

 private:
  friend class Context;
  ColorPair(uint32_t index) : index_(index) { }

 public:
  // Construct a default colors ColorPair.
  ColorPair() : index_(0) { }

  // Accessor.
  uint32_t index() const { return index_; }
  uint32_t& index() { return index_; }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
