#include "sys.h"
#include "TextSpan.h"
#include "debug.h"
#ifdef CWDEBUG
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

//static
std::unique_ptr<TextSpan> TextSpan::create(std::u8string const& text, Rendition rendition)
{
  return std::unique_ptr<TextSpan>{new StyledTextSpan{text, rendition}};
}

//static
std::unique_ptr<TextSpan> TextSpan::create(std::u8string const& text, Rendition rendition, Hyperlink const& hyperlink)
{
  return std::unique_ptr<TextSpan>{new HyperlinkedTextSpan{text, rendition, hyperlink}};
}

Rendition StyledTextSpan::rendition() const
{
  return rendition_;
}

Hyperlink HyperlinkedTextSpan::hyperlink() const
{
  return hyperlink_;
}

TextSpanView::TextSpanView(TextSpan const& text_span) : text_span_(&text_span)
{
  std::u8string const& text_span_text = text_span.text();
  characters_meta_.reserve(text_span_text.size());

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

    characters_meta_.push_back({
        .utf8_begin = offset,
        .utf8_size = size,
        .cell_width = width,
        .whitespace = whitespace
    });
    wide_characters_ += value;

    offset += size;
  }
}

TextSpanView::operator std::u8string_view() const
{
  // Don't call this on an empty TextSpanView.
  ASSERT(!characters_meta_.empty());

  size_t size = 0;
  for (CharacterMeta const& character_meta : characters_meta_)
    size += character_meta.utf8_size;

  return {&text_span_->text()[0] + characters_meta_.front().utf8_begin, size};
}

#ifdef CWDEBUG
void TextSpanView::print_on(std::ostream& os) const
{
  LIBCWD_USING_OSTREAM_PRELUDE;
  os << "{text_span:" << print_pointer(text_span_) << '}';
}
#endif

} //namespace ava::tui::terminal
