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
  struct Impl;
  std::unique_ptr<Impl> impl_;

 private:
  // These are called before ncurses is initialized by the constructor of Session.
  friend class Session;
  Window();                     // Construct an uninitialized Window.
  void init_as_stdscr();        // Initialize a default constructed window with stdscr.

 public:
  // Construct a new Window with its top-left cell at `pos` with dimension `size`.
  Window(Dimension size, Position pos);                         // newwin

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
  // https://invisible-island.net/ncurses/man/curs_window.3x.html

  // Create or replace the writable subwindow inside this Window with dimension `size` and its top-left cell relative to this Window at `pos`.
  //
  // The subwindow shares ncurses storage with the parent Window, is owned by this Window, and is deleted before the parent.
  // Returns false when ncurses rejects the requested rectangle, leaving the Window without a subwindow.
  bool create_writable_subwindow(Dimension size, Position pos); // derwin

  // Remove the writable subwindow, if one exists.
  //
  // This does not erase terminal cells because ncurses subwindows share parent storage; subsequent writes fall back to the full parent Window.
  void delete_writable_subwindow();                             // delwin

  // Move the writable subwindow to `pos` relative to this Window.
  //
  // Returns false if there is no active subwindow or ncurses rejects the move.
  bool move_writable_subwindow(Position pos);                   // mvderwin

  // Propagate changed-cell bookkeeping from the writable subwindow to this Window.
  //
  // This is a no-op when no subwindow is active and is useful before refreshing the parent after subwindow writes.
  void sync_writable_subwindow_to_parent();                     // wsyncup

  // Enable or disable ncurses automatic touch synchronization from the writable subwindow to this Window.
  //
  // Returns false if there is no active subwindow or ncurses rejects the request.
  bool set_writable_subwindow_sync(bool enabled);               // syncok
};

} // namespace terminal
