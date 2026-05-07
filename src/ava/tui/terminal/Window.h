#pragma once

#include "Border.h"
#include "ComplexChar.h"
#include "Dimension.h"
#include "Position.h"

#include <memory>

// To print all implemented ncurses functions (from the comments):
//
// grep -E '^ *(void|Window ).*// [a-z_]' Window.h | sed -e 's/^.*\/\/ //;s/ \/ /,/' | tr ',' '\n' | sort -u
//
// WINDOW related ncurses functions:
//
// grep '^extern.*WINDOW *\*.*implemented' /usr/include/curses.h | grep -v SCREEN | sed -re 's/^extern NCURSES_EXPORT\([^)]*\) ([^ (]*).*/\1/' | sort -u
//
// Missing members:
//
//   mvwin
//
namespace terminal {

// Forward declaration.
class Session;

class Window
{
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

 private:
  // These are called before ncurses is initialized by the constructor of Session.
  friend class Session;
  Window();                     // Construct an uninitialized Window.
  void init_as_stdscr();        // Initialize a default constructed window with stdscr.

  // Wrap an already created subwindow Impl.
  explicit Window(std::unique_ptr<Impl> impl);

 public:
  // Construct a new Window with its top-left cell at `pos` with dimension `size`.
  Window(Dimension size, Position pos); // newwin

  // Disallow copying; allow moving a Window.
  Window(Window const&) = delete;
  Window& operator=(Window const&) = delete;
  Window(Window&&) noexcept;
  Window& operator=(Window&&) noexcept;

  // The destructor must be defined in the .cxx file because of the std::unique_ptr<Impl> with incomplete `Impl`.
  ~Window();

  // https://invisible-island.net/ncurses/man/curs_window.3x.html

  // Create a Window that is a subwindow of the current Window, with dimensions `size` and top-left screen position `pos`.
  //
  // The returned Window shares storage with this Window. The caller must keep parent
  // and child lifetimes ordered so the subwindow is destroyed before its parent.
  Window subwin(Dimension size, Position pos);                          // subwin

  // Create a Window that is a derived subwindow of the current Window, with dimensions `size` and top-left position `pos` relative to this Window.
  //
  // The returned Window shares storage with this Window. The caller must keep parent
  // and child lifetimes ordered so the subwindow is destroyed before its parent.
  Window derwin(Dimension size, Position pos);                          // derwin

  // Move this derived subwindow to `pos` (relative to its parent).
  //
  // The current Window must have been created with `derwin` and stay completely inside its parent window.
  void derwin(Position pos);                                            // mvderwin

  // Flatten the changed-cell bookkeeping, propagating all subwindow touches to the root Window.
  void syncup();                                                        // wsyncup

  // Enable or disable automatic syncup upon mutations.
  void syncok(bool enabled);                                            // syncok

  void set_background(ComplexChar background, bool erase = true);       // wbkgrndset / wbkgrnd
  ComplexChar get_background() const;                                   // wgetbkgrnd

  // https://invisible-island.net/ncurses/man/curs_move.3x.html

  void move(Position pos);                                              // wmove

  // https://invisible-island.net/ncurses/man/curs_outopts.3x.html

  void clearok(bool bf);                                                // clearok
  void idcok(bool bf);                                                  // idcok
  void idlok(bool bf);                                                  // idlok
  void immedok(bool bf);                                                // immedok
  void leaveok(bool bf);                                                // leaveok
  void scrollok(bool bf);                                               // scrollok
  void setscrreg(int top, int bot);                                     // wsetscrreg

  // https://invisible-island.net/ncurses/man/curs_clear.3x.html

  // Fill window with blanks.
  void erase();                                                         // werase

  // Same as erase, plus call to clearok.
  void clear();                                                         // wclear

  // Clear from cursor to the end of the screen.
  void clrtobot();                                                      // wclrtobot

  // Clear the cursor position and everything to the right.
  void clrtoeol();                                                      // wclrtoeol

  // https://invisible-island.net/ncurses/man/curs_refresh.3x.html

  // Copy the Window to the physical screen.
  //
  // This is done by first calling `wnoutrefresh`, followed by `Session::doupdate`.
  //
  // Unless `leaveok` has been enabled, the physical cursor of the
  // terminal is left at the location of the cursor this Window.
  void refresh();                                                       // wrefresh

  // Copy all touched lines from the Window to the virtual screen.
  void wnoutrefresh();                                                  // wnoutrefresh

  // Force a refresh of the entire window (in case of corruption of the screen).
  void redrawwin();                                                     // redrawwin
  // Force a refresh of specified lines.
  void wredrawln(int beg_line, int num_lines);                          // wredrawln

  // https://invisible-island.net/ncurses/man/curs_border_set.3x.html

  // Draw a border around the Window (does not affect the cursor).
  void set_border(Border const& border);                                // wborder_set

  // Add horizontal line after cursor.
  void hline_set(ComplexChar const& complex_char, int n);               // whline_set
  void hline_set(Position pos, ComplexChar const& complex_char, int n); // mvwhline_set

  // Add a vertical line below the cursor.
  void vline_set(ComplexChar const& complex_char, int n);               // wvline_set
  void vline_set(Position pos, ComplexChar const& complex_char, int n); // mvwvline_set

  // https://invisible-island.net/ncurses/man/curs_addstr.3x.html

  void addstr(char const* str);                                         // waddstr
  void addstr(char8_t const* str);                                      //

  void addstr(Position pos, char const* str);                           // mvwaddstr
  void addstr(Position pos, char8_t const* wstr);                       //

  void addstr(char const* str, int n);                                  // waddnstr
  void addstr(char8_t const* str, int n);                               //

  void addstr(Position pos, char const* str, int n);                    // mvwaddnstr
  void addstr(Position pos, char8_t const* str, int n);                 //

  // https://invisible-island.net/ncurses/man/curs_add_wch.3x.html

  void addch(ComplexChar const& complex_char);                          // wadd_wch
  void addch(Position pos, ComplexChar const& complex_char);            // mvwadd_wch

  void echochar(ComplexChar const& complex_char);                       // wecho_wchar

  // https://invisible-island.net/ncurses/man/curs_printw.3x.html

  int printw(char const* fmt, ...);                                     // wprintw
  int vprintw(char const* fmt, va_list varglist);                       // vw_printw
  int mvprintw(int y, int x, char const* fmt, ...);                     // mvprintw

};

} // namespace terminal
