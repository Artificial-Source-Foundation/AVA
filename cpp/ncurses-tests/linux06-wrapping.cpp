#include "terminal/Session.h"
#include "terminal/Window.h"
#include <iostream>

int main()
{
  int wch;
  {
    terminal::Session terminal_session;

    int height = 15;
    int width = 20;
    int y = terminal_session.rows() - height;
    int x = 10;

    terminal::Window window(height, width, y, x);
    window.set_background(terminal_session.default_rendition());
    window.set_border({});
    auto long_line = u8"Dark red window: αβγ this line is longer than the width of the window.";
    window.addstr(1, 2, long_line);
    terminal_session.stdscr().refresh();
    window.refresh();
    wch = terminal_session.get_wch();
  }
  std::cout << "Program terminated with " << wch << std::endl;
}
