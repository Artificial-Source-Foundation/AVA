#include "sys.h"
#include "TextRow.h"

#include <iterator>

namespace ava::tui::terminal {

// Determine how much of `source` still fits on this TextRow.
//
// If everything fits, move-append source to text_span_views_ and return a default constructed TextSpanView.
//
// For example if TextRow currently contains the TextSpanView's "current ", "CONTENT" and " ":
//
//           <--------max_columns_------->
//  this:   |current CONTENT #            |
//           ''''''''^^^^^^^'                 <--
//  source: |hello world|
//
//  result: |current CONTENT hello world# |
//           ''''''''^^^^^^^'^^^^^^^^^^^      <-- this line shows which characters below to which TextSpanView element (alternating ' and ~ characters).
//
// If the first word of `source` is longer than max_columns_, use it to fill the current TextRow by splitting
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
// Otherwise, append all Character's up till the first white-space Character of `source` that still fit,
// plus any subsequent white-space Characters even if they don't fit, as a single TextSpanView to
// to this TextRow and return the remaining Characters as a TextSpanView.
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
TextSpanView TextRow::append(TextSpanView&& source)
{
  if (source.empty())
    // Pretend we succesfully appended source.
    return {};

  auto const source_begin = source.characters().meta().begin();
  auto const source_end = source.characters().meta().end();
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
    text_span_views_.push_back(std::move(source));
    return {};
  }

  // The prefix takes the first `prefix_size` entries of both parallel containers;
  // wide_characters_[N] corresponds to characters_meta_[N].
  auto const prefix_size = static_cast<std::size_t>(prefix_end - source_begin);

  TextSpanView prefix;
  prefix.text_span_ = source.text_span_;
  prefix.characters().copy_prefix(source.characters(), prefix_size);
  source.characters().remove_prefix(prefix_size);

  columns_ += appended_width;
  text_span_views_.push_back(std::move(prefix));
  return std::move(source);
}

} // namespace ava::tui::terminal
