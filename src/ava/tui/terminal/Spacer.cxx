#include "sys.h"
#include "Spacer.h"
#include "BasicWindow.h"
#include "debug.h"

namespace ava::tui::terminal {

void Spacer::write_to(BasicWindow& basic_window, Rendition const& default_rendition) const
{
  DoutEntering(dc::terminal, "Spacer::write_to(" << basic_window << ", " << default_rendition << ")");

  Rendition const current_rendition = basic_window.current_rendition();
  basic_window.addspaces(assigned_width().value(), default_rendition);
  basic_window.restore_rendition(current_rendition);
}

} // namespace ava::tui::terminal
