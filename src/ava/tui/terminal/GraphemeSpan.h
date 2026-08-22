#pragma once

#include "GraphemeRun.h"
#include <vector>

namespace ava::tui::terminal {

class BasicWindow;
class Rendition;

// class GraphemeSpan
//
// A series of a adjacent GraphemeRun's.
//
// A GraphemeSpan does not necessarily contain `max_columns_` wide characters,
// even if the last GraphemeRun ends on white-space (WS):
//
//                       max_columns_
//   ┊◄-----------------------------------------------►┊
//   ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┯━━━━┯━━┓
//   ┃                                         ╎ WS │🯟🯝┃
//   ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┷━━━━┷━━┛
//   ┊◄--------------------------------------------►┊
//                       columns_
//
// It is also possible that the last GraphemeRun (or GraphemeRun's if the latter contain of only spaces)
// go beyond the end of the GraphemeSpan:
//
//                       max_columns_
//   ┊◄-----------------------------------------------►┊
//   ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┯━━━━┱───────┐
//   ┃                                            ╎ whitespace │
//   ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┷━━━━┹───────┘
//   ┊◄-------------------------------------------------------►┊
//                              columns_
//
// The `columns_` in the above examples are the concatenation of all the GraphemeRun::str_ wstring's.
//
class GraphemeSpan
{
 private:
  std::size_t const max_columns_;                       // The maximum number of terminal columns in this row, ignoring trailing white-space.
  std::size_t columns_;                                 // The current number of terminal columns in this row, including trailing white-space.
  std::vector<GraphemeRun> grapheme_runs_;              // A list of GraphemeRun's that make up the row.

 public:
  // Construct an empty GraphemeSpan.
  GraphemeSpan(std::size_t max_columns) : max_columns_(max_columns), columns_(0) { }

  // Move constructor.
  GraphemeSpan(GraphemeSpan&& collector) : max_columns_(collector.max_columns_), columns_(collector.columns_), grapheme_runs_(std::move(collector.grapheme_runs_))
  {
    // Reset the collector for further use(!) as if it was just constructed (empty).
    collector.columns_ = 0;
  }

  // Append as much of a GraphemeRun to the end as possible, returns what didn't fit.
  GraphemeRun append(GraphemeRun&& source);

  // Write the span to a Window.
  void write_to(BasicWindow& basic_window, Rendition const& default_rendition) const;

  // Accessors.
  std::size_t max_columns() const { return max_columns_; }
  std::size_t columns() const { return columns_; }
  std::vector<GraphemeRun> const& grapheme_runs() const { return grapheme_runs_; }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
