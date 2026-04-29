#include <iostream>
#include <sstream>
#include <locale.h>
#include <curses.h>

// Test resizing of terminal windows and being notified.

int main()
{
  // See linux00-initialization.cpp
  setlocale(LC_ALL, "");
  // From https://man7.org/linux/man-pages/man3/resizeterm.3x.html:
  //
  // If the application has not set up a handler for SIGWINCH when it
  // initializes ncurses (by calling initscr(3X) or newterm(3X)), then
  // ncurses establishes a SIGWINCH handler that notifies the library
  // when a window-resizing event has occurred.  The library checks for
  // this notification.
  initscr();
  nocbreak();
  noecho();

  //---------------------------------------------------------------------------
  // From https://man7.org/linux/man-pages/man3/curs_getch.3x.html:
  //
  // wgetch returns KEY_RESIZE, even if the window's keypad mode is disabled,
  // if ncurses has handled a SIGWINCH signal since wgetch was called;
  // see initscr(3X) and resizeterm(3X).
  // [...]
  // Except for the special case KEY_RESIZE, it is necessary to enable keypad for getch to return these codes.

  //keypad(stdscr, TRUE);       // Not necessary for KEY_RESIZE.

  int ch;
  int line_feed_count = 0;
  do
  {
    // Get current terminal size and print it.
    int rows;
    int cols;
    getmaxyx(stdscr, rows, cols);
    std::ostringstream text;
    text << "cols = " << cols << ", rows = " << rows << '\n';
    // Show new values on the screen.
    addstr(text.str().c_str());
    refresh();
    // getch() is the same as wgetch(stdscr).
    ch = getch();
    if (ch == 10)
      ++line_feed_count;
  }
  while (ch == KEY_RESIZE || ch == 10);
  // Note that KEY_RESIZE is delivered despite the nocbreak() because it doesn't originate from the terminal.

  //---------------------------------------------------------------------------

  // See linux00-initialization.cpp
  endwin();

  std::cout << "Terminated due to character " << ch << std::endl;
  std::cout << "line_feed_count = " << line_feed_count << std::endl;
}
