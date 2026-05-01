#include "terminal/Session.h"
#include "terminal/Window.h"
#include <iostream>

int main()
{
  int wch;
  {
    terminal::Session terminal_session;

    int height = 15;
    int width = 80;
    int y = terminal_session.rows() - height;
    int x = 10;

    terminal::Window window(height, width, y, x);
    window.set_background(terminal_session.get_background_cchar());
    window.erase();
    window.refresh();
    window.box(0, 0);
    window.addstr(1, 2, "Dark red window");
    terminal_session.refresh();
    window.refresh();
    wch = terminal_session.get_wch();
  }
  std::cout << "Program terminated with " << wch << std::endl;
}
