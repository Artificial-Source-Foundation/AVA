#include "terminal/Context.h"
#include "terminal/Window.h"

#include <iostream>

namespace terminal = ava::tui::terminal;

int main()
{
  int wch;
  {
    terminal::Context terminal_context;

    terminal::Dimension const size{15, 20};
    terminal::Position const top_left{terminal_context.rows() - size.height(), 10};

    terminal::Window parent_window(size, top_left);
    parent_window.set_background(terminal_context.default_rendition());
    parent_window.set_border({});
    terminal::Window window = parent_window.derwin(terminal::Margin{.top = 1, .bottom = 1, .left = 1, .right = 1});
    auto long_line = u8"Dark red window: αβγ this line is longer than the width of the window.";
    window.addstr(long_line);
    terminal_context.stdscr().refresh();
    parent_window.refresh();
    window.refresh();
    wch = terminal_context.get_wch();
  }
  std::cout << "Program terminated with " << wch << std::endl;
}
