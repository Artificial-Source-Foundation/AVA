#pragma once

#include "Border.h"
#include "ComplexChar.h"
#include "Dimension.h"
#include "Margin.h"
#include "Position.h"

#include <cstdarg>
#include <cwchar>
#include <memory>
#include <optional>
#include <string>

// To print all implemented ncurses functions (from the comments):
//
// grep -E '^ *(void|Window ).*// [a-z_]' Window.h | sed -e 's/^.*\/\/ //;s/ \/ /,/' | tr ',' '\n' | sort -u
//
// WINDOW related ncurses functions:
//
// grep '^extern.*WINDOW *\*.*implemented' /usr/include/curses.h | grep -v SCREEN | sed -re 's/^extern NCURSES_EXPORT\([^)]*\) ([^ (]*).*/\1/' | sort -u
//
namespace ava::tui::terminal {

// Forward declaration.
class Session;

// Inclusive top and bottom rows of a Window scrolling region.
struct ScrollRegion
{
  int top;
  int bottom;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

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

  // Construct a new off-screen pad with dimension `size`; pads require explicit pad refresh rectangles.
  static Window newpad(Dimension size); // newpad

  // Disallow copying; allow moving a Window.
  Window(Window const&) = delete;
  Window& operator=(Window const&) = delete;
  Window(Window&&) noexcept;
  Window& operator=(Window&&) noexcept;

  // The destructor must be defined in the .cxx file because of the std::unique_ptr<Impl> with incomplete `Impl`.
  ~Window();

  // https://invisible-island.net/ncurses/man/curs_window.3x.html

  // Create a Window that is a subwindow of the current Window.
  //
  // Either pass dimension `size` and top-left screen position `pos`,
  // or pass a Margin that is relative to this Window.
  //
  // The returned Window shares storage with this Window. The caller must keep parent
  // and child lifetimes ordered so the subwindow is destroyed before its parent.
  Window subwin(Dimension size, Position pos);                          // subwin
  Window subwin(Margin margin);                                         //

  // Create a Window that is a derived subwindow of the current Window.
  //
  // Either pass dimension `size` and top-left position `pos` relative to this Window,
  // or pass a Margin that is relative to this Window.
  //
  // The returned Window shares storage with this Window. The caller must keep parent
  // and child lifetimes ordered so the subwindow is destroyed before its parent.
  Window derwin(Dimension size, Position pos);                          // derwin
  Window derwin(Margin margin);                                         //

  // Move this derived subwindow to `pos` (relative to its parent).
  //
  // The current Window must have been created with `derwin` and stay completely inside its parent window.
  void derwin(Position pos);                                            // mvderwin

  // Flatten the changed-cell bookkeeping, propagating all subwindow touches to the root Window.
  void syncup();                                                        // wsyncup

  // Propagate this Window cursor position to all ancestor windows.
  void cursyncup();                                                     // wcursyncup

  // Enable or disable automatic syncup upon mutations.
  void syncok(bool enabled);                                            // syncok

  // https://invisible-island.net/ncurses/man/curs_bkgrnd.3x.html

  void set_background(ComplexChar background, bool erase = true);       // wbkgrndset / wbkgrnd
  ComplexChar get_background() const;                                   // wgetbkgrnd

  // https://invisible-island.net/ncurses/man/curs_attr.3x.html

  // Set the rendition used for subsequent output without changing existing cells.
  void attr_set(Rendition rendition);                                   // wattr_set
  // Return the rendition currently used for subsequent output through `rendition`.
  void attr_get(Rendition& rendition) const;                            // wattr_get
  // Add `attributes` to the current output rendition.
  void attr_on(Attributes attributes);                                  // wattr_on
  // Remove `attributes` from the current output rendition.
  void attr_off(Attributes attributes);                                 // wattr_off
  // Change only the color pair used for subsequent output.
  void color_set(ColorPair color_pair);                                 // wcolor_set
  // Change rendition over `n` cells at the cursor, or to end of line when `n` is negative.
  void chgat(int n, Rendition rendition);                               // wchgat
  // Move to `pos` and change rendition over `n` cells, or to end of line when `n` is negative.
  void chgat(Position pos, int n, Rendition rendition);                 // mvwchgat
  // Turn on terminal standout mode for subsequent output.
  void standout();                                                      // wstandout
  // Turn off standout and return subsequent output to normal rendition.
  void standend();                                                      // wstandend

  // https://invisible-island.net/ncurses/man/curs_move.3x.html

  void move(Position pos);                                              // wmove

  // https://invisible-island.net/ncurses/man/wresize.3x.html

  void resize(Dimension size);                                          // wresize

  // https://invisible-island.net/ncurses/man/curs_getyx.3x.html

  // Return the cursor position relative to this Window's top-left corner.
  Position getyx() const;                                               // getyx
  // Return this Window's top-left position in screen coordinates.
  Position getbegyx() const;                                            // getbegyx
  // Return this Window's current dimensions in rows and columns.
  Dimension getmaxyx() const;                                           // getmaxyx
  // Return this subwindow's parent-relative origin, or no value when it has no parent.
  std::optional<Position> getparyx() const;                             // getparyx

  // https://invisible-island.net/ncurses/man/curs_mouse.3x.html

  // Return whether screen-relative `pos` lies within this Window.
  bool enclose(Position pos) const;                                     // wenclose
  // Convert `pos` between Window-local and screen coordinates; false means no valid conversion exists.
  bool mouse_trafo(Position& pos, bool to_screen) const;                // wmouse_trafo

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

#if 0
  // https://invisible-island.net/ncurses/man/curs_add_wchstr.3x.html

  void addstr(ComplexChar const* str);                                  // wadd_wchstr
  // Add at most `n` complex characters without advancing the cursor.
  void addstr(ComplexChar const* str, int n);                           // wadd_wchnstr
  void addstr(Position pos, ComplexChar const* str);                    // mvwadd_wchstr
  // Move to `pos` and add at most `n` complex characters without advancing the cursor.
  void addstr(Position pos, ComplexChar const* str, int n);             // mvwadd_wchnstr
#endif

  // https://invisible-island.net/ncurses/man/curs_addwstr.3x.html

  void addstr(wchar_t const* str);                                      // waddwstr
  // Add at most `n` wide characters, stopping early at a null wide character.
  void addstr(wchar_t const* str, int n);                               // waddnwstr
  void addstr(Position pos, wchar_t const* str);                        // mvwaddwstr
  // Move to `pos` and add at most `n` wide characters.
  void addstr(Position pos, wchar_t const* str, int n);                 // mvwaddnwstr

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

  // https://invisible-island.net/ncurses/man/curs_delch.3x.html

  // Delete the character under the cursor and shift the rest of the line left.
  void delch();                                                         // wdelch
  // Move to `pos`, delete that character, and shift the rest of the line left.
  void delch(Position pos);                                             // mvwdelch

  // https://invisible-island.net/ncurses/man/curs_deleteln.3x.html

  // Insert positive or delete negative line counts at the cursor line.
  void insdelln(int n);                                                 // winsdelln

  // https://invisible-island.net/ncurses/man/curs_get_wch.3x.html

  // Read one wide character or function-key code from this Window into `key`.
  void get_wch(wint_t& key);                                            // wget_wch
  // Push `key` back onto the ncurses input queue for the next read.
  static void unget_wch(wchar_t key);                                   // unget_wch

  // https://invisible-island.net/ncurses/man/curs_in_wch.3x.html

  // Read the complex character under the cursor into `complex_char` without changing the Window.
  void in_wch(ComplexChar& complex_char) const;                         // win_wch
  // Move to `pos` and read that complex character into `complex_char`.
  void in_wch(Position pos, ComplexChar& complex_char) const;           // mvwin_wch

  // https://invisible-island.net/ncurses/man/curs_in_wchstr.3x.html

  // Read complex characters from the cursor into `str` until end of line.
  void instr(ComplexChar* str) const;                                   // win_wchstr
  // Read at most `n` complex characters from the cursor into `str`.
  void instr(ComplexChar* str, int n) const;                            // win_wchnstr
  // Move to `pos` and read complex characters into `str` until end of line.
  void instr(Position pos, ComplexChar* str) const;                     // mvwin_wchstr
  // Move to `pos` and read at most `n` complex characters into `str`.
  void instr(Position pos, ComplexChar* str, int n) const;              // mvwin_wchnstr

  // https://invisible-island.net/ncurses/man/curs_ins_wch.3x.html

  // Insert `complex_char` before the cursor, shifting the line right.
  void ins_wch(ComplexChar const& complex_char);                        // wins_wch
  // Move to `pos` and insert `complex_char` before that cell.
  void ins_wch(Position pos, ComplexChar const& complex_char);          // mvwins_wch

  // https://invisible-island.net/ncurses/man/curs_ins_wstr.3x.html

  // Insert a null-terminated wide string before the cursor, shifting cells right.
  void insstr(wchar_t const* str);                                      // wins_wstr
  // Insert at most `n` wide characters before the cursor, shifting cells right.
  void insstr(wchar_t const* str, int n);                               // wins_nwstr
  // Move to `pos` and insert a null-terminated wide string before that cell.
  void insstr(Position pos, wchar_t const* str);                        // mvwins_wstr
  // Move to `pos` and insert at most `n` wide characters before that cell.
  void insstr(Position pos, wchar_t const* str, int n);                 // mvwins_nwstr

  // https://invisible-island.net/ncurses/man/curs_insstr.3x.html

  // Insert a null-terminated narrow/UTF-8 string before the cursor.
  void insstr(char const* str);                                         // winsstr
  // Insert at most `n` narrow/UTF-8 bytes before the cursor.
  void insstr(char const* str, int n);                                  // winsnstr
  // Move to `pos` and insert a null-terminated narrow/UTF-8 string.
  void insstr(Position pos, char const* str);                           // mvwinsstr
  // Move to `pos` and insert at most `n` narrow/UTF-8 bytes.
  void insstr(Position pos, char const* str, int n);                    // mvwinsnstr

  // https://invisible-island.net/ncurses/man/curs_inwstr.3x.html

  // Extract wide characters from the cursor into `str` until end of line.
  void inwstr(wchar_t* str) const;                                      // winwstr
  // Extract at most `n` wide characters from the cursor into `str`.
  void inwstr(wchar_t* str, int n) const;                               // winnwstr
  // Move to `pos` and extract wide characters into `str` until end of line.
  void inwstr(Position pos, wchar_t* str) const;                        // mvwinwstr
  // Move to `pos` and extract at most `n` wide characters into `str`.
  void inwstr(Position pos, wchar_t* str, int n) const;                 // mvwinnwstr

  // https://invisible-island.net/ncurses/man/curs_kernel.3x.html

  // Set terminal cursor visibility; `visibility` follows ncurses curs_set values.
  static void curs_set(int visibility);                                 // curs_set

  // https://invisible-island.net/ncurses/man/curs_printw.3x.html

  void printw(char const* fmt, ...);                                    // wprintw
  void vprintw(char const* fmt, va_list varglist);                      // vw_printw
  void printw(Position pos, char const* fmt, ...);                      // mvwprintw

  // https://invisible-island.net/ncurses/man/curs_pad.3x.html

  // Create a pad subwindow of `size` with top-left position `pos` relative to this pad.
  Window subpad(Dimension size, Position pos);                          // subpad
  // Refresh a pad rectangle starting at `pad_pos` into a screen rectangle.
  void prefresh(Position pad_pos, Position screen_pos, Dimension screen_size); // prefresh
  // Stage a pad rectangle starting at `pad_pos` into a screen rectangle without updating the terminal.
  void pnoutrefresh(Position pad_pos, Position screen_pos, Dimension screen_size); // pnoutrefresh
  // Add `complex_char` to a pad and refresh the pad using ncurses' remembered pad viewport.
  void pechochar(ComplexChar const& complex_char);                      // pecho_wchar

  // https://invisible-island.net/ncurses/man/curs_scroll.3x.html

  // Scroll the Window up for positive `n` or down for negative `n` lines.
  void scrl(int n);                                                     // wscrl

  // https://invisible-island.net/ncurses/man/curs_util.3x.html

  // Store the printable name for `key` in `name`; function-key codes are supported.
  static void key_name(wint_t key, std::string& name);                  // key_name

  // https://invisible-island.net/ncurses/man/curs_opaque.3x.html

  bool is_cleared() const;                                              // is_cleared
  bool is_idcok() const;                                                // is_idcok
  bool is_idlok() const;                                                // is_idlok
  bool is_immedok() const;                                              // is_immedok
  bool is_keypad() const;                                               // is_keypad
  bool is_leaveok() const;                                              // is_leaveok
  bool is_nodelay() const;                                              // is_nodelay
  bool is_notimeout() const;                                            // is_notimeout
  bool is_pad() const;                                                  // is_pad
  bool is_scrollok() const;                                             // is_scrollok
  bool is_subwin() const;                                               // is_subwin
  bool is_syncok() const;                                               // is_syncok
  // Return this Window's input delay in milliseconds, or ncurses sentinel values for blocking/nonblocking modes.
  int getdelay() const;                                                 // wgetdelay
  // Return a non-owning wrapper for this Window's parent, or null when it has no parent.
  // Commented out because this requires a central registry of Window objects that we don't have (yet).
// std::optional<Window> getparent() const;                              // wgetparent
  //  Return this Window's inclusive scrolling-region row bounds.
  ScrollRegion getscrreg() const;                                      // wgetscrreg

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
