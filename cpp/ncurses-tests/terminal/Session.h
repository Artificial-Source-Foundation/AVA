#pragma once

#include <curses.h>     // chtype, A_NORMAL

namespace terminal {

// Session
//
// Represents the terminal. It's lifetime is equivalent with the
// time that the terminal is under the control of this application.
//
class Session final
{
 private:
  cchar_t background_cchar;

 public:
  Session();
  ~Session();

  cchar_t const* get_background_cchar() const
  {
    return &background_cchar;
  }

  int rows() const;
};

} // namespace terminal
