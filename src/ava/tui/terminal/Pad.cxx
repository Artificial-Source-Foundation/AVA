#include "sys.h"
#include "GraphemeRun.h"
#include "Pad.h"
#include "utils/macros.h"

#include <vector>
#include "debug.h"

namespace ava::tui::terminal {

void Pad::generate(columns_t columns)
{
  // Pass at least one terminal column so wrapping can always make progress.
  ASSERT(columns > 0);

  // Wrap every Paragraph first: the total number of wrapped rows (plus one blank row between
  // consecutive Paragraph's) determines the height of the ncurses pad, which must be known when
  // the pad is created. The wrapped rows only contain views into the TextSpan's of the Paragraph's,
  // so keeping them around until the end is cheap apart from the wide characters and meta data.
  std::vector<std::vector<GraphemeSpan>> wrapped_paragraphs;
  wrapped_paragraphs.reserve(paragraphs_.size());
  uint32_t pad_height = 0;
  for (std::unique_ptr<Paragraph> const& paragraph : paragraphs_)
  {
    wrapped_paragraphs.push_back(paragraph->create_grapheme_spans(columns));
    pad_height += static_cast<uint32_t>(wrapped_paragraphs.back().size());
  }
  if (wrapped_paragraphs.size() > 1)
    pad_height += static_cast<uint32_t>(wrapped_paragraphs.size() - 1);

  // (Re)create the ncurses pad; the assignment destroys the previously generated pad, if any.
  pad_ = BasicWindow::newpad(Dimension{pad_height, columns});

  // Write all wrapped rows into the new pad.
  auto paragraph_iter = paragraphs_.begin();
  uint32_t row = 0;
  for (std::vector<GraphemeSpan> const& rows : wrapped_paragraphs)
  {
    for (GraphemeSpan const& grapheme_span : rows)
    {
      // Paranoia check: all GraphemeSpan's were constructed by passing `columns`.
      ASSERT(grapheme_span.max_columns() == columns);
      pad_->move(Position{row, 0});
      grapheme_span.write_to(*pad_, (*paragraph_iter)->default_rendition());
      ++row;
    }
    ++row;               // Leave one blank row between consecutive Paragraph's.
    ++paragraph_iter;
  }
}

void Pad::prefresh(Position pad_pos, Position screen_pos, Dimension screen_size)
{
  // Call `generate` before calling this function.
  ASSERT(pad_.has_value());
  pad_->prefresh(pad_pos, screen_pos, screen_size);
}

Dimension Pad::dimension() const
{
  // Call `generate` before calling this function.
  ASSERT(pad_.has_value());
  return pad_->getmaxyx();
}

BasicWindow& Pad::basic_window()
{
  // Call `generate` before calling this function.
  ASSERT(pad_.has_value());
  return *pad_;
}

} // namespace ava::tui::terminal
