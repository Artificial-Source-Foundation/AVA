#include "sys.h"
#include "Spacer.h"

namespace ava::tui::terminal {

GraphemeBlock Spacer::create_grapheme_block() const
{
  GraphemeBlock block;
  block.emplace_back(assigned_width().columns(), HorizontalAlignment::left);
  return block;
}

} // namespace ava::tui::terminal
