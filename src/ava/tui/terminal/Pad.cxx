#include "sys.h"
#include "Pad.h"
#include "utils/macros.h"

#include <vector>
#include "debug.h"

namespace ava::tui::terminal {

namespace {

// Write one wrapped TextRow of a Paragraph into the ncurses pad `pad`, at row `row`.
//
// The cursor is moved to the start of the row once; every addstr call then continues
// right after where the previous one ended, so the TextSpanView's of the row simply
// catenate. Each TextSpanView is written with the rendition of its parent TextSpan,
// or with `default_rendition` when that TextSpan was created without a rendition of
// its own. The rendition is only passed to ncurses when it actually changes, and it
// is restored to what it was before this row at the end.
//
// Wrapping may have kept trailing white-space on the row that extends past the right
// edge of the pad (`pad_width` cells). That white-space is still written - it carries
// the background color of its rendition - but clipped at `pad_width` cells, so that
// the cursor cannot wrap onto the next row. Nothing else may be clipped: the rest of
// the row fits in `pad_width` cells because Paragraph::wrap ran with this same width
// (this is asserted).
//
// Rows are filled up to `pad_width` cells with spaces using `default_rendition`, so that
// the background of the whole row equals the paragraph background. A row that is written
// to the very last row of the pad can end exactly at the bottom-right corner of the pad;
// that is the benign ncurses corner case tolerated by BasicWindow::addstr (even though
// ncurses `addstr` returns ERR because it can't advance the cursor).
void write_row(BasicWindow& pad, TextRow const& text_row, uint32_t row, uint32_t pad_width, Rendition const& default_rendition)
{
  pad.move(Position{row, 0});

  // Track the rendition that ncurses would still use, starting from the current one,
  // so that an unchanged rendition never needs an attr_set call.
  Rendition original_rendition = pad.get_rendition();
  Rendition current_rendition{original_rendition};

  // Append `n` characters of the wide character string `str` to `pad` using `rendition`.
  auto&& addstr = [&pad, &current_rendition](wchar_t const* str, int n, Rendition const& rendition){
    if (current_rendition != rendition)
    {
      pad.attr_set(rendition);
      current_rendition = rendition;
    }
    pad.addstr(str, n);
  };

  uint32_t remaining_width = pad_width;
  bool row_full = false;          // Set once a character was clipped; from then on only white-space may follow.

  for (TextSpanView const& text_span_view : text_row.text_span_views())
  {
    // An empty TextSpanView has nothing to write; just go to the next one.
    if (text_span_view.empty())
      continue;

    // Determine how many wide characters of this text_span_view.wide_characters_ fit (also) on the pad row.
    std::size_t count = 0;
    for (auto const& character_meta : text_span_view.characters().meta())
    {
      if (!row_full && static_cast<uint32_t>(character_meta.cell_width) <= remaining_width)
      {
        remaining_width -= static_cast<uint32_t>(character_meta.cell_width);
        ++count;
      }
      else
      {
        // Only white-space may exceed the pad_width. This should have been enforced by Paragraph::wrap.
        ASSERT(character_meta.whitespace);
        // This should be just a space.
        ASSERT(character_meta.cell_width == 1);
        // This implied from the fact that the first time we get here, with character_meta.cell_width == 1, !(1 <= remaining_width).
        ASSERT(remaining_width == 0);
        row_full = true;
      }
    }

    // Since text_span_view is not empty there was at least one element in text_span_view.characters_meta(),
    // therefore now either count is larger than 0 or row_full is true, or both.
    //
    // If count is zero then no character in this text_span_view did fit on this row and all characters
    // truncated were whitespace (see the above ASSERT).
    if (count > 0)
    {
      TextSpan const* text_span = text_span_view.text_span();
      Rendition const required_rendition = text_span->use_default_rendition() ? default_rendition : text_span->rendition();
      addstr(text_span_view.characters().wide().data(), static_cast<int>(count), required_rendition);
    }

    // If the row is full then we expect any additional characters, of subsequent TextSpanView's if any, to be whitespace.
    if (row_full)
      break;
  }

  while (remaining_width > 0)
  {
    constexpr static uint32_t number_of_spaces = 32;
    constexpr static auto spaces = [] {
      std::array<wchar_t, number_of_spaces> result;
      result.fill(L' ');
      return result;
    }();
    addstr(spaces.data(), std::min(remaining_width, number_of_spaces), default_rendition);
    if (AI_UNLIKELY(remaining_width > number_of_spaces))
    {
      remaining_width -= number_of_spaces;
      continue;
    }
    break;
  }

  // Restore the rendition that the pad had before writing this row.
  if (!(current_rendition == original_rendition))
    pad.attr_set(original_rendition);
}

} // namespace

void Pad::generate(uint32_t cell_width)
{
  // Don't pass a cell_width of zero.
  ASSERT(cell_width > 0);

  // Wrap every Paragraph first: the total number of wrapped rows (plus one blank row between
  // consecutive Paragraph's) determines the height of the ncurses pad, which must be known when
  // the pad is created. The wrapped rows only contain views into the TextSpan's of the Paragraph's,
  // so keeping them around until the end is cheap apart from the wide characters and meta data.
  std::vector<std::vector<TextRow>> wrapped_paragraphs;
  wrapped_paragraphs.reserve(paragraphs_.size());
  uint32_t pad_height = 0;
  for (std::unique_ptr<Paragraph> const& paragraph : paragraphs_)
  {
    wrapped_paragraphs.push_back(paragraph->wrap(cell_width));
    pad_height += static_cast<uint32_t>(wrapped_paragraphs.back().size());
  }
  if (wrapped_paragraphs.size() > 1)
    pad_height += static_cast<uint32_t>(wrapped_paragraphs.size() - 1);

  // (Re)create the ncurses pad; the assignment destroys the previously generated pad, if any.
  pad_ = BasicWindow::newpad(Dimension{pad_height, cell_width});

  // Write all wrapped rows into the new pad.
  auto paragraph_iter = paragraphs_.begin();
  uint32_t row = 0;
  for (std::vector<TextRow> const& rows : wrapped_paragraphs)
  {
    for (TextRow const& text_row : rows)
    {
      write_row(*pad_, text_row, row, cell_width, (*paragraph_iter)->default_rendition());
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
