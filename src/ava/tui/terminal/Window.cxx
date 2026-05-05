#include "Window.h"
#include "debug.h"

// This header must be included last.
#include "private_convert.h"

namespace terminal {

struct Window::Impl
{
  WINDOW* ncurses_window_;

  // Construct an Impl representing stdscr.
  Impl() : ncurses_window_(stdscr)
  {
  }

  Impl(Dimension size, Position pos)
  {
    // From https://docs.oracle.com/cd/E86824_01/html/E54767/newwin-3curses.html
    //
    // The newwin() routine creates and returns a pointer to a new window with the given number of lines, nlines, and columns, ncols.
    // The upper left-hand corner of the window is at line begin_y, column begin_x . If either nlines or ncols is zero, they default
    // to LINES — begin_y and COLS — begin_x. A new full-screen window is created by calling newwin(0,0,0,0).
    ncurses_window_ = ::newwin(size.height(), size.width(), pos.row(), pos.col());
  }

  ~Impl()
  {
    if (ncurses_window_ == stdscr)
      return;
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

  void wborder_set(std::array<cchar_t, 8> const& b)
  {
    ::wborder_set(ncurses_window_, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]);
  }

  void addstr(char const* str)
  {
    ::waddstr(ncurses_window_, str);
  }

  void addstr(char8_t const* utf8_str)
  {
    // Instead of using waddwstr, which would require application-side conversion from char8_t (utf8) to wchar_t,
    // it is better to just cast to `char const*` and let the terminal do that.
    ::waddstr(ncurses_window_, reinterpret_cast<char const*>(utf8_str));
  }

  void addstr(Position pos, char const* str)
  {
    ::mvwaddstr(ncurses_window_, pos.row(), pos.col(), str);
  }

  void addstr(Position pos, char8_t const* utf8_str)
  {
    // Instead of using mvwaddwstr, which would require application-side conversion from char8_t (utf8) to wchar_t,
    // it is better to just cast to `char const*` and let the terminal do that.
    ::mvwaddstr(ncurses_window_, pos.row(), pos.col(), reinterpret_cast<char const*>(utf8_str));
  }

  void addstr(char const* str, int n)
  {
    ::waddnstr(ncurses_window_, str, n);
  }

  void addstr(char8_t const* utf8_str, int n)
  {
    ::waddnstr(ncurses_window_, reinterpret_cast<char const*>(utf8_str), n);
  }

  void addstr(Position pos, char const* str, int n)
  {
    ::mvwaddnstr(ncurses_window_, pos.row(), pos.col(), str, n);
  }

  void addstr(Position pos, char8_t const* utf8_str, int n)
  {
    ::mvwaddnstr(ncurses_window_, pos.row(), pos.col(), reinterpret_cast<char const*>(utf8_str), n);
  }

  void addstr(cchar_t const* wchstr)
  {
    ::wadd_wchstr(ncurses_window_, wchstr);
  }

  void addstr(cchar_t const* wchstr, int n)
  {
    ::wadd_wchnstr(ncurses_window_, wchstr, n);
  }

  void addstr(Position pos, cchar_t const* wchstr)
  {
    ::mvwadd_wchstr(ncurses_window_, pos.row(), pos.col(), wchstr);
  }

  void addstr(Position pos, cchar_t const* wchstr, int n)
  {
    ::mvwadd_wchnstr(ncurses_window_, pos.row(), pos.col(), wchstr, n);
  }

  void addch(ComplexChar const& complex_char)
  {
    cchar_t wch = convert_to_cchar(complex_char);
    ::wadd_wch(ncurses_window_, &wch);
  }

  void addch(Position pos, ComplexChar const& complex_char)
  {
    cchar_t wch = convert_to_cchar(complex_char);
    ::mvwadd_wch(ncurses_window_, pos.row(), pos.col(), &wch);
  }

  void echochar(ComplexChar const& complex_char)
  {
    cchar_t wch = convert_to_cchar(complex_char);
    ::wecho_wchar(ncurses_window_, &wch);
  }

  void move(Position pos)
  {
    // https://invisible-island.net/ncurses/man/curs_move.3x.html
    //
    // wmove relocates the cursor associated with the curses window win to
    // line y and column x. The terminal's cursor does not move until
    // refresh(3x) is called. The position (y, x) is relative to the upper
    // left-hand corner of the window, which has coordinates (0, 0).
    ::wmove(ncurses_window_, pos.row(), pos.col());
  }
};

Window::Window()
{
}

void Window::init_as_stdscr()
{
  // Only call this function once and only on a default constructed Window. This should only be called from Session().
  ASSERT(!impl_);
  impl_ = std::make_unique<Impl>();
}

Window::Window(Dimension size, Position pos) : impl_(std::make_unique<Impl>(size, pos))
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
    ::wbkgrndset(impl_->ncurses_window_, &wch);
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
    ::wbkgrnd(impl_->ncurses_window_, &wch);
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

void Window::set_border(Border const& border)
{
  ComplexChar const background = get_background();
  std::array<cchar_t, 8> complex_characters;
  for (int i = 0; i < 8; ++i)
    complex_characters[i] = convert_to_cchar(border.get_complex_character(i, background.rendition()));
  impl_->wborder_set(complex_characters);
}

void Window::addstr(char const* str)
{
  impl_->addstr(str);
}

void Window::addstr(char8_t const* wstr)
{
  impl_->addstr(wstr);
}

void Window::addstr(Position pos, char const* str)
{
  impl_->addstr(pos, str);
}

void Window::addstr(Position pos, char8_t const* wstr)
{
  impl_->addstr(pos, wstr);
}

void Window::addstr(char const* str, int n)
{
  impl_->addstr(str, n);
}

void Window::addstr(char8_t const* wstr, int n)
{
  impl_->addstr(wstr, n);
}

void Window::addstr(Position pos, char const* str, int n)
{
  impl_->addstr(pos, str, n);
}

void Window::addstr(Position pos, char8_t const* wstr, int n)
{
  impl_->addstr(pos, wstr, n);
}

void Window::addch(ComplexChar const& complex_char)
{
  impl_->addch(complex_char);
}

void Window::addch(Position pos, ComplexChar const& complex_char)
{
  impl_->addch(pos, complex_char);
}

void Window::echochar(ComplexChar const& complex_char)
{
  impl_->echochar(complex_char);
}

void Window::move(Position pos)
{
  impl_->move(pos);
}

} // namespace terminal
