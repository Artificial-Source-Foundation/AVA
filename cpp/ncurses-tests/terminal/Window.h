#pragma once

#include "ComplexChar.h"
#include <memory>

namespace terminal {

class Window
{
 public:
  Window(int height, int width, int y, int x);
  ~Window();

  void set_background(ComplexChar background, bool erase = true);
  ComplexChar get_background() const;

  void erase();                                         // https://man.archlinux.org/man/curs_clear.3x.en
  void refresh();                                       // https://man.archlinux.org/man/curs_refresh.3x.en
  void box(int verch, int horch);                       // https://man.archlinux.org/man/curs_border.3x.en
  void addstr(int y, int x, char const* str);           // https://man.archlinux.org/man/curs_addstr.3x.en
  void addstr(int y, int x, char8_t const* wstr);       // https://man.archlinux.org/man/curs_addwstr.3x.en

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace terminal
