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

TextSpanView::TextSpanView(TextSpan const& parent) : parent_(&parent)
{
  std::u8string const& parent_text = parent.text();
  characters_.reserve(parent_text.size());

  std::mbstate_t state{};
  std::size_t offset = 0;

  while (offset != parent_text.size())
  {
    wchar_t value;

    char const* first =
        reinterpret_cast<char const*>(parent_text.data() + offset);
    std::size_t const remaining = parent_text.size() - offset;

    std::size_t const size = std::mbrtowc(
        &value, first, remaining, &state);

    if (size == static_cast<std::size_t>(-1))
      throw std::runtime_error("Invalid UTF-8");
    if (size == static_cast<std::size_t>(-2))
      throw std::runtime_error("Truncated UTF-8");
    if (size == 0)
      throw std::runtime_error("Embedded null character");

    int const width = ::wcwidth(value);

    if (width < 0)
      throw std::runtime_error("Non-printable Unicode character");

    characters_.push_back({
        .utf8_begin = offset,
        .utf8_size = size,
        .value = value,
        .cell_width = width,
        .whitespace = std::iswspace(value) != 0
    });

    offset += size;
  }
}

TextSpanView::operator std::u8string_view() const
{
  // Don't call this on an empty TextSpanView.
  ASSERT(!characters_.empty());

  size_t size = 0;
  for (Character const& character : characters_)
    size += character.utf8_size;

  return {&parent_->text()[0] + characters_.front().utf8_begin, size};
}

#ifdef CWDEBUG
void TextSpanView::print_on(std::ostream& os) const
{
  LIBCWD_USING_OSTREAM_PRELUDE;
  os << "{parent:" << print_pointer(parent_) << '}';
}
#endif

} //namespace ava::tui::terminal
