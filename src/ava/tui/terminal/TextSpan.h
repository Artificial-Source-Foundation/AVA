#pragma once

#include "Hyperlink.h"
#include "LayoutItem.h"
#include "Rendition.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tui::terminal {

class TextSpan : public LayoutItem
{
 private:
  std::u8string text_;          // Pure UTF8 encoded text for display with the same rendition (no markdown).

 protected:
  // Constructor; used by create and the constructor of StyledTextSpan.
  TextSpan(LayoutItem&& layout_item, std::u8string const& text) : LayoutItem(std::move(layout_item)), text_(text) { }

 public:
  // Create a TextSpan without rendition info. Use this for example when the rendition will be determined by the window (which must be enforced elsewhere).
  static std::unique_ptr<TextSpan> create(std::u8string const& text, LayoutItem::Properties layout_properties = {.minimum_width = greedy})
  {
    return std::unique_ptr<TextSpan>{new TextSpan{LayoutItem{layout_properties}, text}};
  }

  // Create a TextSpan with its own rendition info.
  static std::unique_ptr<TextSpan> create(std::u8string const& text, Rendition rendition, LayoutItem::Properties layout_properties = {.minimum_width = greedy});

  // Create a TextSpan with rendition info and hyperlink.
  static std::unique_ptr<TextSpan> create(std::u8string const& text, Rendition rendition, Hyperlink const& hyperlink,
                                          LayoutItem::Properties layout_properties = {.minimum_width = greedy});

  // Accessor.
  std::u8string const& text() const { return text_; }

  // Convenience accessor; returns true for a TextSpan, false if the most-derived class is a StyledTextSpan.
  virtual bool use_default_rendition() const { return true; }

  // Only call this if use_default_rendition() returns false; otherwise use the default rendition of the containing window.
  virtual Rendition rendition() const;

  // Convenience accessor; returns true if the most-derived class is a HyperlinkedTextSpan. False otherwise.
  virtual bool is_hyperlink() const { return false; }

  // Only call this if is_hyperlink() returns true.
  virtual Hyperlink hyperlink() const;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  int do_natural_width() const override;
};

class StyledTextSpan : public TextSpan
{
 private:
  Rendition rendition_;

 public:
  StyledTextSpan(LayoutItem&& layout_item, std::u8string const& text, Rendition rendition) : TextSpan(std::move(layout_item), text), rendition_(rendition) { }

  bool use_default_rendition() const override { return false; }
  Rendition rendition() const override;

  AVA_DEBUG_PRINT_MEMBERS_ON_BASE(TextSpan)
};

class HyperlinkedTextSpan : public StyledTextSpan
{
 private:
  Hyperlink hyperlink_;

 public:
  HyperlinkedTextSpan(LayoutItem&& layout_item, std::u8string const& text, Rendition rendition, Hyperlink const& hyperlink)
      : StyledTextSpan(std::move(layout_item), text, rendition), hyperlink_(hyperlink)
  {
  }

  bool is_hyperlink() const override { return true; }
  Hyperlink hyperlink() const override;

  AVA_DEBUG_PRINT_MEMBERS_ON_BASE(StyledTextSpan)
};

class TextSpanView
{
  // Meta data of the Character, stored in `characters_meta_` at -say- index N
  // that corresponds to the `wchar_t` stored in wide_characters_ at index N.
  struct CharacterMeta
  {
    std::size_t utf8_begin;     // The offset into the parents text to the first byte of the UTF8 encoding of this Character.
    std::size_t utf8_size;      // The total number of UTF8 bytes that are consumed by this Character.
    int cell_width;             // The number of terminal cells that this Character will occupy (unfortunately, this is just an approximation).
    bool whitespace;            // True if this Character is white-space.

    // Printing this object is better done from the TextSpanView that contains it.
    AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
  };

 private:
  friend class TextRow;

  TextSpan const* text_span_;                   // The TextSpan (base) class that this is a view into.
  std::wstring wide_characters_;                // The wide characters that make up this view.
  std::vector<CharacterMeta> characters_meta_;  // The meta data of the characters in wide_characters_.

 public:
  // Construct an empty TextSpanView.
  TextSpanView() : text_span_(nullptr) { }

  // Construct a TextSpanView that covers the whole parent.
  TextSpanView(TextSpan const& parent);

  // Accessors.
  TextSpan const* text_span() const { return text_span_; }

  // The wide characters that make up this view.
  std::wstring const& characters() const { return wide_characters_; }

  // The meta data of the characters.
  std::vector<CharacterMeta> const& characters_meta() const { return characters_meta_; }

  // Convenience accessor; returns true if this view contains no Characters.
  bool empty() const { return wide_characters_.empty(); }

  operator std::u8string_view() const;

#ifdef CWDEBUG
  void print_on(std::ostream& os) const;
#endif

  // We have a custom printer.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

} // namespace ava::tui::terminal
