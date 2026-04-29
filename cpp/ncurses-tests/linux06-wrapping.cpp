#include <iostream>
#include <locale.h>
#include <curses.h>

short const dark_gray = 234;

int main()
{
  setlocale(LC_ALL, "");
  initscr();

  chtype background = A_NORMAL;

  if (has_colors())
  {
    start_color();

    printw("TERM=%s\n", getenv("TERM"));
    printw("COLORS=%d COLOR_PAIRS=%d\n", COLORS, COLOR_PAIRS);
    printw("can_change_color=%d\n", can_change_color());
    printw("tigetstr(initc)=%p\n", tigetstr((char*)"initc"));

    // I can't get can_change_color to work: it seems not possible to redefine the color of a given index.

    if (COLORS > dark_gray) {
      // Pair 1: white text on dark gray background.
      init_pair(1, COLOR_WHITE, dark_gray);
    }
    else
      init_pair(1, COLOR_WHITE, COLOR_BLACK);

    background = COLOR_PAIR(1);
  }

  cbreak();
  noecho();

  int height = 15;
  int width = 80;
  int y = LINES - height;
  int x = 10;

  WINDOW* win = newwin(height, width, y, x);

  // Set the background character/attribute for the whole window.
  wbkgd(win, background);
  werase(win);
  wrefresh(win);

  box(win, 0, 0);
  mvwaddstr(win, 1, 2, "Dark gray window");

  refresh();
  wrefresh(win);

  getch();
  delwin(win);
  endwin();
}
