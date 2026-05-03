#pragma once

#include <cstdint>

namespace terminal {

class ColorPair
{
 private:
  uint32_t index_;

 public:
  ColorPair(uint32_t index) : index_(index) { }

  // Accessor.
  uint32_t index() const { return index_; }
  uint32_t& index() { return index_; }
};

} // namespace terminal
