#pragma once

#include "ComplexChar.h"

namespace terminal {

// Session
//
// Represents the terminal. It's lifetime is equivalent with the
// time that the terminal is under the control of this application.
//
class Session final
{
 private:
  ComplexChar background_cchar_;

 public:
  Session();
  ~Session();

  ComplexChar const& get_background_cchar() const
  {
    return background_cchar_;
  }

  int rows() const;
  void refresh();
  int get_wch();
};

} // namespace terminal
