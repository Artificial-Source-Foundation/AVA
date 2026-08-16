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
    terminal::Position const top_left{(terminal_context.rows() - size.height()) / 2, 10};
    terminal::Margin const margin{.top = 0, .bottom = 1, .left = 1, .right = 0};
    terminal::Rendition const dark_red_rendition(terminal_context.create_color_pair({}, {0x880000}));
    terminal::Window window(size, top_left, {margin, dark_red_rendition});
//    window.set_background(dark_red_rendition);

    auto long_line = u8"Dark red window: αβγ this line is longer than the width of the window.";
    window.addstr(long_line);
    terminal_context.stdscr().refresh();
    window.outer_window().refresh();
    window.refresh();
    wch = terminal_context.get_wch();
  }
  std::cout << "Program terminated with " << wch << std::endl;
}
