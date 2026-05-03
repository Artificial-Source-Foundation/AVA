#pragma once

#include "ComplexChar.h"
#include "Border.h"
#include <memory>

namespace terminal {

class Window
{
 public:
  Window(int height, int width, int y, int x);
  ~Window();

  void set_background(ComplexChar background, bool erase = true);
  ComplexChar get_background() const;

  void erase();                                                 // https://man.archlinux.org/man/curs_clear.3x.en
  void refresh();                                               // https://man.archlinux.org/man/curs_refresh.3x.en
  void set_border(Border const& border);                        // https://man.archlinux.org/man/curs_border_set.3x.en

  // https://invisible-island.net/ncurses/man/curs_addstr.3x.html

  void addstr(char const* str);                                 // waddstr
  void addstr(char8_t const* str);                              //

  void addstr(int y, int x, char const* str);                   // mvwaddstr
  void addstr(int y, int x, char8_t const* wstr);               //

  void addstr(char const* str, int n);                          // waddnstr
  void addstr(char8_t const* str, int n);                       //

  void addstr(int y, int x, char const* str, int n);            // mvwaddnstr
  void addstr(int y, int x, char8_t const* str, int n);         //

  // https://invisible-island.net/ncurses/man/curs_add_wch.3x.html

  void addch(ComplexChar const& complex_char);                  // wadd_wch
  void addch(int y, int x, ComplexChar const& complex_char);    // mvwadd_wch

  void echochar(ComplexChar const& complex_char);               // wecho_wchar

  // https://invisible-island.net/ncurses/man/curs_move.3x.html

  void move(int y, int x);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace terminal
