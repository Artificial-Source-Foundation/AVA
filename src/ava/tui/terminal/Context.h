#pragma once

#include "BasicScreen.h"
#include "BasicWindow.h"
#include "Color.h"
#include "ColorPair.h"
#include "ComplexChar.h"

#include <vector>

namespace ava::tui::terminal {

// Context
//
// Represents the terminal. It's lifetime is equivalent with the
// time that the terminal is under the control of this application.
//
class Context final
{
 private:
  BasicScreen first_screen_;                                            // Owns the screen iff `outfd` and `infd` are passed to the constructor.
  BasicWindow stdscr_;                                                  // The entire surface of the current terminal screen.
  Rendition default_rendition_;                                         // The rendition to use for text that doesn't have any defined of its own.
  bool default_colors_enabled_ = false;                                 // Whether -1 selects the terminal's default colors.
  std::vector<ColorPair> color_pairs_;                                  // All registered foreground/background color pairs so far.

 public:
  Context(FILE* outfd = nullptr, FILE* infd = nullptr);
  ~Context();

  Rendition const& default_rendition() const { return default_rendition_; }

  // Return a ColorPair for `foreground` and `background`, using exact RGB on direct-color terminals and nearest palette
  // colors otherwise.
  //
  // The terminal must support colors and have room for another color pair. The default terminal color is preserved on both paths.
  ColorPair create_color_pair(Color foreground, Color background);      // init_extended_pair

  BasicScreen const& first_screen() const { return first_screen_; }
  BasicScreen& first_screen() { return first_screen_; }

  BasicWindow const& stdscr() const { return stdscr_; }                 // stdscr
  BasicWindow& stdscr() { return stdscr_; }                             //

  uint32_t rows() const;                                                // LINES
  uint32_t cols() const;                                                // COLS
  Dimension size() const { return {rows(), cols()}; }

  int get_wch();                                                        // get_wch
  // Synchronize the virtual screen with the physical screen.
  void doupdate();                                                      // doupdate

  bool has_colors();                                                    // has_colors

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
