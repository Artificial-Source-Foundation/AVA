#pragma once

#include <cstdint>

namespace terminal {

// Forward declaration.
class Session;

// class ColorPair
//
// Wrapper around an index to a foreground/background color pair.
// A ColorPair must be created using Session::create_color_pair.
//
class ColorPair
{
 private:
  uint32_t index_;

 private:
  friend class Session;
  ColorPair(uint32_t index) : index_(index) { }

 public:
  // Construct a default colors ColorPair.
  ColorPair() : index_(0) { }

  // Accessor.
  uint32_t index() const { return index_; }
  uint32_t& index() { return index_; }
};

} // namespace terminal
