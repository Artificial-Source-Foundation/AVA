#include "sys.h"
#include "TextRow.h"

namespace ava::tui::terminal {

// Determine how much of `source` still fits on this TextRow.
//
// If everything fits, move-append source to text_span_views_ and return a default constructed TextSpanView.
//
// For example if TextRow currently contains the TextSpanView's "current ", "CONTENT" and " ":
//
//           <-------max_cell_width_----->
//  this:   |current CONTENT #            |
//           ''''''''^^^^^^^'                 <--
//  source: |hello world|
//
//  result: |current CONTENT hello world# |
//           ''''''''^^^^^^^'^^^^^^^^^^^      <-- this line shows which characters below to which TextSpanView element (alternating ' and ~ characters).
//
// If the first word of `source` is longer than max_cell_width_, then
// use it to fill the current TextRow up till max_cell_width_ cells by truncating that word and return the remainder.
//
// For example,
//             <-------max_cell_width_----->
//  this:     |foo#                         |
//  source:   |AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA BBB|
//
//  result:   |fooAAAAAAAAAAAAAAAAAAAAAAAAAA|
//  returned: |AAAAAAAAAAA BBB|
//
// otherwise if nothing fits, return source itself.
//
// For example,
//             <-------max_cell_width_----->
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
//             <-------max_cell_width_----->
//  this:     |aaaaaaa bbbbbbb #            |
//  source:   |ccccc ddddd eeeeeee fffff ggggg hhhhh|
//
//  result:   |aaaaaaa bbbbbbb ccccc ddddd #|
//             ''''''''''''''''^^^^^^^^^^^^
//  returned: |eeeeeee fffff ggggg hhhhh|
//
//  or
//             <-------max_cell_width_----->
//  this:     |aaaaaaa bbbbbbb #            |
//  source:   |ccccc ddddddd    eeeeeee fffff ggggg hhhhh|
//
//  result:   |aaaaaaa bbbbbbb ccccc ddddddd|    #
//             ''''''''''''''''^^^^^^^^^^^^^^^^^^
//  returned: |eeeeeee fffff ggggg hhhhh|
//
TextSpanView TextRow::append(TextSpanView&& source)
{
  if (source.characters_.empty())
    // Pretend we succesfully appended source.
    return {};

  auto const source_begin = source.characters_.begin();
  auto prefix_end = source_begin;
  auto cursor = source_begin;
  size_t appended_width = 0;

  // Leading whitespace remains trailing whitespace on this row until another
  // word is accepted, so preserve all of it even when it crosses the limit.
  while (cursor != source.characters_.end() && cursor->whitespace)
  {
    appended_width += static_cast<size_t>(cursor->cell_width);
    prefix_end = ++cursor;
  }

  bool first_word = true;
  while (cursor != source.characters_.end())
  {
    auto const word_begin = cursor;
    size_t word_width = 0;
    while (cursor != source.characters_.end() && !cursor->whitespace)
    {
      word_width += static_cast<size_t>(cursor->cell_width);
      ++cursor;
    }

    if (cell_width_ + appended_width + word_width > max_cell_width_)
    {
      // Only an overlong first word may be split. A normally sized word that
      // does not fit in the remaining cells must start on the next row.
      if (first_word && word_width > max_cell_width_)
      {
        cursor = word_begin;
        while (cursor != source.characters_.end() && !cursor->whitespace &&
               cell_width_ + appended_width + static_cast<size_t>(cursor->cell_width) <= max_cell_width_)
        {
          appended_width += static_cast<size_t>(cursor->cell_width);
          prefix_end = ++cursor;
        }
      }
      break;
    }

    appended_width += word_width;
    prefix_end = cursor;
    first_word = false;

    // Whitespace following an accepted word stays on this row, including the
    // portion beyond max_cell_width_, because it is ignored for wrapping.
    while (cursor != source.characters_.end() && cursor->whitespace)
    {
      appended_width += static_cast<size_t>(cursor->cell_width);
      prefix_end = ++cursor;
    }
  }

  if (prefix_end == source_begin)
    return std::move(source);

  if (prefix_end == source.characters_.end())
  {
    cell_width_ += appended_width;
    text_span_views_.push_back(std::move(source));
    return {};
  }

  TextSpanView prefix;
  prefix.parent_ = source.parent_;
  prefix.characters_.assign(source_begin, prefix_end);
  source.characters_.erase(source_begin, prefix_end);

  cell_width_ += appended_width;
  text_span_views_.push_back(std::move(prefix));
  return std::move(source);
}

} // namespace ava::tui::terminal
