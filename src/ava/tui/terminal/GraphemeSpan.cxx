#include "sys.h"
#include "GraphemeSpan.h"
#include "GraphemeRun.h"
#include "BasicWindow.h"
#include "TextSpan.h"
#include "Rendition.h"
#include "utils/macros.h"

#include <iterator>

namespace ava::tui::terminal {

// Determine how much of `source` still fits on this GraphemeSpan.
//
// If everything fits, move-append source to grapheme_runs_ and return a default constructed GraphemeRun.
//
// For example if GraphemeSpan currently contains the GraphemeRun's "current ", "CONTENT" and " ":
//
//           <--------max_columns_------->
//  this:   |current CONTENT #            |
//           ''''''''^^^^^^^'                 <--
//  source: |hello world|
//
//  result: |current CONTENT hello world# |
//           ''''''''^^^^^^^'^^^^^^^^^^^      <-- this line shows which characters belong to which GraphemeRun element (alternating ' and ~ characters).
//
// If the first word of `source` is longer than max_columns_, use it to fill the current GraphemeSpan by splitting
// that word only between compact grapheme clusters and return the remainder. A cluster wider than an otherwise
// empty row is appended whole, exceeding max_columns_, so wrapping can make progress without splitting it.
//
// For example,
//             <--------max_columns_------->
//  this:     |foo#                         |
//  source:   |AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA BBB|
//
//  result:   |fooAAAAAAAAAAAAAAAAAAAAAAAAAA|
//  returned: |AAAAAAAAAAA BBB|
//
// otherwise if nothing fits, return source itself.
//
// For example,
//             <--------max_columns_------->
//  this:     |foo#                         |
//  source:   |AAAAAAAAAAAAAAAAAAAAAAAAAAA BBB|
//
//  result:   |foo#                         |
//  returned: |AAAAAAAAAAAAAAAAAAAAAAAAAAA BBB|
//
// Otherwise, append all grapheme clusters up till the first white-space of `source` that still fit,
// plus any subsequent white-space even if they don't fit, as a single GraphemeRun to to this GraphemeSpan
// and return the remaining grapheme clusters as a GraphemeRun.
//
// For example,
//             <--------max_columns_------->
//  this:     |aaaaaaa bbbbbbb #            |
//  source:   |ccccc ddddd eeeeeee fffff ggggg hhhhh|
//
//  result:   |aaaaaaa bbbbbbb ccccc ddddd #|
//             ''''''''''''''''^^^^^^^^^^^^
//  returned: |eeeeeee fffff ggggg hhhhh|
//
//  or
//             <--------max_columns_------->
//  this:     |aaaaaaa bbbbbbb #            |
//  source:   |ccccc ddddddd    eeeeeee fffff ggggg hhhhh|
//
//  result:   |aaaaaaa bbbbbbb ccccc ddddddd|    #
//             ''''''''''''''''^^^^^^^^^^^^^^^^^^
//  returned: |eeeeeee fffff ggggg hhhhh|
//
GraphemeRun GraphemeSpan::append(GraphemeRun&& source)
{
  if (source.empty())
    // Pretend we succesfully appended source.
    return {};

  auto const source_begin = source.metadata().begin();
  auto const source_end = source.metadata().end();
  auto prefix_end = source_begin;
  auto cursor = source_begin;
  size_t appended_width = 0;

  // Leading whitespace remains trailing whitespace on this row until another
  // word is accepted, so preserve all of it even when it crosses the limit.
  while (cursor != source_end && cursor->whitespace)
  {
    appended_width += static_cast<size_t>(cursor->columns);
    prefix_end = ++cursor;
  }

  bool first_word = true;
  while (cursor != source_end)
  {
    auto const word_begin = cursor;
    size_t word_width = 0;
    while (cursor != source_end && !cursor->whitespace)
    {
      word_width += static_cast<size_t>(cursor->columns);
      ++cursor;
    }

    if (columns_ + appended_width + word_width > max_columns_)
    {
      // Only an overlong first word may be split. A normally sized word that
      // does not fit in the remaining cells must start on the next row.
      if (first_word && word_width > max_columns_)
      {
        cursor = word_begin;
        while (cursor != source_end && !cursor->whitespace && columns_ + appended_width <= max_columns_)
        {
          auto cluster_end = std::next(cursor);
          size_t cluster_width = static_cast<size_t>(cursor->columns);
          while (cluster_end != source_end && cluster_end->combining)
          {
            cluster_width += static_cast<size_t>(cluster_end->columns);
            ++cluster_end;
          }

          if (columns_ + appended_width + cluster_width > max_columns_)
          {
            // An indivisible cluster wider than an empty row must be kept whole so wrapping makes progress.
            if (columns_ == 0 && appended_width == 0)
            {
              appended_width = cluster_width;
              prefix_end = cluster_end;
              cursor = cluster_end;
            }
            break;
          }

          appended_width += cluster_width;
          prefix_end = cluster_end;
          cursor = cluster_end;
        }
      }
      break;
    }

    appended_width += word_width;
    prefix_end = cursor;
    first_word = false;

    // Whitespace following an accepted word stays on this row, including the
    // portion beyond max_columns_, because it is ignored for wrapping.
    while (cursor != source_end && cursor->whitespace)
    {
      appended_width += static_cast<size_t>(cursor->columns);
      prefix_end = ++cursor;
    }
  }

  if (prefix_end == source_begin)
    return std::move(source);

  if (prefix_end == source_end)
  {
    columns_ += appended_width;
    grapheme_runs_.push_back(std::move(source));
    return {};
  }

  // The prefix takes the first `prefix_size` entries of both parallel containers;
  // wide_[N] corresponds to metadata_[N].
  auto const prefix_size = static_cast<std::size_t>(prefix_end - source_begin);

  GraphemeRun prefix;
  prefix.text_span_ = source.text_span_;
  prefix.copy_prefix(source, prefix_size);
  source.remove_prefix(prefix_size);

  columns_ += appended_width;
  grapheme_runs_.push_back(std::move(prefix));
  return std::move(source);
}

// Write one GraphemeSpan into the ncurses handle `basic_window` at the current cursor position.
//
// Every addstr call continues right after where the previous one ended; the GraphemeRun's are simply concatenated.
// Each GraphemeRun is written with the rendition of its parent TextSpan, or with `default_rendition` when that
// TextSpan was created without a rendition of its own. The rendition is only passed to ncurses when it changes,
// and is restored at the end.
//
// Wrapping (for example of a Paragraph) may have kept trailing white-space past the end of the GraphemeSpan.
// Trailing white-space is still written - it carries the background color of its rendition - but is clipped
// at `max_columns_` terminal columns. A GraphemeSpan only contains spaces that go beyond its `max_columns_`,
// therefore nothing else will be clipped.
//
// Rows are extended to `max_columns_` terminal columns using spaces with `default_rendition`, so that the
// background of the whole row equals the paragraph background. A row that is written to the very last row
// of the basic_window (i.e. an ncurses window, subwindow or pad) can end exactly at the bottom-right corner.
// That is the benign ncurses corner case tolerated by BasicWindow::addstr (even though ncurses `addstr` returns
// ERR because it can't advance the cursor).
//
void GraphemeSpan::write_to(BasicWindow& basic_window, Rendition const& default_rendition) const
{
  // Track the rendition that ncurses would still use, starting from the current one,
  // so that an unchanged rendition never needs an attr_set call.
  Rendition original_rendition = basic_window.get_rendition();
  Rendition current_rendition{original_rendition};

  // Append `n` characters of the wide character string `str` to `basic_window` using `rendition`.
  auto&& addstr = [&basic_window, &current_rendition](wchar_t const* str, int n, Rendition const& rendition) {
    if (current_rendition != rendition)
    {
      basic_window.attr_set(rendition);
      current_rendition = rendition;
    }
    basic_window.addstr(str, n);
  };

  uint32_t remaining_columns = max_columns_;
  bool row_full = false;          // Set once a character was clipped; from then on only white-space may follow.

  for (GraphemeRun const& grapheme_run : grapheme_runs_)
  {
    // An empty GraphemeRun has nothing to write; just go to the next one.
    if (grapheme_run.empty())
      continue;

    // Determine how many wide characters of this grapheme_run fit (also) on the basic_window row.
    std::size_t count = 0;
    for (auto const& character_metadata : grapheme_run.metadata())
    {
      if (!row_full && static_cast<uint32_t>(character_metadata.columns) <= remaining_columns)
      {
        remaining_columns -= static_cast<uint32_t>(character_metadata.columns);
        ++count;
      }
      else
      {
        // Only white-space may exceed max_columns_. This should have been enforced by Paragraph::wrap.
        ASSERT(character_metadata.whitespace);
        // This should be just a space.
        ASSERT(character_metadata.columns == 1);
        // This follows because the first time we get here, a one-column character does not fit in remaining_columns.
        ASSERT(remaining_columns == 0);
        row_full = true;
      }
    }

    // Since grapheme_run is not empty there was at least one element in grapheme_run.characters_meta(),
    // therefore now either count is larger than 0 or row_full is true, or both.
    //
    // If count is zero then no character in this grapheme_run did fit on this row and all characters
    // truncated were whitespace (see the above ASSERT).
    if (count > 0)
    {
      TextSpan const* text_span = grapheme_run.text_span();
      Rendition const required_rendition = text_span->use_default_rendition() ? default_rendition : text_span->rendition();
      addstr(grapheme_run.str().data(), static_cast<int>(count), required_rendition);
    }

    // If the row is full then we expect any additional characters, of subsequent GraphemeRun's if any, to be whitespace.
    if (row_full)
      break;
  }

  while (remaining_columns > 0)
  {
    constexpr static uint32_t number_of_spaces = 32;
    constexpr static auto spaces = [] {
      std::array<wchar_t, number_of_spaces> result;
      result.fill(L' ');
      return result;
    }();
    addstr(spaces.data(), std::min(remaining_columns, number_of_spaces), default_rendition);
    if (AI_UNLIKELY(remaining_columns > number_of_spaces))
    {
      remaining_columns -= number_of_spaces;
      continue;
    }
    break;
  }

  // Restore the rendition that the basic_window had before writing this row.
  if (!(current_rendition == original_rendition))
    basic_window.attr_set(original_rendition);
}

} // namespace ava::tui::terminal
