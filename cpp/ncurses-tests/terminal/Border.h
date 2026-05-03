#pragma once

#include "ComplexChar.h"
#include <array>
#include <string>

namespace terminal {

// class Border
//
// While a real ncursesw border exists of eight cchar_t complex characters,
// we only store a single wchar_t here: combining characters are not possible.
// The Rendition has to be supplied separatedly.
//
class Border
{
 public:
  static constexpr int ls = 3;  // Index into box_characters_ for the left hand side character of the border.
  static constexpr int rs = 5;  //               ,,                  right hand side           ,,
  static constexpr int ts = 1;  //               ,,                    top side                ,,
  static constexpr int bs = 7;  //               ,,                 bottom side                ,,
  static constexpr int tl = 0;  //               ,,                    top left                ,,
  static constexpr int tr = 2;  //               ,,                    top right               ,,
  static constexpr int bl = 6;  //               ,,                 bottom left                ,,
  static constexpr int br = 8;  //               ,,                 bottom right               ,,

  static constexpr std::array<int, 8> index_to_pos = { ls, rs, ts, bs, tl, tr, bl, br };

  // Default string.
  static constexpr wchar_t const* default_box =
    L"┏━┓"    // Corresponds with indices into box_characters_: 012
     "┃ ┃"    //                                                3 5
     "┗━┛";   //                                                678

 private:
  std::wstring const box_characters_;

 public:
  Border(wchar_t const* box = default_box) : box_characters_(box) { }

  // Return the i-th border character as a ComplexChar using Rendition rendition,
  // where i = 0 corresponds to the left hand side, i = 1 corresponds to right hand side, etc.
  // (the same order as the elements of index_to_pos).
  ComplexChar get_complex_character(int i, Rendition rendition) const
  {
    std::array<wchar_t, 2> zero_terminated_character = { box_characters_[index_to_pos[i]], L'\0' };
    return {zero_terminated_character.data(), rendition};
  }
};

} // namespace terminal
