#pragma once

#include "TextSpan.h"
#include <vector>

namespace ava::tui::terminal {

class TextRow
{
 private:
  size_t max_cell_width_;                               // The maximum width, ignoring trailing white-space, in terminal cell characters of this row.
  size_t cell_width_ = 0;                               // The current width in terminal cells of this row, including trailing white-space.
  std::vector<TextSpanView> text_span_views_;           // A list of TextSpanView's that make up the row.

 public:
  // Construct an empty TextRow.
  TextRow(size_t max_cell_width) : max_cell_width_(max_cell_width) { }

  // Append as much of a TextSpanView to the end as possible, returns what didn't fit.
  TextSpanView append(TextSpanView&& source);

  // Accessors.
  size_t max_cell_width() const { return max_cell_width_; }
  size_t cell_width() const { return cell_width_; }
  std::vector<TextSpanView> const& text_span_views() const { return text_span_views_; }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
