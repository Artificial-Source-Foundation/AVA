#include "sys.h"
#include "terminal/Context.h"
#include "terminal/Spacer.h"
#include "terminal/Paragraph.h"
#include "terminal/HorizontalLayout.h"
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

  auto exit_text = terminal::TextSpan::create(u8"Exit the app", {.priority = 1});
  auto spacer = terminal::Spacer::create();
  auto shortcuts = terminal::Paragraph::create({.priority = 2, .alignment = terminal::HorizontalAlignment::right});
  auto shortcuts_text = terminal::TextSpan::create(u8"ctrl+c, ctrl+d, ctrl+x q");
  shortcuts->append(std::move(shortcuts_text));
  shortcuts->initialize_cached_natural_width();

  terminal::HorizontalLayout horizontal_layout;
  horizontal_layout.append(std::move(exit_text));
  horizontal_layout.append(std::move(spacer));
  horizontal_layout.append(std::move(shortcuts));

  horizontal_layout.set_width(17);
  Dout(dc::notice, "horizontal_layout = " << horizontal_layout);

  //... display it
  //... allow resizing with keyboard
}
