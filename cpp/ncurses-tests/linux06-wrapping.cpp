#include "terminal/Session.h"
#include "terminal/Window.h"
#include <iostream>

int main()
{
  int wch;
  {
    terminal::Session terminal_session;

    terminal::Dimension const size{15, 20};
    terminal::Position const top_left{terminal_session.rows() - size.height(), 10};

    terminal::Window window(size, top_left);
    window.set_background(terminal_session.default_rendition());
    window.set_border({});
    auto long_line = u8"Dark red window: αβγ this line is longer than the width of the window.";
    window.addstr({1, 2}, long_line);
    terminal_session.stdscr().refresh();
    window.refresh();
    wch = terminal_session.get_wch();
  }
  std::cout << "Program terminated with " << wch << std::endl;
}
