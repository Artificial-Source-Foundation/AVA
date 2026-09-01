#pragma once

#include "GraphemeBlock.h"

namespace ava::tui::terminal {

// class GraphemeBlockRow
//
// A horizontal band in a GraphemeSurface. Its width is the sum of its horizontally stacked
// GraphemeBlock widths, and its height is the largest height of any block.
//
//                                   width_
// ┊◄----------------------------------------------------------------------►┊
// ┏━━━━━━━━━━━━━━━━━┯━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┯━━━━━━━━━━━━━━━━━━━━┓ ┄
// ┃                 │                                 │   GraphemeBlock 2  ┃ ▲
// ┃ GraphemeBlock 0 │                                 ├────────────────────┨ ┆
// ┃                 │         GraphemeBlock 1         │╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲┃ ┆ height_
// ┠─────────────────┤                                 │╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲┃ ┆
// ┃╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲│                                 │╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲╲┃ ▼
// ┗━━━━━━━━━━━━━━━━━┷━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┷━━━━━━━━━━━━━━━━━━━━┛ ┄
//
class GraphemeBlockRow
{
 private:
  std::vector<GraphemeBlock> blocks_;           // Horizontally stacked GraphemeBlock's spanning width_ terminal columns.
  uint32_t height_{};                           // The height of the block row; the largest height of any block.
  columns_t width_{};                           // The width of the block row, in terminal columns.

 public:
  // Construct an empty GraphemeBlockRow with a pre-allocated capacity for `reserved_blocks` GraphemeBlock's.
  GraphemeBlockRow(std::size_t reserved_blocks) { blocks_.reserve(reserved_blocks); }

  // Construct a GraphemeBlockRow from one or more horizontally stacked GraphemeBlock's.
  GraphemeBlockRow(std::vector<GraphemeBlock>&& blocks) : blocks_(std::move(blocks))
  {
    // Pass at least one GraphemeBlock so this GraphemeBlockRow has a known width.
    ASSERT(!blocks_.empty());
    for (GraphemeBlock const& block : blocks_)
    {
      height_ = std::max(height_, height_of(block));
      width_ += width_of(block);
    }
  }

  // Construct a GraphemeBlockRow that exists of a single GraphemeBlock.
  GraphemeBlockRow(GraphemeBlock&& block) { append(std::move(block)); }

  void append(GraphemeBlock&& block)
  {
    height_ = std::max(height_, height_of(block));
    width_ += width_of(block);
    blocks_.emplace_back(std::move(block));
  }

  // Accessors.

  std::vector<GraphemeBlock> const& blocks() const { return blocks_; }
  uint32_t height() const { return height_; }
  columns_t width() const { return width_; }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
