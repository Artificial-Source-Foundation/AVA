#pragma once

#include <cstdint>

namespace terminal {

// class Position
//
// The coordinates of the top-left cell of a block of terminal cell-characters in (row, col).
// The top-left of the screen is (0, 0).
//
class Position
{
 private:
  uint32_t row_;        // The y-coordinate of the top-left cell.
  uint32_t col_;        // The x-coordinate of the top-left cell.

 public:
  // Construct a Position from a row and col.
  // Note that rows always come before colums; this is also how ncurses treats
  // coordinates although in that case they use (y, x) instead of (row, col).
  Position(uint32_t row, uint32_t col) : row_(row), col_(col) { }

  // Accessors.
  uint32_t row() const { return row_; }
  uint32_t col() const { return col_; }
};

} // namespace terminal
