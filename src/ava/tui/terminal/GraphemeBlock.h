#pragma once

#include "GraphemeSpan.h"
#include "utils/Vector.h"

namespace ava::tui::terminal {

// A GraphemeBlock is a non-empty vertical sequence of equally wide GraphemeSpan rows.
//
// The block retains views into its source TextSpan objects, so those objects must outlive
// the block and any GraphemeSurface containing it.
struct GraphemeBlockCategory
{
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};
using GraphemeBlockIndex = utils::VectorIndex<GraphemeBlockCategory>;
using GraphemeBlock = utils::Vector<GraphemeSpan, GraphemeBlockIndex, core::Application::Vec8Alloc::rebind<GraphemeSpan>::other>;

inline uint32_t height_of(GraphemeBlock const& block)
{
  return static_cast<uint32_t>(block.size());
}
inline columns_t width_of(GraphemeBlock const& block)
{
  return block.front().max_columns();
}

} // namespace ava::tui::terminal
