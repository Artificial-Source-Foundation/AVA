#include "sys.h"
#include "TextSpan.h"
#include "debug.h"
#ifdef CWDEBUG
#include "utils/debug_ostream_operators.h"
#include "utils/print_pointer.h"
#endif

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
int TextSpan::do_natural_width() const
{
  //FIXME: this seems inefficient; can't this be delayed until the TextSpanView is required anyway?
  TextSpanView view(*this);     // Use TextSpanView to get the cell width of each character.

  int natural_width = 0;
  auto const& characters_meta = view.characters().meta();
  auto first_non_whitespace_character = characters_meta.begin();
  while (first_non_whitespace_character != characters_meta.end() && first_non_whitespace_character->whitespace)
    ++first_non_whitespace_character;
  if (first_non_whitespace_character != characters_meta.end())
  {
    auto first_trailing_whitespace_character = characters_meta.end();
    while (std::prev(first_trailing_whitespace_character)->whitespace)
      --first_trailing_whitespace_character;
    for (auto iter = first_non_whitespace_character; iter != first_trailing_whitespace_character; ++iter)
      natural_width += iter->cell_width;
  }
  return natural_width;
}

//static
std::unique_ptr<TextSpan> TextSpan::create(std::u8string const& text, Rendition rendition, LayoutItem::Properties layout_properties)
{
  return std::unique_ptr<TextSpan>{new StyledTextSpan{layout_properties, text, rendition}};
}

//static
std::unique_ptr<TextSpan> TextSpan::create(std::u8string const& text, Rendition rendition, Hyperlink const& hyperlink, LayoutItem::Properties layout_properties)
{
  return std::unique_ptr<TextSpan>{new HyperlinkedTextSpan{layout_properties, text, rendition, hyperlink}};
}

Rendition StyledTextSpan::rendition() const
{
  return rendition_;
}

Hyperlink HyperlinkedTextSpan::hyperlink() const
{
  return hyperlink_;
}

TextSpanView::TextSpanView(TextSpan const& text_span) : text_span_(&text_span), characters_(text_span.text().size())
{
  std::u8string const& text_span_text = text_span.text();

  std::mbstate_t state{};
  std::size_t offset = 0;

  while (offset != text_span_text.size())
  {
    char const* first = reinterpret_cast<char const*>(text_span_text.data() + offset);
    std::size_t const remaining = text_span_text.size() - offset;

    wchar_t value;
    std::size_t const size = std::mbrtowc(&value, first, remaining, &state);
    if (size == static_cast<std::size_t>(-1))
      throw std::runtime_error("Invalid UTF-8");
    if (size == static_cast<std::size_t>(-2))
      throw std::runtime_error("Truncated UTF-8");
    if (size == 0)
      throw std::runtime_error("Embedded null character");

    int const width = ::wcwidth(value);
    if (width < 0)
      throw std::runtime_error("Non-printable Unicode character");
    bool const whitespace = std::iswspace(value) != 0;

    // Not sure if the rest of the code really relies on this...
    // If this fails then this character is probably one of '\t', '\f', '\n', '\r' or '\v', and those should be handled separately.
    ASSERT(!whitespace || width == 1);

    characters_.push_back({
        .utf8_begin = offset,
        .utf8_size = size,
        .cell_width = width,
        .whitespace = whitespace
      }, value);

    offset += size;
  }
}

#ifdef CWDEBUG
std::u8string_view TextSpanView::get_u8string_view() const
{
  // Don't call this on an empty TextSpanView.
  ASSERT(!characters_.empty());
  return {&text_span_->text()[0] + characters_.meta().front().utf8_begin, characters_.utf8_size()};
}

void TextSpanView::print_on(std::ostream& os) const
{
  LIBCWD_USING_OSTREAM_PRELUDE;
  os << "{text_span:" << print_pointer(text_span_) << ", characters_:" << get_u8string_view() << '}';
}

#endif

} //namespace ava::tui::terminal
