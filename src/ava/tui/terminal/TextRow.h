#pragma once

#include "GraphemeRun.h"
#include <vector>

namespace ava::tui::terminal {

class TextRow
{
 private:
  std::size_t max_columns_;                             // The maximum number of terminal columns in this row, ignoring trailing white-space.
  std::size_t columns_ = 0;                             // The current number of terminal columns in this row, including trailing white-space.
  std::vector<GraphemeRun> grapheme_runs_;              // A list of GraphemeRun's that make up the row.

 public:
  // Construct an empty TextRow.
  TextRow(std::size_t max_columns) : max_columns_(max_columns) { }

  // Append as much of a GraphemeRun to the end as possible, returns what didn't fit.
  GraphemeRun append(GraphemeRun&& source);

  // Accessors.
  std::size_t max_columns() const { return max_columns_; }
  std::size_t columns() const { return columns_; }
  std::vector<GraphemeRun> const& grapheme_runs() const { return grapheme_runs_; }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
