#pragma once

#include "Hyperlink.h"
#include "LayoutItem.h"
#include "Rendition.h"
#include "utils/Badge.h"

#include <memory>
#include <string_view>

namespace ava::tui::terminal {

class BasicWindow;
class Rendition;

// class TextSpan
//
// Represents the UTF8 characters that make up a span of characters to be displayed with the same rendition.
//
// For example:
//
//   u8"Exit the app"
//
// If `use_default_rendition` returns `true` (the TextSpan was created by a call to `TextSpan::create` without passing a `rendition`)
// then the rendition of this text must come from elsewhere. Otherwise (if a `rendition` was passed to `TextSpan::create`), the rendition
// to be used can be obtained by a call to `rendition`.
//
// Iff `is_hyperlink` returns `true` (the TextSpan was created by a call to `TextSpan::create` while passing a `Hyperlink`) then
// this `TextSpan` is associated with the `Hyperlink` returned by the member function `hyperlink`.
//
class TextSpan : public LayoutItem
{
 private:
  std::u8string text_;          // Pure UTF8 encoded text for display with the same rendition (no markdown).

 protected:
  // Constructor; used by create and the constructor of StyledTextSpan.
  TextSpan(LayoutItem::Properties const& layout_properties, std::u8string const& text) : LayoutItem(layout_properties), text_(text)
  {
    initialize_cached_natural_width(obtain_natural_width());
  }

 public:
  // Create a TextSpan without rendition info. Use this for example when the rendition will be determined by the window (which must be enforced elsewhere).
  static std::unique_ptr<TextSpan> create(std::u8string const& text, LayoutItem::Properties layout_properties = {.minimum_width = greedy})
  {
    return std::unique_ptr<TextSpan>{new TextSpan{layout_properties, text}};
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

  // Convert this TextSpan to one clipped GraphemeSpan at its assigned terminal-column width.
  //
  // Content is clipped without splitting a compact grapheme cluster. Rendering later fills any unused
  // columns according to the configured horizontal alignment.
  GraphemeBlock create_grapheme_block() const override;

  AVA_DEBUG_PRINT_MEMBERS_ON_BASE(LayoutItem)

 private:
  Width obtain_natural_width() const;
};

class StyledTextSpan : public TextSpan
{
 private:
  Rendition rendition_;

 public:
  StyledTextSpan(utils::Badge<TextSpan>, LayoutItem::Properties const& layout_properties, std::u8string const& text, Rendition rendition)
      : TextSpan(layout_properties, text), rendition_(rendition)
  {
  }

  bool use_default_rendition() const override { return false; }
  Rendition rendition() const override;

  AVA_DEBUG_PRINT_MEMBERS_ON_BASE(TextSpan)
};

class HyperlinkedTextSpan : public StyledTextSpan
{
 private:
  Hyperlink hyperlink_;

 public:
  HyperlinkedTextSpan(utils::Badge<TextSpan> badge, LayoutItem::Properties const& layout_properties, std::u8string const& text, Rendition rendition,
                      Hyperlink const& hyperlink)
      : StyledTextSpan(badge, layout_properties, text, rendition), hyperlink_(hyperlink)
  {
  }

  bool is_hyperlink() const override { return true; }
  Hyperlink hyperlink() const override;

  AVA_DEBUG_PRINT_MEMBERS_ON_BASE(StyledTextSpan)
};

} // namespace ava::tui::terminal
