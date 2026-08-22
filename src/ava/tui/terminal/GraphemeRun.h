#pragma once

#include "WideCharacters.h"

namespace ava::tui::terminal {

class TextSpan;
class TextRow;

class GraphemeRun
{
 private:
  friend class TextRow;

  TextSpan const* text_span_;                   // The source.
  WideCharacters wide_characters_;              // The wide characters and meta data that make up this grapheme run.

 public:
  // Construct an empty GraphemeRun.
  GraphemeRun() : text_span_(nullptr), wide_characters_(0) { }

  // Construct a GraphemeRun that covers the whole parent.
  GraphemeRun(TextSpan const& parent);

  // Accessors.
  TextSpan const* text_span() const { return text_span_; }

  WideCharacters& wide_characters() { return wide_characters_; }
  WideCharacters const& wide_characters() const { return wide_characters_; }

  // Convenience accessor; returns true if this view contains no wide characters.
  bool empty() const { return wide_characters_.empty(); }

#ifdef CWDEBUG
  // Get a view into the TextSpan parent of just the utf8 characters.
  std::u8string_view get_u8string_view() const;

  void print_on(std::ostream& os) const;
#endif

  // We have a custom printer.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

} // namespace ava::tui::terminal
