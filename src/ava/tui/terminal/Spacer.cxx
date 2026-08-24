#include "sys.h"
#include "Spacer.h"
#include "BasicWindow.h"

namespace ava::tui::terminal {

void Spacer::write_to(BasicWindow& basic_window, Rendition const& default_rendition) const
{
  basic_window.attr_set(default_rendition);
  std::size_t number_of_spaces = assigned_width().value();
  std::wstring spaces(number_of_spaces, L' ');
  basic_window.addstr(spaces.data(), number_of_spaces);
}

} // namespace ava::tui::terminal
