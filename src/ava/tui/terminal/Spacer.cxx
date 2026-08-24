#include "sys.h"
#include "Spacer.h"
#include "BasicWindow.h"

namespace ava::tui::terminal {

void Spacer::write_to(BasicWindow& basic_window, Rendition const& default_rendition) const
{
  Rendition const current_rendition = basic_window.current_rendition();
  basic_window.addspaces(assigned_width().value(), default_rendition);
  basic_window.restore_rendition(current_rendition);
}

} // namespace ava::tui::terminal
