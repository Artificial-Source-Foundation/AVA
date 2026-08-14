#pragma once

#include "Hyperlink.h"
#include "Rendition.h"
#include <string>
#include <memory>
#include <string_view>
#include <vector>

namespace ava::tui::terminal {

class TextSpan
{
 private:
  std::u8string text_;          // Pure UTF8 encoded text for display with the same rendition (no markdown).

 protected:
  // Constructor; used by create and the constructor of StyledTextSpan.
  TextSpan(std::u8string const& text) : text_(text) { }

 public:
  virtual ~TextSpan() = default;

 public:
  // Create a TextSpan without rendition info. Use this for example when the rendition will be determined by the window (which must be enforced elsewhere).
  static std::unique_ptr<TextSpan> create(std::u8string const& text) { return std::unique_ptr<TextSpan>{new TextSpan{text}}; }

  // Create a TextSpan with its own rendition info.
  static std::unique_ptr<TextSpan> create(std::u8string const& text, Rendition rendition);

  // Create a TextSpan with rendition info and hyperlink.
  static std::unique_ptr<TextSpan> create(std::u8string const& text, Rendition rendition, Hyperlink const& hyperlink);

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
};

class StyledTextSpan : public TextSpan
{
 private:
  Rendition rendition_;

 public:
  StyledTextSpan(std::u8string const& text, Rendition rendition) : TextSpan(text), rendition_(rendition) { }

  bool use_default_rendition() const override { return false; }
  Rendition rendition() const override;

  AVA_DEBUG_PRINT_MEMBERS_ON_BASE(TextSpan)
};

class HyperlinkedTextSpan : public StyledTextSpan
{
 private:
  Hyperlink hyperlink_;

 public:
  HyperlinkedTextSpan(std::u8string const& text, Rendition rendition, Hyperlink const& hyperlink) :
    StyledTextSpan(text, rendition), hyperlink_(hyperlink) { }

  bool is_hyperlink() const override { return true; }
  Hyperlink hyperlink() const override;

  AVA_DEBUG_PRINT_MEMBERS_ON_BASE(StyledTextSpan)
};

struct Character
{
  std::size_t utf8_begin;                       // The offset into the parents text to the first byte of this Character.
  std::size_t utf8_size;                        // The total number of bytes in the parents text that are consumed by this Character.
  wchar_t value;                                // The stretch of UTF8 chars converted to a wide character.
  int cell_width;                               // The number of terminal cells that this character will occupy (unfortunately, this is just an approximation).
  bool whitespace;                              // True if this character is white-space.

  // It is current not possible to print a wchar_t. Printing this object is better done from the TextSpanView that contains it.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

class TextSpanView
{
 private:
  friend class TextRow;

  TextSpan const* parent_;                      // The TextSpan (base) class that this is a view into.
  std::vector<Character> characters_;           // The Characters that make up this view.

 public:
  // Construct an empty TextSpanView.
  TextSpanView() : parent_(nullptr) { }

  // Construct a TextSpanView that covers the whole parent.
  TextSpanView(TextSpan const& parent);

  // Accessors.
  TextSpan const* parent() const { return parent_; }
  operator std::u8string_view() const;

#ifdef CWDEBUG
  void print_on(std::ostream& os) const;
#endif

  // We have a custom printer.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

} // namespace ava::tui::terminal
