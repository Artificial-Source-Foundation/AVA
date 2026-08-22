#include "sys.h"
#include "terminal/Context.h"
#include "terminal/HorizontalLayout.h"
#include "terminal/Paragraph.h"
#include "terminal/Spacer.h"

#include "debug.h"

namespace terminal = ava::tui::terminal;

int main()
{
  Debug(NAMESPACE_DEBUG::init());
  Debug(libcw_do.always_flush_on());

  std::mutex log_mutex;
  std::ofstream log("debug.out");
  Debug(libcw_do.set_ostream(&log, &log_mutex));

  terminal::Context terminal_context;
  terminal::BasicWindow const& stdscr = terminal_context.stdscr();

  terminal::HorizontalLayout horizontal_layout1;
  terminal::HorizontalLayout horizontal_layout2;
  terminal::HorizontalLayout horizontal_layout3;

  {
    auto exit_text = terminal::TextSpan::create(u8"Exit the app", {.priority = 1});
    auto spacer = terminal::Spacer::create();
    auto shortcuts = terminal::Paragraph::create({.priority = 2, .alignment = terminal::HorizontalAlignment::right});
    auto shortcuts_text = terminal::TextSpan::create(u8"ctrl+c, ctrl+d, ctrl+x q");
    shortcuts->append(std::move(shortcuts_text));
    shortcuts->initialize_cached_natural_width();

    horizontal_layout1.append(std::move(exit_text));
    horizontal_layout1.append(std::move(spacer));
    horizontal_layout1.append(std::move(shortcuts));

    horizontal_layout1.set_width(17);
    Dout(dc::notice, "horizontal_layout1 = " << horizontal_layout1);
  }

  {
    //   index i    priority   minimum width   natural width
    //   0          5          4               13
    //   1          2          8               14
    //   2          2          5               12
    //   3          0          3               unlimited

    auto item0 = terminal::TextSpan::create(u8"1234567890123", {.priority = 5, .minimum_width = 4});
    auto item1 = terminal::TextSpan::create(u8"12345678901234", {.priority = 2, .minimum_width = 8});
    auto item2 = terminal::TextSpan::create(u8"123456789012", {.priority = 2, .minimum_width = 5});
    auto item3 = terminal::Spacer::create({.minimum_width = 3});

    horizontal_layout2.append(std::move(item3));
    horizontal_layout2.append(std::move(item0));
    horizontal_layout2.append(std::move(item2));
    horizontal_layout2.append(std::move(item1));

    horizontal_layout2.set_width(32);
    Dout(dc::notice, "horizontal_layout2 = " << horizontal_layout2);
  }

  {
    //                      A         B         C
    //   minimum width |    5         7        11
    //   natural width |   20        31        17
    //          weight |  1.0       1.5       2.0

    auto item_A = terminal::TextSpan::create(u8"12345678901234567890", {.weight = 1, .minimum_width = 5});
    auto item_B = terminal::TextSpan::create(u8"1234567890123456789012345678901", {.weight = 1.5, .minimum_width = 7});
    auto item_C = terminal::TextSpan::create(u8"12345678901234567", {.weight = 2, .minimum_width = 11});
    horizontal_layout3.append(std::move(item_A));
    horizontal_layout3.append(std::move(item_B));
    horizontal_layout3.append(std::move(item_C));

    horizontal_layout3.set_width(43);
    Dout(dc::notice, "horizontal_layout3 = " << horizontal_layout3);
  }

  terminal::Position top_left{5, 5};
//  horizontal_layout1.show(stdscr, top_left);

  //... display it
  //... allow resizing with keyboard
}
