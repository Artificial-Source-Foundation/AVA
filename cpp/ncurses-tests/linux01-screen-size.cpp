#include <iostream>
#include <locale.h>
#include <curses.h>

// Test getting the size of the screen / terminal.

int main()
{
  // See linux00-initialization.cpp
  setlocale(LC_ALL, "");
  initscr();
  nocbreak();
  noecho();

  //---------------------------------------------------------------------------
  // Get terminal size.

  int rows;
  int cols;

  // We get the size of `stdscr` - the full screen window - to get the size of the screen / terminal.
  getmaxyx(stdscr, rows, cols);

  //---------------------------------------------------------------------------

  // See linux00-initialization.cpp
  endwin();

  // Print the result.
  std::cout << "cols = " << cols << ", rows = " << rows << std::endl;
}
