#pragma once

namespace terminal {

class ColorPair
{
 private:
  int index_;

 public:
  ColorPair(int index) : index_(index) { }

  // Accessor.
  int index() const { return index_; }
};

} // namespace terminal
