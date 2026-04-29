#include <iostream>
#include <locale.h>
#include <curses.h>

// Test ncurses initialization and printing of UTF8 characters.

int main()
{
  // Needed for UTF8 characters to display properly (see https://stackoverflow.com/a/9927113/1487069).
  setlocale(LC_ALL, "");

  // The initscr code determines the terminal type and initializes all curses data structures.
  // initscr also causes the first call to refresh to clear the screen (after creating an empty `stdscr` that fills the screen).
  initscr();

  if (has_colors())
  {
    // From https://linux.die.net/man/3/init_color
    // The start_color routine requires no arguments. It must be called if the programmer wants to use colors,
    // and before any other color manipulation routine is called. It is good practice to call this routine right after initscr.
    start_color();

    // start_color initializes eight basic colors (black, red, green, yellow, blue, magenta, cyan, and white),
    // and two global variables, COLORS and COLOR_PAIRS (respectively defining the maximum number of colors and
    // color-pairs the terminal can support).
    std::cout << "COLORS = " << COLORS << ", COLOR_PAIRS = " << COLOR_PAIRS << "\r\n";
  }
  if (can_change_color())
    std::cout << "Colors can be changed.\r\n";

  // From https://man7.org/linux/man-pages/man3/curs_inopts.3x.html:
  //
  // The state of the terminal is unknown to a curses application when it
  // starts; therefore, a program should call cbreak or nocbreak explicitly.
  nocbreak();

  // Authors of most interactive programs prefer to do their own echoing
  // in a controlled area of the screen, or not to echo at all, so they
  // disable echoing by calling noecho. [See curs_getch(3X) for a discussion
  // of how these routines interact with cbreak and nocbreak.]
  noecho();

  // addstr adds a string to `stdscr`, the default screen-filling window.
  addstr("UTF-8: αβγ ✓ ─ │ ┌ ┐");

  // Get actual output to the terminal. This writes `stdscr` to the screen.
  refresh();

  // Wait for any key.
  cbreak();
  getch();

  // Close `stdscr` - revealing the previous screen content again.
  endwin();
}
