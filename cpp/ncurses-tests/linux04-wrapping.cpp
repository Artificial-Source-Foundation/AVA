#include "terminal/Session.h"
#include <iostream>
#include <curses.h>

int main()
{
  wint_t wch;
  {
    terminal::Session terminal_session;

    int height = 15;
    int width = 80;
    int y = terminal_session.rows() - height;
    int x = 10;

    WINDOW* win = newwin(height, width, y, x);

    // Set the background character/attribute for the whole window.
    wbkgrnd(win, terminal_session.get_background_cchar());
    werase(win);
    wrefresh(win);

    box(win, 0, 0);
    mvwaddstr(win, 1, 2, "Dark red window");

    refresh();
    wrefresh(win);
    get_wch(&wch);
    delwin(win);
  }
  std::cout << "Program terminated with " << wch << std::endl;
}
