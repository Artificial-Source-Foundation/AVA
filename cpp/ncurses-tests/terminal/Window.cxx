#include "Window.h"

// This header must be included last.
#include "private_convert.h"

namespace terminal {

struct Window::Impl
{
  WINDOW* ncurses_window_;

  Impl(int nlines, int ncols, int begin_y, int begin_x)
  {
    // From https://docs.oracle.com/cd/E86824_01/html/E54767/newwin-3curses.html
    //
    // The newwin() routine creates and returns a pointer to a new window with the given number of lines, nlines, and columns, ncols.
    // The upper left-hand corner of the window is at line begin_y, column begin_x . If either nlines or ncols is zero, they default
    // to LINES — begin_y and COLS — begin_x. A new full-screen window is created by calling newwin(0,0,0,0).
    ncurses_window_ = ::newwin(nlines, ncols, begin_y, begin_x);
  }

  ~Impl()
  {
    // From https://docs.oracle.com/cd/E86824_01/html/E54767/delwin-3xcurses.html
    //
    // The delwin() function deletes the specified window, freeing up the memory associated with it.
    // Deleting a parent window without deleting its subwindows and then trying to manipulate the subwindows will have undefined results.
    ::delwin(ncurses_window_);
  }

  void erase()
  {
    // See https://docs.oracle.com/cd/E88353_01/html/E37849/werase-3curses.html
    //
    // The werase() routine copy blanks to every position in the window.
    ::werase(ncurses_window_);
  }

  void refresh()
  {
    // From https://docs.oracle.com/cd/E88353_01/html/E37849/wrefresh-3curses.html
    //
    // The refresh() and wrefresh() routines (or wnoutrefresh() and doupdate()) must be called to get any output on the terminal,
    // as other routines merely manipulate data structures. The routine wrefresh() copies the named window to the physical terminal screen,
    // taking into account what is already there in order to do optimizations. The refresh() routine is the same, using stdscr as the default window.
    // Unless leaveok() has been enabled, the physical cursor of the terminal is left at the location of the cursor for that window.
    ::wrefresh(ncurses_window_);
  }

  void wborder(chtype left_side, chtype right_side, chtype top_side, chtype bottom_side, chtype top_left, chtype top_right, chtype bottom_left, chtype bottom_right)
  {
    // See https://docs.oracle.com/cd/E88353_01/html/E37849/wborder-3xcurses.html
    //
    // With the wborder() routine, a border is drawn around the edges of the window.
    // +------------------------------------------------------+
    // | Parameter       Default constant    Default character|
    // | left_side       ACS_VLINE           |                |
    // | right_side      ACS_VLINE           |                |
    // | top_side        ACS_HLINE           -                |
    // | bottom_side     ACS_HLINE           -                |
    // | bottom_left     ACS_BLCORNER        +                |
    // | bottom_right    ACS_BRCORNER        +                |
    // | top_left        ACS_ULCORNER        +                |
    // | top_right       ACS_URCORNER        +                |
    // +------------------------------------------------------+
    ::wborder(ncurses_window_, left_side, right_side, top_side, bottom_side, top_left, top_right, bottom_left, bottom_right);
  }

  void addstr(int y, int x, char const* str)
  {
    // From https://docs.oracle.com/cd/E88353_01/html/E37849/mvwaddstr-3xcurses.html
    //
    // The addstr() function writes a null-terminated string of multibyte characters to the stdscr window at the current cursor position.
    // The waddstr() function performs an identical action, but writes the character to the window specified by win.
    // The mvaddstr() and mvwaddstr() functions write the string to the position indicated by the x (column) and y (row) parameters
    // (the former to the stdscr window; the latter to window win).
    ::mvwaddstr(ncurses_window_, y, x, str);
  }

  void mvwadd_wchstr(int y, int x, cchar_t const* wchstr)
  {
    // From https://docs.oracle.com/cd/E88353_01/html/E37849/mvwadd-wchstr-3xcurses.html
    //
    // The add_wchstr() function copies the string of cchar_t characters to the stdscr window at the current cursor position.
    // The mvadd_wchstr() and mvwadd_wchstr() functions copy the string to the starting position indicated by the x (column)
    // and y (row) parameters (the former to the stdscr window; the latter to window win). The wadd_wchstr() is identical to
    // add_wchstr (), but writes to the window specified by win.
    ::mvwadd_wchstr(ncurses_window_, y, x, wchstr);
  }

  void addstr(int y, int x, char8_t const* utf8_str)
  {
    // Instead of using mvwaddwstr, which would require application-side conversion from char8_t (utf8) to wchar_t,
    // it is better to just cast to `char const*` and let the terminal do that.
    ::mvwaddstr(ncurses_window_, y, x, reinterpret_cast<char const*>(utf8_str));
  }
};

Window::Window(int height, int width, int y, int x) : impl_(std::make_unique<Impl>(height, width, y, x))
{
}

Window::~Window()
{
}

void Window::set_background(ComplexChar background, bool erase)
{
  cchar_t const wch = convert_to_cchar(background);
  if (erase)
  {
    // See https://docs.oracle.com/cd/E86824_01/html/E54767/wbkgrndset-3xcurses.html
    //
    // The wbkgrndset() function turns off the previous background attributes, logical OR the requested attributes into the window rendition,
    // and sets the background property of the current or specified window based on the information in cchar of the second parameter.
    wbkgrndset(impl_->ncurses_window_, &wch);
    impl_->erase();
  }
  else
  {
    // See https://docs.oracle.com/cd/E88353_01/html/E37849/wbkgrnd-3xcurses.html
    //
    // The bkgrnd() and wbkgrnd() functions turn off the previous background attributes, logical OR the requested attributes into the window rendition,
    // and set the background property of the current or specified window and then apply this setting to every character position in that window:
    //
    // * The rendition of every character on the screen is changed to the new window rendition.
    // * Wherever the former background character appears, it is changed to the new background character.
    wbkgrnd(impl_->ncurses_window_, &wch);
  }
}

ComplexChar Window::get_background() const
{
  cchar_t background;
  ::wgetbkgrnd(impl_->ncurses_window_, &background);
  return convert_to_ComplexChar(background);
}

void Window::erase()
{
  impl_->erase();
}

void Window::refresh()
{
  impl_->refresh();
}

void Window::box(int verch, int horch)
{
  // From https://docs.oracle.com/cd/E88353_01/html/E37849/box-3curses.html
  //
  // box(win, verch, horch) is a shorthand for the following call:
  impl_->wborder(verch, verch, horch, horch, 0, 0, 0, 0);
}

void Window::addstr(int y, int x, char const* str)
{
  impl_->addstr(y, x, str);
}

void Window::addstr(int y, int x, char8_t const* wstr)
{
  impl_->addstr(y, x, wstr);
}

} // namespace terminal
