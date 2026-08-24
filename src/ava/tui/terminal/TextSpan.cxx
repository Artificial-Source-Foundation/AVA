#include "sys.h"
#include "TextSpan.h"
#include "GraphemeRun.h"
#include "BasicWindow.h"
#include "utils/macros.h"
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
  Rendition const current_rendition = basic_window.current_rendition();
  std::size_t const assigned_width = this->assigned_width().value();
  GraphemeRun const grapheme_run(*this);
  std::size_t length = std::min(grapheme_run.str().length(), assigned_width);
  std::size_t spaces = assigned_width - length; // The total number of required filler spaces.
  if (spaces > 0 && horizontal_alignment() != HorizontalAlignment::left)
  {
    std::size_t const leading_spaces = horizontal_alignment() == HorizontalAlignment::right ? spaces : spaces / 2;
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
