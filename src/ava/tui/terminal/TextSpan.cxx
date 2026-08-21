#include "sys.h"
#include "TextSpan.h"

#include "debug.h"
#ifdef CWDEBUG
#include "utils/debug_ostream_operators.h"
#include "utils/print_pointer.h"
#endif

namespace ava::tui::terminal {
namespace {

// Return whether `codepoint` is one of the regional indicators used in paired flag clusters.
bool is_regional_indicator(char32_t codepoint)
{
  return codepoint >= 0x1F1E6 && codepoint <= 0x1F1FF;
}

// Return whether `codepoint` modifies the skin tone of a preceding emoji base.
bool is_emoji_modifier(char32_t codepoint)
{
  return codepoint >= 0x1F3FB && codepoint <= 0x1F3FF;
}

// Return whether `codepoint` selects a standardized or ideographic variation of its preceding base.
bool is_variation_selector(char32_t codepoint)
{
  return (codepoint >= 0xFE00 && codepoint <= 0xFE0F) || (codepoint >= 0xE0100 && codepoint <= 0xE01EF);
}

// Return whether `codepoint` can begin the compact emoji clusters recognized by the terminal renderer.
bool is_emoji_cluster_start(char32_t codepoint)
{
  return (codepoint >= 0x1F000 && codepoint <= 0x1FAFF) || (codepoint >= 0x2600 && codepoint <= 0x26FF) || codepoint == 0x2705;
}

// Track compact grapheme boundaries while TextSpan UTF-8 is decoded into individual wide characters.
//
// This deliberately matches the narrower-than-UAX-29 behavior in composer_text.cpp: base characters with
// non-spacing marks, regional-indicator pairs, emoji modifiers, and emoji ZWJ sequences are kept together.
class CompactClusterState
{
 private:
  bool has_base_ = false;
  bool emoji_cluster_ = false;
  bool regional_indicator_waiting_ = false;
  bool joined_character_waiting_ = false;

 public:
  // Classify `codepoint` with terminal width `columns`, noting whether another character follows it.
  //
  // Returns true when this character continues the preceding compact grapheme cluster. Orphan marks and
  // incomplete trailing joiners begin their own clusters instead of attaching across a TextSpan boundary.
  bool classify(char32_t codepoint, uint32_t columns, bool has_following_character)
  {
    if (joined_character_waiting_)
    {
      joined_character_waiting_ = false;
      regional_indicator_waiting_ = false;
      has_base_ = true;
      emoji_cluster_ = true;
      return true;
    }

    if (is_regional_indicator(codepoint))
    {
      bool const continuation = regional_indicator_waiting_;
      regional_indicator_waiting_ = !continuation;
      joined_character_waiting_ = false;
      has_base_ = true;
      emoji_cluster_ = false;
      return continuation;
    }
    regional_indicator_waiting_ = false;

    if (is_emoji_modifier(codepoint) && has_base_ && emoji_cluster_)
      return true;

    if (is_variation_selector(codepoint) || (columns == 0 && codepoint != 0x200C && codepoint != 0x200D))
    {
      if (has_base_)
        return true;
      emoji_cluster_ = false;
      return false;
    }

    if (codepoint == 0x200D && has_base_ && emoji_cluster_ && has_following_character)
    {
      joined_character_waiting_ = true;
      return true;
    }

    has_base_ = true;
    emoji_cluster_ = is_emoji_cluster_start(codepoint);
    return false;
  }

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

} // namespace

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
  // FIXME: this seems inefficient; can't this be delayed until the TextSpanView is required anyway?
  TextSpanView view(*this);     // Use TextSpanView to get the number of terminal columns occupied by each character.

  uint32_t natural_width = 0;
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
  CompactClusterState cluster_state;

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
    bool const combining = cluster_state.classify(static_cast<char32_t>(value), static_cast<uint32_t>(width), offset + size < text_span_text.size());

    // Not sure if the rest of the code really relies on this...
    // If this fails then this character is probably one of '\t', '\f', '\n', '\r' or '\v', and those should be handled separately.
    ASSERT(!whitespace || width == 1);

    characters_.push_back({.utf8_begin = offset,
                           .columns = static_cast<uint32_t>(width),
                           .utf8_size = static_cast<uint8_t>(size),        // The maximum value is MB_CUR_MAX = 6 with the used locale.
                           .whitespace = whitespace,
                           .combining = combining},
                          value);

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

} // namespace ava::tui::terminal
