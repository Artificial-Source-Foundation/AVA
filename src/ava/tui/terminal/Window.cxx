#include "Window.h"
#include "debug.h"

// This header must be included last.
#include "private_convert.h"

namespace terminal {

struct Window::Impl
{
  // We store a new window initially in active_window_.
  // If a subwindow is created, then active_window_ is stored in parent_window_ and the new subwindow is stored in active_window_.
  WINDOW* active_window_;
  WINDOW* parent_window_ = nullptr;

  // Return the ncurses window that owns this Window's full rectangle.
  // If a subwindow is active, this is the parent returned by newwin; otherwise it is the active window itself. Use
  // this for operations, such as border drawing and whole-window refreshes, whose semantics include margin cells.
  WINDOW* outer_window() const
  {
    return parent_window_ ? parent_window_ : active_window_;
  }

  // Construct an Impl representing stdscr.
  Impl() : active_window_(stdscr)
  {
  }

  Impl(Dimension size, Position pos)
  {
    // From https://docs.oracle.com/cd/E86824_01/html/E54767/newwin-3curses.html
    //
    // The newwin() routine creates and returns a pointer to a new window with the given number of lines, nlines, and columns, ncols.
    // The upper left-hand corner of the window is at line begin_y, column begin_x . If either nlines or ncols is zero, they default
    // to LINES — begin_y and COLS — begin_x. A new full-screen window is created by calling newwin(0,0,0,0).
    active_window_ = ::newwin(size.height(), size.width(), pos.row(), pos.col());
  }

  ~Impl()
  {
    delete_subwindow();
    if (active_window_ == stdscr)
      return;
    // From https://docs.oracle.com/cd/E86824_01/html/E54767/delwin-3xcurses.html
    //
    // The delwin() function deletes the specified window, freeing up the memory associated with it.
    // Deleting a parent window without deleting its subwindows and then trying to manipulate the subwindows will have undefined results.
    ::delwin(active_window_);
  }

  // Return true if this Window has a subwindow.
  bool is_subwindow() const
  {
    return parent_window_ != nullptr;
  }

  // Create or replace the derived ncurses subwindow used for margin-aware writes.
  // `size` is the writable area's height and width, and `pos` is the writable area's top-left cell relative to
  // active_window_. The old subwindow is deleted first because ncurses requires subwindows to be destroyed before
  // their parent; if derwin fails, no subwindow remains and write operations fall back to the parent window.
  // The returned pointer is owned by this Impl and is valid until the next create_subwindow, delete_subwindow, or
  // destructor call.
  WINDOW* create_subwindow(Dimension size, Position pos)
  {
    delete_subwindow();
    WINDOW* const subwindow = ::derwin(active_window_, size.height(), size.width(), pos.row(), pos.col());
    if (!subwindow)
      return nullptr;
    parent_window_ = active_window_;
    active_window_ = subwindow;
    return subwindow;
  }

  // Delete the currently installed derived subwindow, if any.
  // This releases only the subwindow object; ncurses subwindows share backing storage with their parent, so screen
  // contents are not erased and the parent remains valid. The method is idempotent to make margin reset and
  // destructor paths safe to call even when no subwindow was created.
  void delete_subwindow()
  {
    if (!is_subwindow())
      return;
    ::delwin(active_window_);
    active_window_ = parent_window_;
    parent_window_ = nullptr;
  }

  // Move an existing derived subwindow within its parent without changing its dimensions.
  // `pos` is relative to active_window_. ncurses returns ERR if the moved subwindow would not fit in the parent;
  // callers can use the return value to decide whether to recreate the subwindow or report an invalid margin.
  int move_subwindow(Position pos)
  {
    if (!is_subwindow())
      return ERR;
    return ::mvderwin(active_window_, pos.row(), pos.col());
  }

  // Mark parent cells touched after writes through the derived subwindow.
  // Subwindows and parents share character storage but not all refresh bookkeeping; wsyncup propagates touch state
  // from the writable subwindow to ancestors so a later parent refresh knows which cells changed. It is a no-op when
  // no subwindow is active.
  void sync_subwindow_to_parent()
  {
    if (!is_subwindow())
      return;
    ::wsyncup(active_window_);
  }

  // Enable or disable ncurses automatic synchronization from the subwindow to its parent after each change.
  // The boolean controls ncurses syncok for the active subwindow only. The return value is ncurses OK/ERR so callers
  // can preserve error context when subwindow creation failed or ncurses rejects the request.
  int set_subwindow_sync(bool enabled)
  {
    if (!is_subwindow())
      return ERR;
    return ::syncok(active_window_, enabled);
  }

  void erase()
  {
    // See https://docs.oracle.com/cd/E88353_01/html/E37849/werase-3curses.html
    //
    // The werase() routine copy blanks to every position in the window.
    ::werase(active_window_);
  }

  void refresh()
  {
    // From https://docs.oracle.com/cd/E88353_01/html/E37849/wrefresh-3curses.html
    //
    // The refresh() and wrefresh() routines (or wnoutrefresh() and doupdate()) must be called to get any output on the terminal,
    // as other routines merely manipulate data structures. The routine wrefresh() copies the named window to the physical terminal screen,
    // taking into account what is already there in order to do optimizations. The refresh() routine is the same, using stdscr as the default window.
    // Unless leaveok() has been enabled, the physical cursor of the terminal is left at the location of the cursor for that window.
    if (is_subwindow())
      sync_subwindow_to_parent();
    ::wrefresh(outer_window());
  }

  void wborder_set(std::array<cchar_t, 8> const& b)
  {
    ::wborder_set(outer_window(), &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]);
  }

  ComplexChar outer_background() const
  {
    cchar_t background;
    ::wgetbkgrnd(outer_window(), &background);
    return convert_to_ComplexChar(background);
  }

  void addstr(char const* str)
  {
    ::waddstr(active_window_, str);
  }

  void addstr(char8_t const* utf8_str)
  {
    // Instead of using waddwstr, which would require application-side conversion from char8_t (utf8) to wchar_t,
    // it is better to just cast to `char const*` and let the terminal do that.
    ::waddstr(active_window_, reinterpret_cast<char const*>(utf8_str));
  }

  void addstr(Position pos, char const* str)
  {
    ::mvwaddstr(active_window_, pos.row(), pos.col(), str);
  }

  void addstr(Position pos, char8_t const* utf8_str)
  {
    // Instead of using mvwaddwstr, which would require application-side conversion from char8_t (utf8) to wchar_t,
    // it is better to just cast to `char const*` and let the terminal do that.
    ::mvwaddstr(active_window_, pos.row(), pos.col(), reinterpret_cast<char const*>(utf8_str));
  }

  void addstr(char const* str, int n)
  {
    ::waddnstr(active_window_, str, n);
  }

  void addstr(char8_t const* utf8_str, int n)
  {
    ::waddnstr(active_window_, reinterpret_cast<char const*>(utf8_str), n);
  }

  void addstr(Position pos, char const* str, int n)
  {
    ::mvwaddnstr(active_window_, pos.row(), pos.col(), str, n);
  }

  void addstr(Position pos, char8_t const* utf8_str, int n)
  {
    ::mvwaddnstr(active_window_, pos.row(), pos.col(), reinterpret_cast<char const*>(utf8_str), n);
  }

  void addstr(cchar_t const* wchstr)
  {
    ::wadd_wchstr(active_window_, wchstr);
  }

  void addstr(cchar_t const* wchstr, int n)
  {
    ::wadd_wchnstr(active_window_, wchstr, n);
  }

  void addstr(Position pos, cchar_t const* wchstr)
  {
    ::mvwadd_wchstr(active_window_, pos.row(), pos.col(), wchstr);
  }

  void addstr(Position pos, cchar_t const* wchstr, int n)
  {
    ::mvwadd_wchnstr(active_window_, pos.row(), pos.col(), wchstr, n);
  }

  void addch(ComplexChar const& complex_char)
  {
    cchar_t wch = convert_to_cchar(complex_char);
    ::wadd_wch(active_window_, &wch);
  }

  void addch(Position pos, ComplexChar const& complex_char)
  {
    cchar_t wch = convert_to_cchar(complex_char);
    ::mvwadd_wch(active_window_, pos.row(), pos.col(), &wch);
  }

  void echochar(ComplexChar const& complex_char)
  {
    cchar_t wch = convert_to_cchar(complex_char);
    ::wecho_wchar(active_window_, &wch);
  }

  void move(Position pos)
  {
    // https://invisible-island.net/ncurses/man/curs_move.3x.html
    //
    // wmove relocates the cursor associated with the curses window win to
    // line y and column x. The terminal's cursor does not move until
    // refresh(3x) is called. The position (y, x) is relative to the upper
    // left-hand corner of the window, which has coordinates (0, 0).
    ::wmove(active_window_, pos.row(), pos.col());
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

bool Window::create_writable_subwindow(Dimension size, Position pos)
{
  return impl_->create_subwindow(size, pos) != nullptr;
}

void Window::delete_writable_subwindow()
{
  impl_->delete_subwindow();
}

bool Window::move_writable_subwindow(Position pos)
{
  return impl_->move_subwindow(pos) != ERR;
}

void Window::sync_writable_subwindow_to_parent()
{
  impl_->sync_subwindow_to_parent();
}

bool Window::set_writable_subwindow_sync(bool enabled)
{
  return impl_->set_subwindow_sync(enabled) != ERR;
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
    ::wbkgrndset(impl_->active_window_, &wch);
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
    ::wbkgrnd(impl_->active_window_, &wch);
  }
}

ComplexChar Window::get_background() const
{
  cchar_t background;
  ::wgetbkgrnd(impl_->active_window_, &background);
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
  ComplexChar const background = impl_->outer_background();
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
