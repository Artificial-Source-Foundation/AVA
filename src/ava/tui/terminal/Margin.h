#pragma once

#include <cstdint>

namespace terminal {

// Struct Margin
//
// Aggregate containing the desired rows and columns of cell offset between a parent Window and a to be created subwindow.
//
//  Parent Window
//  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
//  ┃                top              ┃
//  ┃      ┏━━━━━━━━━━━━━━━━━━━━━━┓   ┃
//  ┃      ┃ subwindow            ┃ r ┃
//  ┃      ┃                      ┃ i ┃
//  ┃ left ┃                      ┃ g ┃
//  ┃      ┃                      ┃ h ┃
//  ┃      ┃                      ┃ t ┃
//  ┃      ┗━━━━━━━━━━━━━━━━━━━━━━┛   ┃
//  ┃               bottom            ┃
//  ┃                                 ┃
//  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
//
// Hence, the top-left of the subwindow will become (top, left) relative the to parent Window.
//
struct Margin
{
  uint8_t top = 0;
  uint8_t bottom = 0;
  uint8_t left = 0;
  uint8_t right = 0;
};

} // namespace terminal
