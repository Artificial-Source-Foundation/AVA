#pragma once

#include "ComplexChar.h"
#include "Border.h"
#include "Dimension.h"
#include "Position.h"
#include <memory>

namespace terminal {

// Forward declaration.
class Session;

class Window
{
 private:
  // These are called before ncurses is initialized by the constructor of Session.
  friend class Session;
  Window();                     // Construct an uninitialized Window.
  void init_as_stdscr();        // Initialize a default constructed window with stdscr.

 public:
  // Construct a new Window with its top-left cell at `pos` with dimension `size`.
  Window(Dimension size, Position pos);

  // The destructor must be defined in the .cxx file because of the std::unique_ptr<Impl> with incomplete `Impl`.
  ~Window();

  void set_background(ComplexChar background, bool erase = true);
  ComplexChar get_background() const;

  void erase();                                                 // https://man.archlinux.org/man/curs_clear.3x.en
  void refresh();                                               // https://man.archlinux.org/man/curs_refresh.3x.en
  void set_border(Border const& border);                        // https://man.archlinux.org/man/curs_border_set.3x.en

  // https://invisible-island.net/ncurses/man/curs_addstr.3x.html

  void addstr(char const* str);                                 // waddstr
  void addstr(char8_t const* str);                              //

  void addstr(Position pos, char const* str);                   // mvwaddstr
  void addstr(Position pos, char8_t const* wstr);               //

  void addstr(char const* str, int n);                          // waddnstr
  void addstr(char8_t const* str, int n);                       //

  void addstr(Position pos, char const* str, int n);            // mvwaddnstr
  void addstr(Position pos, char8_t const* str, int n);         //

  // https://invisible-island.net/ncurses/man/curs_add_wch.3x.html

  void addch(ComplexChar const& complex_char);                  // wadd_wch
  void addch(Position pos, ComplexChar const& complex_char);    // mvwadd_wch

  void echochar(ComplexChar const& complex_char);               // wecho_wchar

  // https://invisible-island.net/ncurses/man/curs_move.3x.html

  void move(Position pos);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace terminal
