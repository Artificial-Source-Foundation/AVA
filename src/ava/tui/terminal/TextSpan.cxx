#include "sys.h"
#include "BasicWindow.h"
#include "GraphemeRun.h"
#include "TextSpan.h"
#include "utils/macros.h"

#include <iterator>
#include "debug.h"      // ASSERT

namespace ava::tui::terminal {

Rendition TextSpan::rendition() const
{
  // Only call this if TextSpan::use_default_rendition returned false.
  ASSERT(false);
  AI_NEVER_REACHED;
}

Hyperlink TextSpan::hyperlink() const
{
  // Only call this if TextSpan::is_hyperlink returned true.
  ASSERT(false);
  AI_NEVER_REACHED;
}

// Returns the width in terminal columns of the trimmed text_ string.
Width TextSpan::obtain_natural_width() const
{
  // FIXME: this seems inefficient; can't this be delayed until the GraphemeRun is required anyway?
  GraphemeRun grapheme_run(*this);      // Use GraphemeRun to get the number of terminal columns occupied by each character.

  uint32_t natural_width = 0;
  auto const& characters_metadata = grapheme_run.metadata();
  auto first_non_whitespace_character = characters_metadata.begin();
  while (first_non_whitespace_character != characters_metadata.end() && first_non_whitespace_character->whitespace)
    ++first_non_whitespace_character;
  if (first_non_whitespace_character != characters_metadata.end())
  {
    auto first_trailing_whitespace_character = characters_metadata.end();
    while (std::prev(first_trailing_whitespace_character)->whitespace)
      --first_trailing_whitespace_character;
    for (auto iter = first_non_whitespace_character; iter != first_trailing_whitespace_character; ++iter)
      natural_width += iter->columns;
  }
  return natural_width;
}

//static
std::unique_ptr<TextSpan> TextSpan::create(std::u8string const& text, Rendition rendition, LayoutItem::Properties layout_properties)
{
  return std::unique_ptr<TextSpan>{new StyledTextSpan{{}, layout_properties, text, rendition}};
}

//static
std::unique_ptr<TextSpan> TextSpan::create(std::u8string const& text, Rendition rendition, Hyperlink const& hyperlink, LayoutItem::Properties layout_properties)
{
  return std::unique_ptr<TextSpan>{new HyperlinkedTextSpan{{}, layout_properties, text, rendition, hyperlink}};
}

void TextSpan::write_to(BasicWindow& basic_window, Rendition const& default_rendition) const
{
  DoutEntering(dc::terminal, "TextSpan::write_to(" << basic_window << ", " << default_rendition << ")");

  Rendition const current_rendition = basic_window.current_rendition();
  columns_t const assigned_width = this->assigned_width().columns();
  GraphemeRun const grapheme_run(*this);
  Dout(dc::terminal, "This TextSpan as GraphemeRun: " << grapheme_run);

  // Find the longest whole-cluster prefix that fits in the assigned terminal columns. `length` is a wide-character count for
  // BasicWindow::addstr, while `content_columns` tracks the potentially different cursor advance.
  std::size_t length = 0;
  columns_t content_columns = 0;
  auto const& metadata = grapheme_run.metadata();
  for (auto cluster = metadata.begin(); cluster != metadata.end();)
  {
    auto next_cluster = std::next(cluster);
    columns_t cluster_columns = cluster->columns;
    // Include all combining wide characters in the current grapheme.
    while (next_cluster != metadata.end() && next_cluster->combining)
    {
      cluster_columns += next_cluster->columns;
      ++next_cluster;
    }

    // If this cluster does not fit in the remaining space then exit the loop.
    if (content_columns + cluster_columns > assigned_width)
      break;

    content_columns += cluster_columns;
    length += static_cast<std::size_t>(next_cluster - cluster);
    cluster = next_cluster;
  }

  columns_t spaces = assigned_width - content_columns; // The total number of required filler spaces.
  if (spaces > 0 && horizontal_alignment() != HorizontalAlignment::left)
  {
    columns_t const leading_spaces = horizontal_alignment() == HorizontalAlignment::right ? spaces : spaces / 2;
    basic_window.addspaces(leading_spaces, default_rendition);
    // Adjust `spaces` to become the number of trailing spaces.
    spaces -= leading_spaces;
  }
  // Write the TextSpan content.
  basic_window.addstr(grapheme_run.str().data(), static_cast<int>(length), use_default_rendition() ? default_rendition : rendition());
  // Write the trailing spaces, if any.
  if (spaces > 0)
    basic_window.addspaces(spaces, default_rendition);
  basic_window.restore_rendition(current_rendition);
}

Rendition StyledTextSpan::rendition() const
{
  return rendition_;
}

Hyperlink HyperlinkedTextSpan::hyperlink() const
{
  return hyperlink_;
}

} // namespace ava::tui::terminal
