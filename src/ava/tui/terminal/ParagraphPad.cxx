#include "sys.h"
#include "GraphemeRun.h"
#include "ParagraphPad.h"
#include "GraphemeSurface.h"
#include "utils/macros.h"

#include <vector>
#include "debug.h"

namespace ava::tui::terminal {

GraphemeSurface ParagraphPad::generate_grapheme_surface(columns_t columns)
{
  // Pass at least one terminal column so wrapping can always make progress.
  ASSERT(columns > 0);
  // Do not call `generate` unless this ParagraphPad contains at least one Paragraph.
  ASSERT(!paragraphs_.empty());

  // Wrap every Paragraph first: the total number of wrapped rows (plus one blank row between
  // consecutive Paragraph's) determines the height of the ncurses pad, which must be known when
  // the pad is created. The wrapped rows only contain views into the TextSpan's of the Paragraph's,
  // so keeping them around until the end is cheap apart from the wide characters and meta data.
  //
  // Reserve paragraphs_.size() number of GraphemeBlockRow's for the surface.
  GraphemeSurface wrapped_paragraphs(paragraphs_.size());
  uint32_t pad_height = 0;
  for (std::unique_ptr<Paragraph> const& paragraph : paragraphs_)
  {
    GraphemeBlockRow block_row(paragraph->create_grapheme_block(columns));
    pad_height += block_row.height();
    wrapped_paragraphs.append(std::move(block_row));
  }

  return wrapped_paragraphs;
}

void ParagraphPad::generate(columns_t columns, bool blank_line_between_block_rows)
{
  GraphemeSurface surface = generate_grapheme_surface(columns);

  // (Re)create the ncurses pad; the assignment destroys the previously generated pad, if any.
  pad_ = BasicWindow::newpad({surface.height() + (blank_line_between_block_rows ? surface.number_of_blocks_rows() - 1 : 0), surface.width()});

  // Write all wrapped rows into the new pad.
  auto paragraph_iter = paragraphs_.begin();
  uint32_t row = 0;
  for (GraphemeBlockRow const& block_row : surface.blocks_rows())
  {
    // There is only one block in each block row in this case.
    GraphemeBlock const& block = block_row.blocks().front();
    for (GraphemeSpan const& grapheme_span : block)
    {
      // Paranoia check: all GraphemeSpan's were constructed by passing `columns`.
      ASSERT(grapheme_span.max_columns() == columns);
      pad_->move(Position{row, 0});
      grapheme_span.write_to(*pad_, (*paragraph_iter)->default_rendition());
      ++row;
    }
    if (blank_line_between_block_rows)
      ++row;            // Leave one blank row between consecutive Paragraph's.
    ++paragraph_iter;
  }
}

void ParagraphPad::prefresh(Position pad_pos, Position screen_pos, Dimension screen_size)
{
  // Call `generate` before calling this function.
  ASSERT(pad_.has_value());
  pad_->prefresh(pad_pos, screen_pos, screen_size);
}

Dimension ParagraphPad::dimension() const
{
  // Call `generate` before calling this function.
  ASSERT(pad_.has_value());
  return pad_->getmaxyx();
}

BasicWindow& ParagraphPad::basic_window()
{
  // Call `generate` before calling this function.
  ASSERT(pad_.has_value());
  return *pad_;
}

} // namespace ava::tui::terminal
