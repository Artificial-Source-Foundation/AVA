#include "Window.h"

#include <utility>

#include "debug.h"

// This header must be included last.
#include "private_convert.h"

namespace terminal {

struct Window::Impl {
 private:
  WINDOW* handle_;

 private:
  void default_window_initialization()
  {
    int res;
    res = ::keypad(handle_, TRUE);
    ASSERT(res == OK);
    // Block on calls to get_wch: use a dedicated thread to get input.
    res = ::nodelay(handle_, FALSE);
    ASSERT(res == OK);
    // Wait after seeing an ESC for more characters to allow a keyboard to send a full escape sequence.
    res = ::notimeout(handle_, FALSE);
    ASSERT(res == OK);
  }

 public:
  // Construct an Impl representing stdscr.
  Impl() : handle_(stdscr) { default_window_initialization(); }

  // Wrap an ncurses WINDOW handle returned by a window-creation function.
  // The pointer must be non-null and is owned by this Impl, except for stdscr which is owned by ncurses itself.
  explicit Impl(WINDOW* handle) : handle_(handle) { ASSERT(handle_); }

  Impl(Dimension size, Position pos)
  {
    // https://invisible-island.net/ncurses/man/curs_window.3x.html
    //
    // Calling newwin creates and returns a pointer to a new window with the given number of lines and columns. The
    // upper left-hand corner of the window is at line begin_y, column begin_x. If either nlines or ncols is zero,
    // they default to LINES - begin_y and COLS - begin_x.
    handle_ = ::newwin(size.height(), size.width(), pos.row(), pos.col());
    ASSERT(handle_);
    default_window_initialization();
  }

  ~Impl()
  {
    if (handle_ == stdscr)
      return;
    // https://invisible-island.net/ncurses/man/curs_window.3x.html
    //
    // Calling delwin deletes the named window, freeing all memory associated with it. Subwindows must be deleted
    // before the main window can be deleted.
    ::delwin(handle_);
  }

  WINDOW* subwin(Dimension size, Position pos)
  {
    // https://invisible-island.net/ncurses/man/curs_window.3x.html
    //
    // Calling subwin creates and returns a pointer to a new window with the given number of lines and columns. The
    // window is at position (begin_y, begin_x) on the screen. The subwindow shares memory with the window orig, its
    // ancestor, so changes made to one window will affect both windows.
    return ::subwin(handle_, size.height(), size.width(), pos.row(), pos.col());
  }

  WINDOW* derwin(Dimension size, Position pos)
  {
    // https://invisible-island.net/ncurses/man/curs_window.3x.html
    //
    // Calling derwin is the same as calling subwin, except that begin_y and begin_x are relative to the origin of the
    // window orig rather than the screen. There is no difference between the subwindows and the derived windows.
    return ::derwin(handle_, size.height(), size.width(), pos.row(), pos.col());
  }

  int derwin(Position pos)
  {
    // https://invisible-island.net/ncurses/man/curs_window.3x.html
    //
    // Calling mvderwin moves a derived window (or subwindow) inside its parent window. The screen-relative parameters
    // of the window are not changed. This routine is used to display different parts of the parent window at the same
    // physical position on the screen.
    return ::mvderwin(handle_, pos.row(), pos.col());
  }

  void syncup()
  {
    // https://invisible-island.net/ncurses/man/curs_window.3x.html
    //
    // Calling wsyncup touches all locations in ancestors of win that are changed in win. If syncok is called with
    // second argument TRUE then wsyncup is called automatically whenever there is a change in the window.
    ::wsyncup(handle_);
  }

  int syncok(bool enabled)
  {
    // https://invisible-island.net/ncurses/man/curs_window.3x.html
    //
    // If syncok is called with second argument TRUE then wsyncup is called automatically whenever there is a change in
    // the window.
    return ::syncok(handle_, enabled);
  }

  void erase()
  {
    // https://invisible-island.net/ncurses/man/curs_clear.3x.html
    //
    // The erase and werase routines copy blanks to every position in the window, clearing the screen.
    ::werase(handle_);
  }

  void refresh()
  {
    // https://invisible-island.net/ncurses/man/curs_refresh.3x.html
    //
    // The refresh and wrefresh routines (or wnoutrefresh and doupdate) must be called to get any output on the
    // terminal, as other routines merely manipulate data structures. The routine wrefresh copies the named window to
    // the physical terminal screen.
    ::wrefresh(handle_);
  }

  void border_set(std::array<cchar_t, 8> const& b)
  {
    // https://invisible-island.net/ncurses/man/curs_border_set.3x.html
    //
    // The border_set and wborder_set functions draw a border around the edges of the current or specified window.
    ::wborder_set(handle_, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]);
  }

  int hline_set(ComplexChar const& complex_char, int n)
  {
    // https://invisible-island.net/ncurses/man/curs_border_set.3x.html
    //
    // whline_set draws up to n horizontal line cells starting at the cursor without moving the cursor.
    cchar_t wch = convert_to_cchar(complex_char);
    return ::whline_set(handle_, &wch, n);
  }

  int hline_set(Position pos, ComplexChar const& complex_char, int n)
  {
    // https://invisible-island.net/ncurses/man/curs_border_set.3x.html
    //
    // mvwhline_set first moves the cursor, then draws up to n horizontal line cells without moving the cursor.
    cchar_t wch = convert_to_cchar(complex_char);
    return ::mvwhline_set(handle_, pos.row(), pos.col(), &wch, n);
  }

  int vline_set(ComplexChar const& complex_char, int n)
  {
    // https://invisible-island.net/ncurses/man/curs_border_set.3x.html
    //
    // wvline_set draws up to n vertical line cells downward from the cursor without moving the cursor.
    cchar_t wch = convert_to_cchar(complex_char);
    return ::wvline_set(handle_, &wch, n);
  }

  int vline_set(Position pos, ComplexChar const& complex_char, int n)
  {
    // https://invisible-island.net/ncurses/man/curs_border_set.3x.html
    //
    // mvwvline_set first moves the cursor, then draws up to n vertical line cells without moving the cursor.
    cchar_t wch = convert_to_cchar(complex_char);
    return ::mvwvline_set(handle_, pos.row(), pos.col(), &wch, n);
  }

  void set_background(ComplexChar background, bool erase)
  {
    cchar_t const wch = convert_to_cchar(background);
    if (erase)
    {
      // https://invisible-island.net/ncurses/man/curs_bkgrnd.3x.html
      //
      // The wbkgrndset function manipulates the background of the named window. The background becomes a property of
      // the character and moves with the character through any scrolling and insert/delete line/character operations.
      ::wbkgrndset(handle_, &wch);
      this->erase();
    }
    else
    {
      // https://invisible-island.net/ncurses/man/curs_bkgrnd.3x.html
      //
      // The wbkgrnd function turns off the previous background attributes, logically ORs the requested attributes into
      // the window rendition, and applies this setting to every character position in that window.
      ::wbkgrnd(handle_, &wch);
    }
  }

  ComplexChar get_background() const
  {
    // https://invisible-island.net/ncurses/man/curs_bkgrnd.3x.html
    //
    // The getbkgrnd and wgetbkgrnd functions obtain the window's current background character and rendition.
    cchar_t background;
    ::wgetbkgrnd(handle_, &background);
    return convert_to_ComplexChar(background);
  }

  void addstr(char const* str)
  {
    // https://invisible-island.net/ncurses/man/curs_addstr.3x.html
    //
    // The addstr, addnstr, waddstr, and waddnstr routines write all characters of the null-terminated string str on
    // the given window. The n variants write at most n characters.
    ::waddstr(handle_, str);
  }

  void addstr(char8_t const* utf8_str)
  {
    // Instead of using waddwstr, which would require application-side conversion from char8_t (utf8) to wchar_t,
    // it is better to just cast to `char const*` and let the terminal do that.
    ::waddstr(handle_, reinterpret_cast<char const*>(utf8_str));
  }

  void addstr(Position pos, char const* str) { ::mvwaddstr(handle_, pos.row(), pos.col(), str); }

  void addstr(Position pos, char8_t const* utf8_str)
  {
    // Instead of using mvwaddwstr, which would require application-side conversion from char8_t (utf8) to wchar_t,
    // it is better to just cast to `char const*` and let the terminal do that.
    ::mvwaddstr(handle_, pos.row(), pos.col(), reinterpret_cast<char const*>(utf8_str));
  }

  void addstr(char const* str, int n) { ::waddnstr(handle_, str, n); }

  void addstr(char8_t const* utf8_str, int n) { ::waddnstr(handle_, reinterpret_cast<char const*>(utf8_str), n); }

  void addstr(Position pos, char const* str, int n) { ::mvwaddnstr(handle_, pos.row(), pos.col(), str, n); }

  void addstr(Position pos, char8_t const* utf8_str, int n)
  {
    ::mvwaddnstr(handle_, pos.row(), pos.col(), reinterpret_cast<char const*>(utf8_str), n);
  }

  void addstr(cchar_t const* wchstr)
  {
    // https://invisible-island.net/ncurses/man/curs_add_wchstr.3x.html
    //
    // The wadd_wchstr functions copy the array of complex characters into the window image structure at and after the
    // cursor position. The four functions with n as the last argument copy at most n elements.
    ::wadd_wchstr(handle_, wchstr);
  }

  void addstr(cchar_t const* wchstr, int n) { ::wadd_wchnstr(handle_, wchstr, n); }

  void addstr(Position pos, cchar_t const* wchstr) { ::mvwadd_wchstr(handle_, pos.row(), pos.col(), wchstr); }

  void addstr(Position pos, cchar_t const* wchstr, int n) { ::mvwadd_wchnstr(handle_, pos.row(), pos.col(), wchstr, n); }

  void addch(ComplexChar const& complex_char)
  {
    // https://invisible-island.net/ncurses/man/curs_add_wch.3x.html
    //
    // The wadd_wch function places the complex character at the current cursor position of the specified window, then
    // advances the cursor position.
    cchar_t wch = convert_to_cchar(complex_char);
    ::wadd_wch(handle_, &wch);
  }

  void addch(Position pos, ComplexChar const& complex_char)
  {
    cchar_t wch = convert_to_cchar(complex_char);
    ::mvwadd_wch(handle_, pos.row(), pos.col(), &wch);
  }

  void echochar(ComplexChar const& complex_char)
  {
    // https://invisible-island.net/ncurses/man/curs_add_wch.3x.html
    //
    // The wecho_wchar function is functionally equivalent to calling wadd_wch followed by wrefresh.
    cchar_t wch = convert_to_cchar(complex_char);
    ::wecho_wchar(handle_, &wch);
  }

  void move(Position pos)
  {
    // https://invisible-island.net/ncurses/man/curs_move.3x.html
    //
    // wmove relocates the cursor associated with the curses window win to
    // line y and column x. The terminal's cursor does not move until
    // refresh(3x) is called. The position (y, x) is relative to the upper
    // left-hand corner of the window, which has coordinates (0, 0).
    ::wmove(handle_, pos.row(), pos.col());
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

Window::Window(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}

Window::Window(Window&&) noexcept = default;

Window& Window::operator=(Window&&) noexcept = default;

Window::~Window()
{
}

Window Window::subwin(Dimension size, Position pos)
{
  return Window(std::make_unique<Impl>(impl_->subwin(size, pos)));
}

Window Window::derwin(Dimension size, Position pos)
{
  return Window(std::make_unique<Impl>(impl_->derwin(size, pos)));
}

void Window::derwin(Position pos)
{
  int res = impl_->derwin(pos);
  ASSERT(res != ERR);
}

void Window::syncup()
{
  impl_->syncup();
}

void Window::syncok(bool enabled)
{
  int res = impl_->syncok(enabled);
  ASSERT(res != ERR);
}

void Window::set_background(ComplexChar background, bool erase)
{
  impl_->set_background(background, erase);
}

ComplexChar Window::get_background() const
{
  return impl_->get_background();
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
  impl_->border_set(complex_characters);
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
