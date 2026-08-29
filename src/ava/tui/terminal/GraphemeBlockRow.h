#pragma once

#include "Paragraph.h"  // GraphemeBlock

namespace ava::tui::terminal {

// class GraphemeBlockRow
//
// A band of rows of a GraphemeSurface, over the full width.
// The number of rows is given by `height`, in terminal rows.
//
//                                   width_
// ┊◄----------------------------------------------------------------------►┊
// ┏━━━━━━━━━━━━━━━━━┯━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┯━━━━━━━━━━━━━━━━━━━━┓ ┄
// ┃                 │                                 │                    ┃ ▲
// ┃                 │                                 │                    ┃ ┆
// ┃ GraphemeBlock 0 │         GraphemeBlock 1         │   GraphemeBlock 2  ┃ ┆ height_
// ┃                 │                                 │                    ┃ ┆
// ┃                 │                                 │                    ┃ ▼
// ┗━━━━━━━━━━━━━━━━━┷━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┷━━━━━━━━━━━━━━━━━━━━┛ ┄
//
class GraphemeBlockRow
{
 private:
  std::vector<GraphemeBlock> blocks_;           // Horizontally stacked GraphemeBlock's spanning the full width of the containing GraphemeSurface.
  uint32_t height_{};                           // The height of the block row; the largest height of any block.
  columns_t width_{};                           // The width of the block row, in terminal columns.

 public:
  // Construct an empty GraphemeBlockRow with a pre-allocated capacity for `reserved_blocks` GraphemeBlock's.
  GraphemeBlockRow(std::size_t reserved_blocks)
  {
    blocks_.reserve(reserved_blocks);
  }

  // Construct a GraphemeBlockRow from one or more horizontally stacked GraphemeBlock's.
  GraphemeBlockRow(std::vector<GraphemeBlock>&& blocks) : blocks_(std::move(blocks))
  {
    // There must be at least one block (otherwise this can impossibly span the full width of the containing GraphemeSurface).
    ASSERT(!blocks_.empty());
    for (GraphemeBlock const& block : blocks_)
    {
      height_ = std::max(height_, height_of(block));
      width_ += width_of(block);
    }
  }

  // Construct a GraphemeBlockRow that exists of a single GraphemeBlock.
  GraphemeBlockRow(GraphemeBlock&& block)
  {
    append(std::move(block));
  }

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
