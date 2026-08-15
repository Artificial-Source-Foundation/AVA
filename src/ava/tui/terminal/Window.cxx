#include "sys.h"
#include "Window.h"

#include <array>
#include <cstdarg>
#include <string>
#include <utility>
#include <vector>
#include "debug.h"
#include "utils/macros.h"
#include "utils/print_wstring.h"

// This header must be included last.
#include "private_convert.h"

namespace ava::tui::terminal {

struct Window::Impl
{
 private:
  WINDOW* handle_;

 private:
  static int screen_max_row(Position screen_pos, Dimension screen_size)
  {
    ASSERT(screen_size.height() > 0);
    return static_cast<int>(screen_pos.row() + screen_size.height() - 1);
  }

  static int screen_max_col(Position screen_pos, Dimension screen_size)
  {
    ASSERT(screen_size.width() > 0);
    return static_cast<int>(screen_pos.col() + screen_size.width() - 1);
  }

  static std::vector<cchar_t> convert_to_cchar_vector(ComplexChar const* str, int n)
  {
    ASSERT(str);
    ASSERT(n >= 0);
    std::vector<cchar_t> result;
    result.reserve(static_cast<size_t>(n) + 1);
    for (int i = 0; i < n; ++i) result.push_back(convert_to_cchar(str[i]));
    result.push_back({});
    return result;
  }

  static void convert_from_cchar_array(cchar_t const* src, ComplexChar* dest, int n)
  {
    ASSERT(src);
    ASSERT(dest);
    ASSERT(n >= 0);
    for (int i = 0; i < n; ++i) dest[i] = convert_to_ComplexChar(src[i]);
  }

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

  static WINDOW* newpad(Dimension size)
  {
    // https://invisible-island.net/ncurses/man/curs_pad.3x.html
    //
    // newpad creates and returns a pointer to a new pad data structure with the given number of lines and columns.
    // A pad is not restricted by the screen size and is refreshed with explicit source and destination rectangles.
    return ::newpad(size.height(), size.width());
  }

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

  void cursyncup()
  {
    // https://invisible-island.net/ncurses/man/curs_window.3x.html
    //
    // wcursyncup updates the current cursor position of all ancestors of the window to reflect the current cursor
    // position of this window.
    ::wcursyncup(handle_);
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

  int clear()
  {
    // https://invisible-island.net/ncurses/man/curs_clear.3x.html
    //
    // wclear clears the window like werase and also arranges for the next refresh to clear and repaint the screen.
    return ::wclear(handle_);
  }

  int clrtobot()
  {
    // https://invisible-island.net/ncurses/man/curs_clear.3x.html
    //
    // wclrtobot clears from the cursor to the end of the window, inclusive of the cursor line after the cursor.
    return ::wclrtobot(handle_);
  }

  int clrtoeol()
  {
    // https://invisible-island.net/ncurses/man/curs_clear.3x.html
    //
    // wclrtoeol clears from the cursor to the end of the current line.
    return ::wclrtoeol(handle_);
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

  int wnoutrefresh()
  {
    // https://invisible-island.net/ncurses/man/curs_refresh.3x.html
    //
    // wnoutrefresh copies the window to the virtual screen without updating the physical terminal until doupdate.
    return ::wnoutrefresh(handle_);
  }

  int redrawwin()
  {
    // https://invisible-island.net/ncurses/man/curs_refresh.3x.html
    //
    // redrawwin marks the entire window as changed so the next refresh repaints it completely.
    return ::redrawwin(handle_);
  }

  int wredrawln(int beg_line, int num_lines)
  {
    // https://invisible-island.net/ncurses/man/curs_refresh.3x.html
    //
    // wredrawln marks a range of lines as changed so the next refresh repaints those lines.
    return ::wredrawln(handle_, beg_line, num_lines);
  }

  int clearok(bool bf)
  {
    // https://invisible-island.net/ncurses/man/curs_outopts.3x.html
    //
    // clearok controls whether the next refresh clears and repaints the screen from scratch.
    return ::clearok(handle_, bf);
  }

  void idcok(bool bf)
  {
    // https://invisible-island.net/ncurses/man/curs_outopts.3x.html
    //
    // idcok controls use of the terminal insert/delete character feature for this window.
    ::idcok(handle_, bf);
  }

  int idlok(bool bf)
  {
    // https://invisible-island.net/ncurses/man/curs_outopts.3x.html
    //
    // idlok controls use of the terminal insert/delete line feature for this window.
    return ::idlok(handle_, bf);
  }

  void immedok(bool bf)
  {
    // https://invisible-island.net/ncurses/man/curs_outopts.3x.html
    //
    // immedok controls whether each window change automatically refreshes the window immediately.
    ::immedok(handle_, bf);
  }

  int leaveok(bool bf)
  {
    // https://invisible-island.net/ncurses/man/curs_outopts.3x.html
    //
    // leaveok controls whether refresh leaves the physical cursor where ncurses happens to leave it.
    return ::leaveok(handle_, bf);
  }

  int scrollok(bool bf)
  {
    // https://invisible-island.net/ncurses/man/curs_outopts.3x.html
    //
    // scrollok controls whether output may scroll the window when the cursor moves past the bottom edge.
    return ::scrollok(handle_, bf);
  }

  int setscrreg(int top, int bot)
  {
    // https://invisible-island.net/ncurses/man/curs_outopts.3x.html
    //
    // wsetscrreg sets the scrolling region, limiting line scroll operations to the given inclusive line range.
    return ::wsetscrreg(handle_, top, bot);
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

  int attr_set(Rendition rendition)
  {
    // https://invisible-island.net/ncurses/man/curs_attr.3x.html
    //
    // wattr_set sets the window's current attributes and color pair; subsequent characters added to the window use
    // this rendition until it is changed again.
    int color_pair = rendition.color_pair().index();
    return ::wattr_set(handle_, convert_to_attr(rendition.attributes()), 0, &color_pair);
  }

  int attr_get(Rendition& rendition) const
  {
    // https://invisible-island.net/ncurses/man/curs_attr.3x.html
    //
    // wattr_get returns the window's current attributes and color pair used for subsequent output.
    attr_t attrs = A_NORMAL;
    NCURSES_PAIRS_T pair = 0;
    int extended_pair = 0;
    int res = ::wattr_get(handle_, &attrs, &pair, &extended_pair);
    ColorPair color_pair;
    color_pair.index() = extended_pair;
    rendition = Rendition{color_pair, convert_to_Attributes(attrs)};
    return res;
  }

  int attr_on(Attributes attributes)
  {
    // https://invisible-island.net/ncurses/man/curs_attr.3x.html
    //
    // wattr_on turns on the named attributes of the window without disturbing other attributes or the color pair.
    return ::wattr_on(handle_, convert_to_attr(attributes), nullptr);
  }

  int attr_off(Attributes attributes)
  {
    // https://invisible-island.net/ncurses/man/curs_attr.3x.html
    //
    // wattr_off turns off the named attributes of the window without disturbing other attributes or the color pair.
    return ::wattr_off(handle_, convert_to_attr(attributes), nullptr);
  }

  int color_set(ColorPair color_pair)
  {
    // https://invisible-island.net/ncurses/man/curs_attr.3x.html
    //
    // wcolor_set sets the current color pair of the window for characters written after the call.
    int extended_pair = color_pair.index();
    return ::wcolor_set(handle_, 0, &extended_pair);
  }

  int chgat(int n, Rendition rendition)
  {
    // https://invisible-island.net/ncurses/man/curs_attr.3x.html
    //
    // wchgat changes the rendition of a given number of characters starting at the current cursor position; negative
    // n changes characters through the end of the line.
    return ::wchgat(handle_, n, convert_to_attr(rendition.attributes()), rendition.color_pair().index(), nullptr);
  }

  int chgat(Position pos, int n, Rendition rendition)
  {
    // https://invisible-island.net/ncurses/man/curs_attr.3x.html
    //
    // mvwchgat first moves the cursor to the requested window-relative position and then changes character rendition.
    return ::mvwchgat(handle_, pos.row(), pos.col(), n, convert_to_attr(rendition.attributes()), rendition.color_pair().index(), nullptr);
  }

  int standout()
  {
    // https://invisible-island.net/ncurses/man/curs_attr.3x.html
    //
    // wstandout turns on the best highlighting mode of the terminal for the window's subsequent output.
    return ::wstandout(handle_);
  }

  int standend()
  {
    // https://invisible-island.net/ncurses/man/curs_attr.3x.html
    //
    // wstandend turns off all attributes for subsequent output on the window.
    return ::wstandend(handle_);
  }

  // https://invisible-island.net/ncurses/man/curs_addstr.3x.html
  //
  // The addstr, addnstr, waddstr, and waddnstr routines write all characters of the null-terminated string str on the given window.
  // The n variants write at most n characters. The mv variants first move the cursor to the given Position.
  //
  void addstr(char const* str) { ::waddstr(handle_, str); }
  void addstr(char const* str, int n) { ::waddnstr(handle_, str, n); }
  void addstr(char8_t const* utf8_str) { ::waddstr(handle_, reinterpret_cast<char const*>(utf8_str)); }
  void addstr(char8_t const* utf8_str, int n) { ::waddnstr(handle_, reinterpret_cast<char const*>(utf8_str), n); }
  void addstr(Position pos, char const* str) { ::mvwaddstr(handle_, pos.row(), pos.col(), str); }
  void addstr(Position pos, char const* str, int n) { ::mvwaddnstr(handle_, pos.row(), pos.col(), str, n); }
  // Instead of using waddwstr, which would require application-side conversion from char8_t (utf8) to wchar_t,
  // it is better to just cast to `char const*` and let the terminal do that.
  void addstr(Position pos, char8_t const* utf8_str) { ::mvwaddstr(handle_, pos.row(), pos.col(), reinterpret_cast<char const*>(utf8_str)); }
  void addstr(Position pos, char8_t const* utf8_str, int n) { ::mvwaddnstr(handle_, pos.row(), pos.col(), reinterpret_cast<char const*>(utf8_str), n); }

  // https://invisible-island.net/ncurses/man/curs_addwstr.3x.html
  //
  // waddwstr writes a null-terminated wide-character string to the window starting at the cursor.
  // The n variants write at most n characters. The mv variants first move the cursor to the given Position.
  //
  int addstr(wchar_t const* str) { return ::waddwstr(handle_, str); }
  int addstr(wchar_t const* str, int n) { return ::waddnwstr(handle_, str, n); }
  int addstr(Position pos, wchar_t const* str) { return ::mvwaddwstr(handle_, pos.row(), pos.col(), str); }
  int addstr(Position pos, wchar_t const* str, int n) { return ::mvwaddnwstr(handle_, pos.row(), pos.col(), str, n); }

#if 0 // FIXME: Lets not do this; instead set a different rendition with Window::attr_set and then write a string using addstr to build the cchar_t array.
  // https://invisible-island.net/ncurses/man/curs_add_wchstr.3x.html
  //
  // The wadd_wchstr functions copy the array of complex characters into the window image structure at and after the cursor position.
  // The n variants write at most n characters. The mv variants first move the cursor to the given Position.
  //
  int addstr(ComplexChar const* str)
  {
    // https://invisible-island.net/ncurses/man/curs_add_wchstr.3x.html
    //
    ASSERT(str);
    int n = 0;
    while (str[n].cell_character().length() != 0) ++n;
    auto converted = convert_to_cchar_vector(str, n);
    return ::wadd_wchstr(handle_, converted.data());
  }

  int addstr(ComplexChar const* str, int n)
  {
    // https://invisible-island.net/ncurses/man/curs_add_wchstr.3x.html
    //
    // wadd_wchnstr copies at most n complex characters into the window at and after the cursor without advancing it.
    auto converted = convert_to_cchar_vector(str, n);
    return ::wadd_wchnstr(handle_, converted.data(), n);
  }

  int addstr(Position pos, ComplexChar const* str)
  {
    // https://invisible-island.net/ncurses/man/curs_add_wchstr.3x.html
    //
    // mvwadd_wchstr moves the cursor, then copies complex characters into the window without advancing it.
    ASSERT(str);
    int n = 0;
    while (str[n].cell_character().length() != 0) ++n;
    auto converted = convert_to_cchar_vector(str, n);
    return ::mvwadd_wchstr(handle_, pos.row(), pos.col(), converted.data());
  }

  int addstr(Position pos, ComplexChar const* str, int n)
  {
    // https://invisible-island.net/ncurses/man/curs_add_wchstr.3x.html
    //
    // mvwadd_wchnstr moves the cursor, then copies at most n complex characters into the window.
    auto converted = convert_to_cchar_vector(str, n);
    return ::mvwadd_wchnstr(handle_, pos.row(), pos.col(), converted.data(), n);
  }
#endif

  // https://invisible-island.net/ncurses/man/curs_add_wch.3x.html
  //
  // The wadd_wch function places the complex character at the current cursor position of the specified window, then advances the cursor position.
  // The mv variant first move the cursor to the given Position.
  //
  // The wecho_wchar function is functionally equivalent to calling wadd_wch followed by wrefresh.
  //
  void addch(ComplexChar const& complex_char)
  {
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
    cchar_t wch = convert_to_cchar(complex_char);
    ::wecho_wchar(handle_, &wch);
  }

  int delch()
  {
    // https://invisible-island.net/ncurses/man/curs_delch.3x.html
    //
    // wdelch deletes the character under the cursor; characters to the right shift left and the last cell becomes
    // blank.
    return ::wdelch(handle_);
  }

  int delch(Position pos)
  {
    // https://invisible-island.net/ncurses/man/curs_delch.3x.html
    //
    // mvwdelch moves to the requested position and then deletes the character under the cursor.
    return ::mvwdelch(handle_, pos.row(), pos.col());
  }

  int insdelln(int n)
  {
    // https://invisible-island.net/ncurses/man/curs_deleteln.3x.html
    //
    // winsdelln inserts n blank lines above the cursor line when positive, or deletes lines when negative.
    return ::winsdelln(handle_, n);
  }

  int get_wch(wint_t& key)
  {
    // https://invisible-island.net/ncurses/man/curs_get_wch.3x.html
    //
    // wget_wch gets a wide character or function-key code from the window's input stream.
    return ::wget_wch(handle_, &key);
  }

  static int unget_wch(wchar_t key)
  {
    // https://invisible-island.net/ncurses/man/curs_get_wch.3x.html
    //
    // unget_wch pushes a wide character back onto the input queue so it is returned by a subsequent input call.
    return ::unget_wch(key);
  }

  int in_wch(ComplexChar& complex_char) const
  {
    // https://invisible-island.net/ncurses/man/curs_in_wch.3x.html
    //
    // win_wch extracts the complex character and rendition at the cursor without altering the window.
    cchar_t wch;
    int res = ::win_wch(handle_, &wch);
    complex_char = convert_to_ComplexChar(wch);
    return res;
  }

  int in_wch(Position pos, ComplexChar& complex_char) const
  {
    // https://invisible-island.net/ncurses/man/curs_in_wch.3x.html
    //
    // mvwin_wch moves to the requested position and extracts the complex character and rendition at that cell.
    cchar_t wch;
    int res = ::mvwin_wch(handle_, pos.row(), pos.col(), &wch);
    complex_char = convert_to_ComplexChar(wch);
    return res;
  }

  int instr(ComplexChar* str) const
  {
    // https://invisible-island.net/ncurses/man/curs_in_wchstr.3x.html
    //
    // win_wchstr reads complex characters from the cursor through the end of the line into the caller's buffer.
    ASSERT(str);
    int cols = getmaxx(handle_);
    std::vector<cchar_t> tmp(static_cast<size_t>(cols) + 1);
    int res = ::win_wchstr(handle_, tmp.data());
    convert_from_cchar_array(tmp.data(), str, cols);
    return res;
  }

  int instr(ComplexChar* str, int n) const
  {
    // https://invisible-island.net/ncurses/man/curs_in_wchstr.3x.html
    //
    // win_wchnstr reads at most n complex characters from the cursor into the caller's buffer.
    std::vector<cchar_t> tmp(static_cast<size_t>(n) + 1);
    int res = ::win_wchnstr(handle_, tmp.data(), n);
    convert_from_cchar_array(tmp.data(), str, n);
    return res;
  }

  int instr(Position pos, ComplexChar* str) const
  {
    // https://invisible-island.net/ncurses/man/curs_in_wchstr.3x.html
    //
    // mvwin_wchstr moves to the requested position and reads complex characters through the end of the line.
    ASSERT(str);
    int cols = getmaxx(handle_);
    std::vector<cchar_t> tmp(static_cast<size_t>(cols) + 1);
    int res = ::mvwin_wchstr(handle_, pos.row(), pos.col(), tmp.data());
    convert_from_cchar_array(tmp.data(), str, cols);
    return res;
  }

  int instr(Position pos, ComplexChar* str, int n) const
  {
    // https://invisible-island.net/ncurses/man/curs_in_wchstr.3x.html
    //
    // mvwin_wchnstr moves to the requested position and reads at most n complex characters from that cell.
    std::vector<cchar_t> tmp(static_cast<size_t>(n) + 1);
    int res = ::mvwin_wchnstr(handle_, pos.row(), pos.col(), tmp.data(), n);
    convert_from_cchar_array(tmp.data(), str, n);
    return res;
  }

  int ins_wch(ComplexChar const& complex_char)
  {
    // https://invisible-island.net/ncurses/man/curs_ins_wch.3x.html
    //
    // wins_wch inserts a complex character before the cursor and shifts following characters right.
    cchar_t wch = convert_to_cchar(complex_char);
    return ::wins_wch(handle_, &wch);
  }

  int ins_wch(Position pos, ComplexChar const& complex_char)
  {
    // https://invisible-island.net/ncurses/man/curs_ins_wch.3x.html
    //
    // mvwins_wch moves to the requested position and inserts a complex character before that cell.
    cchar_t wch = convert_to_cchar(complex_char);
    return ::mvwins_wch(handle_, pos.row(), pos.col(), &wch);
  }

  int insstr(wchar_t const* str)
  {
    // https://invisible-island.net/ncurses/man/curs_ins_wstr.3x.html
    //
    // wins_wstr inserts a wide-character string before the cursor, shifting existing cells right.
    return ::wins_wstr(handle_, str);
  }

  int insstr(wchar_t const* str, int n)
  {
    // https://invisible-island.net/ncurses/man/curs_ins_wstr.3x.html
    //
    // wins_nwstr inserts at most n wide characters before the cursor, stopping at a null wide character.
    return ::wins_nwstr(handle_, str, n);
  }

  int insstr(Position pos, wchar_t const* str)
  {
    // https://invisible-island.net/ncurses/man/curs_ins_wstr.3x.html
    //
    // mvwins_wstr moves to the requested position and inserts a wide-character string before that cell.
    return ::mvwins_wstr(handle_, pos.row(), pos.col(), str);
  }

  int insstr(Position pos, wchar_t const* str, int n)
  {
    // https://invisible-island.net/ncurses/man/curs_ins_wstr.3x.html
    //
    // mvwins_nwstr moves to the requested position and inserts at most n wide characters before that cell.
    return ::mvwins_nwstr(handle_, pos.row(), pos.col(), str, n);
  }

  int insstr(char const* str)
  {
    // https://invisible-island.net/ncurses/man/curs_insstr.3x.html
    //
    // winsstr inserts a narrow string before the cursor, shifting existing characters right until the line fills.
    return ::winsstr(handle_, str);
  }

  int insstr(char const* str, int n)
  {
    // https://invisible-island.net/ncurses/man/curs_insstr.3x.html
    //
    // winsnstr inserts at most n narrow bytes before the cursor, shifting existing characters right.
    return ::winsnstr(handle_, str, n);
  }

  int insstr(Position pos, char const* str)
  {
    // https://invisible-island.net/ncurses/man/curs_insstr.3x.html
    //
    // mvwinsstr moves to the requested position and inserts a narrow string before that cell.
    return ::mvwinsstr(handle_, pos.row(), pos.col(), str);
  }

  int insstr(Position pos, char const* str, int n)
  {
    // https://invisible-island.net/ncurses/man/curs_insstr.3x.html
    //
    // mvwinsnstr moves to the requested position and inserts at most n narrow bytes before that cell.
    return ::mvwinsnstr(handle_, pos.row(), pos.col(), str, n);
  }

  int inwstr(wchar_t* str) const
  {
    // https://invisible-island.net/ncurses/man/curs_inwstr.3x.html
    //
    // winwstr extracts wide characters from the cursor through the end of the line into the caller's buffer.
    return ::winwstr(handle_, str);
  }

  int inwstr(wchar_t* str, int n) const
  {
    // https://invisible-island.net/ncurses/man/curs_inwstr.3x.html
    //
    // winnwstr extracts at most n wide characters from the cursor into the caller's buffer.
    return ::winnwstr(handle_, str, n);
  }

  int inwstr(Position pos, wchar_t* str) const
  {
    // https://invisible-island.net/ncurses/man/curs_inwstr.3x.html
    //
    // mvwinwstr moves to the requested position and extracts wide characters through the end of the line.
    return ::mvwinwstr(handle_, pos.row(), pos.col(), str);
  }

  int inwstr(Position pos, wchar_t* str, int n) const
  {
    // https://invisible-island.net/ncurses/man/curs_inwstr.3x.html
    //
    // mvwinnwstr moves to the requested position and extracts at most n wide characters from that cell.
    return ::mvwinnwstr(handle_, pos.row(), pos.col(), str, n);
  }

  static int curs_set(int visibility)
  {
    // https://invisible-island.net/ncurses/man/curs_kernel.3x.html
    //
    // curs_set changes the terminal cursor visibility and returns the previous visibility setting when supported.
    return ::curs_set(visibility);
  }

  int printw(char const* fmt, va_list args)
  {
    // https://invisible-island.net/ncurses/man/curs_printw.3x.html
    //
    // vw_printw performs printf-style formatted output to the window using a va_list.
    return ::vw_printw(handle_, fmt, args);
  }

  int printw(Position pos, char const* fmt, va_list args)
  {
    // https://invisible-island.net/ncurses/man/curs_printw.3x.html
    //
    // mvwprintw first moves the cursor to the requested position and then performs printf-style output.
    int res = ::wmove(handle_, pos.row(), pos.col());
    if (res == ERR)
      return res;
    return ::vw_printw(handle_, fmt, args);
  }

  WINDOW* subpad(Dimension size, Position pos)
  {
    // https://invisible-island.net/ncurses/man/curs_pad.3x.html
    //
    // subpad creates a subwindow within a pad; its position is relative to the parent pad and storage is shared.
    return ::subpad(handle_, size.height(), size.width(), pos.row(), pos.col());
  }

  int prefresh(Position pad_pos, Position screen_pos, Dimension screen_size)
  {
    // https://invisible-island.net/ncurses/man/curs_pad.3x.html
    //
    // prefresh copies a rectangle from the pad, starting at pad_pos, to an inclusive rectangle on the physical screen.
    return ::prefresh(handle_, pad_pos.row(), pad_pos.col(), screen_pos.row(), screen_pos.col(), screen_max_row(screen_pos, screen_size),
                      screen_max_col(screen_pos, screen_size));
  }

  int pnoutrefresh(Position pad_pos, Position screen_pos, Dimension screen_size)
  {
    // https://invisible-island.net/ncurses/man/curs_pad.3x.html
    //
    // pnoutrefresh stages a pad rectangle on the virtual screen; doupdate performs the physical update later.
    return ::pnoutrefresh(handle_, pad_pos.row(), pad_pos.col(), screen_pos.row(), screen_pos.col(), screen_max_row(screen_pos, screen_size),
                          screen_max_col(screen_pos, screen_size));
  }

  int pechochar(ComplexChar const& complex_char)
  {
    // https://invisible-island.net/ncurses/man/curs_pad.3x.html
    //
    // pecho_wchar adds a complex character to a pad and refreshes the pad using the viewport remembered by ncurses.
    cchar_t wch = convert_to_cchar(complex_char);
    return ::pecho_wchar(handle_, &wch);
  }

  int scrl(int n)
  {
    // https://invisible-island.net/ncurses/man/curs_scroll.3x.html
    //
    // wscrl scrolls the window up for positive n or down for negative n, subject to scrollok and scrolling region.
    return ::wscrl(handle_, n);
  }

  static char const* key_name(wint_t key)
  {
    // https://invisible-island.net/ncurses/man/curs_util.3x.html
    //
    // key_name returns a printable name for a wide character or function-key code.
    return ::key_name(key);
  }

  int move(Position pos)
  {
    // https://invisible-island.net/ncurses/man/curs_move.3x.html
    //
    // wmove relocates the cursor associated with the curses window win to
    // line y and column x. The terminal's cursor does not move until
    // refresh(3x) is called. The position (y, x) is relative to the upper
    // left-hand corner of the window, which has coordinates (0, 0).
    return ::wmove(handle_, pos.row(), pos.col());
  }

  int resize(Dimension size)
  {
    // https://invisible-island.net/ncurses/man/wresize.3x.html
    //
    // wresize reallocates storage for win, adjusting its dimensions to lines and columns.
    // If either dimension is larger than its current value, ncurses fills the expanded part
    // of the window with the window's background character as configured by wbkgrndset.
    return ::wresize(handle_, size.height(), size.width());
  }

  Position getyx() const
  {
    // https://invisible-island.net/ncurses/man/curs_getyx.3x.html
    //
    // getyx stores the current cursor row and column of the specified window in caller-provided variables.
    int y = ::getcury(handle_);
    int x = ::getcurx(handle_);
    ASSERT(y >= 0 && x >= 0);
    return Position(static_cast<uint32_t>(y), static_cast<uint32_t>(x));
  }

  Position getbegyx() const
  {
    // https://invisible-island.net/ncurses/man/curs_getyx.3x.html
    //
    // getbegyx stores the beginning row and column of the specified window in screen coordinates.
    int y = ::getbegy(handle_);
    int x = ::getbegx(handle_);
    ASSERT(y >= 0 && x >= 0);
    return Position(static_cast<uint32_t>(y), static_cast<uint32_t>(x));
  }

  Dimension getmaxyx() const
  {
    // https://invisible-island.net/ncurses/man/curs_getyx.3x.html
    //
    // getmaxyx stores the size of the specified window as row and column counts.
    int y = ::getmaxy(handle_);
    int x = ::getmaxx(handle_);
    ASSERT(y >= 0 && x >= 0);
    return Dimension(static_cast<uint32_t>(y), static_cast<uint32_t>(x));
  }

  std::optional<Position> getparyx() const
  {
    // https://invisible-island.net/ncurses/man/curs_getyx.3x.html
    //
    // getparyx stores a subwindow's beginning row and column relative to its parent, or (-1, -1) for no parent.
    int y = ::getpary(handle_);
    int x = ::getparx(handle_);
    if (y == -1 && x == -1)
      return std::nullopt;
    ASSERT(y >= 0 && x >= 0);
    return Position(static_cast<uint32_t>(y), static_cast<uint32_t>(x));
  }

  bool enclose(Position pos) const
  {
    // https://invisible-island.net/ncurses/man/curs_mouse.3x.html
    //
    // wenclose tests whether the given screen-relative row and column fall inside the specified window.
    return ::wenclose(handle_, pos.row(), pos.col());
  }

  bool mouse_trafo(Position& pos, bool to_screen) const
  {
    // https://invisible-island.net/ncurses/man/curs_mouse.3x.html
    //
    // wmouse_trafo converts coordinates between screen-relative and window-relative coordinate systems when possible.
    int y = static_cast<int>(pos.row());
    int x = static_cast<int>(pos.col());
    bool const res = ::wmouse_trafo(handle_, &y, &x, to_screen);
    if (res)
    {
      ASSERT(y >= 0 && x >= 0);
      pos = Position(static_cast<uint32_t>(y), static_cast<uint32_t>(x));
    }
    return res;
  }

  bool is_cleared() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_cleared returns whether clearok has marked the window to be cleared on the next refresh.
    return ::is_cleared(handle_);
  }

  bool is_idcok() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_idcok returns whether use of terminal insert/delete character capabilities is enabled for the window.
    return ::is_idcok(handle_);
  }

  bool is_idlok() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_idlok returns whether use of terminal insert/delete line capabilities is enabled for the window.
    return ::is_idlok(handle_);
  }

  bool is_immedok() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_immedok returns whether window changes automatically trigger a refresh.
    return ::is_immedok(handle_);
  }

  bool is_keypad() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_keypad returns whether keypad translation is enabled for the window.
    return ::is_keypad(handle_);
  }

  bool is_leaveok() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_leaveok returns whether refresh may leave the physical cursor wherever ncurses chooses.
    return ::is_leaveok(handle_);
  }

  bool is_nodelay() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_nodelay returns whether input reads are configured to return immediately when no input is ready.
    return ::is_nodelay(handle_);
  }

  bool is_notimeout() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_notimeout returns whether escape-sequence timer behavior is disabled for the window.
    return ::is_notimeout(handle_);
  }

  bool is_pad() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_pad returns whether the window is a pad rather than a normal screen-bounded window.
    return ::is_pad(handle_);
  }

  bool is_scrollok() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_scrollok returns whether the window may scroll when output passes the bottom edge.
    return ::is_scrollok(handle_);
  }

  bool is_subwin() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_subwin returns whether the window is a subwindow sharing storage with another window.
    return ::is_subwin(handle_);
  }

  bool is_syncok() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // is_syncok returns whether window changes automatically propagate touched state to ancestors.
    return ::is_syncok(handle_);
  }

  int getdelay() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // wgetdelay returns the input delay for the window as set by nodelay or wtimeout.
    return ::wgetdelay(handle_);
  }

  WINDOW* getparent() const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // wgetparent returns a pointer to the parent WINDOW of a subwindow, or null if there is no parent.
    return ::wgetparent(handle_);
  }

  int getscrreg(ScrollRegion& region) const
  {
    // https://invisible-island.net/ncurses/man/curs_opaque.3x.html
    //
    // wgetscrreg stores the top and bottom row numbers of the window's scrolling region.
    return ::wgetscrreg(handle_, &region.top, &region.bottom);
  }
};

Window::Window(Dimension size, Position pos) : impl_(std::make_unique<Impl>(size, pos))
{
}

Window::Window(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}

Window::Window() = default;

void Window::init_as_stdscr()
{
  // Only call this function once and only on a default constructed Window. This should only be called from Context().
  ASSERT(!impl_);
  impl_ = std::make_unique<Impl>();
}

Window::~Window() = default;
Window::Window(Window&&) noexcept = default;
Window& Window::operator=(Window&&) noexcept = default;

Window Window::newpad(Dimension size)
{
  WINDOW* res = Impl::newpad(size);
  ASSERT(res);
  return Window(std::make_unique<Impl>(res));
}

Window Window::subwin(Dimension size, Position pos)
{
  return Window(std::make_unique<Impl>(impl_->subwin(size, pos)));
}

Window Window::subwin(Margin margin)
{
  Dimension size = getmaxyx();
  // The caller is responsible for making sure this is true.
  ASSERT(margin < size);
  Position pos = getbegyx();
  return subwin(size - margin, pos + margin);
}

Window Window::derwin(Dimension size, Position pos)
{
  return Window(std::make_unique<Impl>(impl_->derwin(size, pos)));
}

Window Window::derwin(Margin margin)
{
  Dimension size = getmaxyx();
  // The caller is responsible for making sure this is true.
  ASSERT(margin < size);
  Position pos{0, 0};
  return derwin(size - margin, pos + margin);
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

void Window::cursyncup()
{
  impl_->cursyncup();
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

void Window::attr_set(Rendition rendition)
{
  int res = impl_->attr_set(rendition);
  ASSERT(res != ERR);
}

void Window::attr_get(Rendition& rendition) const
{
  int res = impl_->attr_get(rendition);
  ASSERT(res != ERR);
}

void Window::attr_on(Attributes attributes)
{
  int res = impl_->attr_on(attributes);
  ASSERT(res != ERR);
}

void Window::attr_off(Attributes attributes)
{
  int res = impl_->attr_off(attributes);
  ASSERT(res != ERR);
}

void Window::color_set(ColorPair color_pair)
{
  int res = impl_->color_set(color_pair);
  ASSERT(res != ERR);
}

void Window::chgat(int n, Rendition rendition)
{
  int res = impl_->chgat(n, rendition);
  ASSERT(res != ERR);
}

void Window::chgat(Position pos, int n, Rendition rendition)
{
  int res = impl_->chgat(pos, n, rendition);
  ASSERT(res != ERR);
}

void Window::standout()
{
  int res = impl_->standout();
  ASSERT(res != ERR);
}

void Window::standend()
{
  int res = impl_->standend();
  ASSERT(res != ERR);
}

void Window::erase()
{
  impl_->erase();
}

void Window::clear()
{
  int res = impl_->clear();
  ASSERT(res != ERR);
}

void Window::clrtobot()
{
  int res = impl_->clrtobot();
  ASSERT(res != ERR);
}

void Window::clrtoeol()
{
  int res = impl_->clrtoeol();
  ASSERT(res != ERR);
}

void Window::refresh()
{
  impl_->refresh();
}

void Window::wnoutrefresh()
{
  int res = impl_->wnoutrefresh();
  ASSERT(res != ERR);
}

void Window::redrawwin()
{
  int res = impl_->redrawwin();
  ASSERT(res != ERR);
}

void Window::wredrawln(int beg_line, int num_lines)
{
  int res = impl_->wredrawln(beg_line, num_lines);
  ASSERT(res != ERR);
}

void Window::clearok(bool bf)
{
  int res = impl_->clearok(bf);
  ASSERT(res != ERR);
}

void Window::idcok(bool bf)
{
  impl_->idcok(bf);
}

void Window::idlok(bool bf)
{
  int res = impl_->idlok(bf);
  ASSERT(res != ERR);
}

void Window::immedok(bool bf)
{
  impl_->immedok(bf);
}

void Window::leaveok(bool bf)
{
  int res = impl_->leaveok(bf);
  ASSERT(res != ERR);
}

void Window::scrollok(bool bf)
{
  int res = impl_->scrollok(bf);
  ASSERT(res != ERR);
}

void Window::setscrreg(int top, int bot)
{
  int res = impl_->setscrreg(top, bot);
  ASSERT(res != ERR);
}

void Window::set_border(Border const& border)
{
  ComplexChar const background = get_background();
  std::array<cchar_t, 8> complex_characters;
  for (int i = 0; i < 8; ++i) complex_characters[i] = convert_to_cchar(border.get_complex_character(i, background.rendition()));
  impl_->border_set(complex_characters);
}

void Window::hline_set(ComplexChar const& complex_char, int n)
{
  int res = impl_->hline_set(complex_char, n);
  ASSERT(res != ERR);
}

void Window::hline_set(Position pos, ComplexChar const& complex_char, int n)
{
  int res = impl_->hline_set(pos, complex_char, n);
  ASSERT(res != ERR);
}

void Window::vline_set(ComplexChar const& complex_char, int n)
{
  int res = impl_->vline_set(complex_char, n);
  ASSERT(res != ERR);
}

void Window::vline_set(Position pos, ComplexChar const& complex_char, int n)
{
  int res = impl_->vline_set(pos, complex_char, n);
  ASSERT(res != ERR);
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

#if 0
void Window::addstr(ComplexChar const* str)
{
  int res = impl_->addstr(str);
  ASSERT(res != ERR);
}

void Window::addstr(ComplexChar const* str, int n)
{
  int res = impl_->addstr(str, n);
  ASSERT(res != ERR);
}

void Window::addstr(Position pos, ComplexChar const* str)
{
  int res = impl_->addstr(pos, str);
  ASSERT(res != ERR);
}

void Window::addstr(Position pos, ComplexChar const* str, int n)
{
  int res = impl_->addstr(pos, str, n);
  ASSERT(res != ERR);
}
#endif

void Window::addstr(wchar_t const* str)
{
  int res = impl_->addstr(str);
  ASSERT(res != ERR);
}

void Window::addstr(wchar_t const* str, int n)
{
  int res = impl_->addstr(str, n);
#if CW_DEBUG
  if (AI_UNLIKELY(res == ERR))
  {
    // The bottom-right corner character was stored, but the cursor could not advance
    // past it (see the comment in Window.h). Verify that the cursor is indeed stuck
    // at the bottom-right corner, so that any other kind of error still asserts.
    Position const cursor = getyx();
    Dimension const size = getmaxyx();
    ASSERT(cursor.row() + 1 == size.height() && cursor.col() + 1 == size.width());
  }
}
#endif

void Window::addstr(Position pos, wchar_t const* str)
{
  int res = impl_->addstr(pos, str);
  ASSERT(res != ERR);
}

void Window::addstr(Position pos, wchar_t const* str, int n)
{
  int res = impl_->addstr(pos, str, n);
  ASSERT(res != ERR);
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

void Window::delch()
{
  int res = impl_->delch();
  ASSERT(res != ERR);
}

void Window::delch(Position pos)
{
  int res = impl_->delch(pos);
  ASSERT(res != ERR);
}

void Window::insdelln(int n)
{
  int res = impl_->insdelln(n);
  ASSERT(res != ERR);
}

void Window::get_wch(wint_t& key)
{
  int res = impl_->get_wch(key);
  ASSERT(res != ERR);
}

void Window::unget_wch(wchar_t key)
{
  int res = Impl::unget_wch(key);
  ASSERT(res != ERR);
}

void Window::in_wch(ComplexChar& complex_char) const
{
  int res = impl_->in_wch(complex_char);
  ASSERT(res != ERR);
}

void Window::in_wch(Position pos, ComplexChar& complex_char) const
{
  int res = impl_->in_wch(pos, complex_char);
  ASSERT(res != ERR);
}

void Window::instr(ComplexChar* str) const
{
  int res = impl_->instr(str);
  ASSERT(res != ERR);
}

void Window::instr(ComplexChar* str, int n) const
{
  int res = impl_->instr(str, n);
  ASSERT(res != ERR);
}

void Window::instr(Position pos, ComplexChar* str) const
{
  int res = impl_->instr(pos, str);
  ASSERT(res != ERR);
}

void Window::instr(Position pos, ComplexChar* str, int n) const
{
  int res = impl_->instr(pos, str, n);
  ASSERT(res != ERR);
}

void Window::ins_wch(ComplexChar const& complex_char)
{
  int res = impl_->ins_wch(complex_char);
  ASSERT(res != ERR);
}

void Window::ins_wch(Position pos, ComplexChar const& complex_char)
{
  int res = impl_->ins_wch(pos, complex_char);
  ASSERT(res != ERR);
}

void Window::insstr(wchar_t const* str)
{
  int res = impl_->insstr(str);
  ASSERT(res != ERR);
}

void Window::insstr(wchar_t const* str, int n)
{
  int res = impl_->insstr(str, n);
  ASSERT(res != ERR);
}

void Window::insstr(Position pos, wchar_t const* str)
{
  int res = impl_->insstr(pos, str);
  ASSERT(res != ERR);
}

void Window::insstr(Position pos, wchar_t const* str, int n)
{
  int res = impl_->insstr(pos, str, n);
  ASSERT(res != ERR);
}

void Window::insstr(char const* str)
{
  int res = impl_->insstr(str);
  ASSERT(res != ERR);
}

void Window::insstr(char const* str, int n)
{
  int res = impl_->insstr(str, n);
  ASSERT(res != ERR);
}

void Window::insstr(Position pos, char const* str)
{
  int res = impl_->insstr(pos, str);
  ASSERT(res != ERR);
}

void Window::insstr(Position pos, char const* str, int n)
{
  int res = impl_->insstr(pos, str, n);
  ASSERT(res != ERR);
}

void Window::inwstr(wchar_t* str) const
{
  int res = impl_->inwstr(str);
  ASSERT(res != ERR);
}

void Window::inwstr(wchar_t* str, int n) const
{
  int res = impl_->inwstr(str, n);
  ASSERT(res != ERR);
}

void Window::inwstr(Position pos, wchar_t* str) const
{
  int res = impl_->inwstr(pos, str);
  ASSERT(res != ERR);
}

void Window::inwstr(Position pos, wchar_t* str, int n) const
{
  int res = impl_->inwstr(pos, str, n);
  ASSERT(res != ERR);
}

void Window::curs_set(int visibility)
{
  int res = Impl::curs_set(visibility);
  ASSERT(res != ERR);
}

void Window::printw(char const* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  int res = impl_->printw(fmt, args);
  va_end(args);
  ASSERT(res != ERR);
}

void Window::vprintw(char const* fmt, va_list varglist)
{
  int res = impl_->printw(fmt, varglist);
  ASSERT(res != ERR);
}

void Window::printw(Position pos, char const* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  int res = impl_->printw(pos, fmt, args);
  va_end(args);
  ASSERT(res != ERR);
}

Window Window::subpad(Dimension size, Position pos)
{
  WINDOW* res = impl_->subpad(size, pos);
  ASSERT(res);
  return Window(std::make_unique<Impl>(res));
}

void Window::prefresh(Position pad_pos, Position screen_pos, Dimension screen_size)
{
  int res = impl_->prefresh(pad_pos, screen_pos, screen_size);
  ASSERT(res != ERR);
}

void Window::pnoutrefresh(Position pad_pos, Position screen_pos, Dimension screen_size)
{
  int res = impl_->pnoutrefresh(pad_pos, screen_pos, screen_size);
  ASSERT(res != ERR);
}

void Window::pechochar(ComplexChar const& complex_char)
{
  int res = impl_->pechochar(complex_char);
  ASSERT(res != ERR);
}

void Window::scrl(int n)
{
  int res = impl_->scrl(n);
  ASSERT(res != ERR);
}

void Window::key_name(wint_t key, std::string& name)
{
  char const* res = Impl::key_name(key);
  ASSERT(res);
  name = res;
}

void Window::move(Position pos)
{
  int res = impl_->move(pos);
  ASSERT(res != ERR);
}

void Window::resize(Dimension size)
{
  int res = impl_->resize(size);
  ASSERT(res != ERR);
}

Position Window::getyx() const
{
  return impl_->getyx();
}

Position Window::getbegyx() const
{
  return impl_->getbegyx();
}

Dimension Window::getmaxyx() const
{
  return impl_->getmaxyx();
}

std::optional<Position> Window::getparyx() const
{
  return impl_->getparyx();
}

bool Window::enclose(Position pos) const
{
  return impl_->enclose(pos);
}

bool Window::mouse_trafo(Position& pos, bool to_screen) const
{
  return impl_->mouse_trafo(pos, to_screen);
}

bool Window::is_cleared() const
{
  return impl_->is_cleared();
}

bool Window::is_idcok() const
{
  return impl_->is_idcok();
}

bool Window::is_idlok() const
{
  return impl_->is_idlok();
}

bool Window::is_immedok() const
{
  return impl_->is_immedok();
}

bool Window::is_keypad() const
{
  return impl_->is_keypad();
}

bool Window::is_leaveok() const
{
  return impl_->is_leaveok();
}

bool Window::is_nodelay() const
{
  return impl_->is_nodelay();
}

bool Window::is_notimeout() const
{
  return impl_->is_notimeout();
}

bool Window::is_pad() const
{
  return impl_->is_pad();
}

bool Window::is_scrollok() const
{
  return impl_->is_scrollok();
}

bool Window::is_subwin() const
{
  return impl_->is_subwin();
}

bool Window::is_syncok() const
{
  return impl_->is_syncok();
}

int Window::getdelay() const
{
  return impl_->getdelay();
}

ScrollRegion Window::getscrreg() const
{
  ScrollRegion region{};
  int res = impl_->getscrreg(region);
  ASSERT(res != ERR);
  return region;
}

} // namespace ava::tui::terminal
