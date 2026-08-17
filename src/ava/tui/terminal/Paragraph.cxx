#include "sys.h"
#include "Paragraph.h"

#include "debug.h"

namespace ava::tui::terminal {

// Perform wrapping: return a list of TextRow's.
//
// A catenation of all TextSpan's of each TextRow results in the same text with
// the same rendition as if all TextSpan's of the Paragraph were catenated.
// However, a single TextSpan of this Paragraph might have been split up into
// two (or more) TextSpan. The catenation of each TextSpan in any given TextRow
// has a cell width that is less than or equal `cell_width` *after* stripping
// all white-space characters at the end.
//
// For example, if the Paragraph contains:
//
// "AAAAAAAAAAA  bbb ccc ddd EEEEEEEE FFFFF  GGGGG hhhhh iii j kkkk lll  MMMMM N OOO ppp Q  rrr  sss ttt uuuu vvvv wwww xxx yyy  ZZZ"
//
// Where each space stands for 'white space' at which we can break a line,
// and upper/lower can represents different renditions (e.g. lower case is the "default" rendition)
// then the list of TextSpan's of the Paragraph could be:
//
//  * "AAAAAAAAAAA "
//  * " bbb ccc ddd "
//  * "EEEEEEEE FFFFF  GGGGG"
//  * " hhhhh iii j kkkk lll  "
//  * "MMMMM N OOO"
//  * " ppp "
//  * "Q "
//  * " rrr  sss ttt uuuu vvvv wwww xxx yyy "
//  * " ZZZ"
//
// which, after catenating gives the above string.
//
// Wrapping using a cell_width of 9 then should give:
//
// row    width       TextSpanView list
//      <---9--->
//  1  |AAAAAAAAA#    "AAAAAAAAA"
//  2  |AA  bbb #     "AA ", " bbb "
//  3  |ccc ddd #     "ccc ddd "
//  4  |EEEEEEEE #    "EEEEEEEE "
//  5  |FFFFF  #      "FFFFF  "
//  6  |GGGGG #       "GGGGG", " "
//  7  |hhhhh iii #   "hhhhh iii "                (a width of 10: one extra trailing space)
//  8  |j kkkk #      "j kkkk "
//  9  |lll  #        "lll  "
// 10  |MMMMM N #     "MMMMM N "
// 11  |OOO ppp Q  #  "OOO", " ppp ", "Q ", " "   (a width of 11: two extra trailing spaces)
// 12  |rrr  sss #    "rrr  sss "
// 13  |ttt uuuu #    "ttt uuuu "
// 14  |vvvv wwww #   "vvvv wwww "                (a width of 10: one extra trailing space)
// 15  |xxx yyy  #    "xxx yyy ", " "
// 16  |ZZZ#          "ZZZ"
//
// Note how all TextSpanView's in this list are views into the existing TextSpan's of the Paragraph.
// The catenation of all TextSpanView's gives again the original string.
//
std::vector<TextRow> Paragraph::wrap(uint32_t cell_width)
{
  // Don't pass a cell_width of zero.
  ASSERT(cell_width > 0);

  std::vector<TextRow> rows;
  TextRow row{static_cast<size_t>(cell_width)};

  for (std::unique_ptr<TextSpan> const& text_span : text_spans_)
  {
    // Feed this whole TextSpan to TextRow::append, starting a new TextRow for
    // whatever didn't fit anymore, until the TextSpan is fully consumed.
    TextSpanView view{*text_span};
    while (!view.empty())
    {
      TextSpanView remainder = row.append(std::move(view));
      if (remainder.empty())
        break;
      rows.push_back(std::move(row));
      row = TextRow{static_cast<size_t>(cell_width)};
      view = std::move(remainder);
    }
  }

  if (!row.text_span_views().empty())
    rows.push_back(std::move(row));

  return rows;
}

} // namespace ava::tui::terminal
