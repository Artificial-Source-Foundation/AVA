#pragma once

#include "ComplexChar.h"
#include "ColorPair.h"
#include "Color.h"
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

  Rendition const& default_rendition() const
  {
    return default_rendition_;
  }

  // Return a suitable ColorPair for the given colors.
  ColorPair create_color_pair(Color foreground, Color background);

  Window const& stdscr() const { return stdscr_; }
  Window& stdscr() { return stdscr_; }

  int rows() const;
  int cols() const;
  int get_wch();
};

} // namespace terminal
