#pragma once

#include "Color.h"
#include "ColorPair.h"
#include "ComplexChar.h"
#include "Window.h"

#include <vector>

namespace terminal {

// Session
//
// Represents the terminal. It's lifetime is equivalent with the
// time that the terminal is under the control of this application.
//
class Session final
{
 private:
  Window stdscr_;
  Rendition default_rendition_;
  std::vector<ColorPair> color_pairs_;

 public:
  Session();
  ~Session();

  Rendition const& default_rendition() const { return default_rendition_; }

  // Return a suitable ColorPair for the given colors.
  ColorPair create_color_pair(Color foreground, Color background);      // init_extended_pair

  Window const& stdscr() const { return stdscr_; }                      // stdscr
  Window& stdscr() { return stdscr_; }                                  //

  uint32_t rows() const;                                                // LINES
  uint32_t cols() const;                                                // COLS
  Dimension size() const { return {rows(), cols()}; }

  int get_wch();                                                        // get_wch
  // Synchronize the virtual screen with the physical screen.
  void doupdate();                                                      // doupdate

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace terminal
