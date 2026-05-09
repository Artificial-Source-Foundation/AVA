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

    terminal::Window parent_window(size, top_left);
    parent_window.set_background(terminal_session.default_rendition());
    parent_window.set_border({});
    terminal::Window window = parent_window.derwin(terminal::Margin{.top = 1, .bottom = 1, .left = 1, .right = 1});
    auto long_line = u8"Dark red window: αβγ this line is longer than the width of the window.";
    window.addstr(long_line);
    terminal_session.stdscr().refresh();
    parent_window.refresh();
    window.refresh();
    wch = terminal_session.get_wch();
  }
  std::cout << "Program terminated with " << wch << std::endl;
}
