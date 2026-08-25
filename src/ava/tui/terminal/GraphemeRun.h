#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "debug.h"      // ASSERT

namespace ava::tui::terminal {

class TextSpan;
class GraphemeSpan;

using columns_t = uint32_t;

// class GraphemeRun
//
// A contiguous fragment of one TextSpan. It contains whole grapheme clusters
// and retains a pointer to its source TextSpan for rendition and hyperlink data:
//
//       ┊◄──────── run A ────────►┊◄───── run B ────►┊
//       ┌─────────────────────────┬──────────────────┐
//       │ ordinary text           │ bold text        │
//       └─────────────────────────┴──────────────────┘
//           source TextSpan A       source TextSpan B
//
//       compact graphemes within run A:
//
//       ┌───┬───┬───┬─────┬───┬───┬───┬───┬───┬───┬──
//       │ o │ r │ d │  i  │ n │ a │ r │ y │   │ t │ ···
//       └───┴───┴───┴─────┴───┴───┴───┴───┴───┴───┴──
//
// Wrapping may split one TextSpan into more than one GraphemeRun, but never changes
// which TextSpan supplies the rendition.
//
class GraphemeRun
{
 public:
  // Metadata stored in `metadata_` at -say- index N that corresponds to the `wchar_t` stored in str_ at index N.
  struct Metadata
  {
    std::size_t utf8_begin;     // The offset into the parents text to the first byte of the UTF8 encoding of this Character.
    columns_t columns;          // The number of terminal columns that this Character will occupy (unfortunately, this is just an approximation).
    uint8_t utf8_size;          // The total number of UTF8 bytes that are consumed by this Character.
    uint8_t whitespace : 1;     // True if this Character is white-space.
    uint8_t combining : 1;      // True if this Character continues the preceding compact grapheme cluster.

    // Printing this object is better done from the GraphemeRun that contains it.
    AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
  };

 private:
  friend class GraphemeSpan;

  TextSpan const* text_span_;           // The source.
  std::wstring str_;                    // The wide characters of a run of grapheme clusters.
  std::vector<Metadata> metadata_;      // The meta data of the characters in str_.

 private:
  void copy_prefix(GraphemeRun const& source, std::size_t size)
  {
    str_.reserve(size);
    str_.assign(source.str_.begin(), source.str_.begin() + size);
    metadata_.reserve(size);
    metadata_.assign(source.metadata_.begin(), source.metadata_.begin() + size);
  }

  void remove_prefix(std::size_t size)
  {
    // Paranoia check.
    ASSERT(str_.size() == metadata_.size() && size <= str_.size());
    str_.erase(str_.begin(), str_.begin() + static_cast<std::ptrdiff_t>(size));
    metadata_.erase(metadata_.begin(), metadata_.begin() + static_cast<std::ptrdiff_t>(size));
  }

  void push_back(Metadata character_meta, wchar_t character_wide)
  {
    str_ += character_wide;
    metadata_.push_back(character_meta);
  }

  size_t utf8_size() const
  {
    size_t size = 0;
    for (Metadata const& character_meta : metadata_)
      size += character_meta.utf8_size;
    return size;
  }

 public:
  // Accessors.
  std::wstring const& str() const { return str_; }
  std::vector<Metadata> const& metadata() const { return metadata_; }

  bool empty() const { return str_.empty(); }

  // Construct an empty GraphemeRun.
  GraphemeRun() : text_span_(nullptr) { }

  // Construct a GraphemeRun that covers the whole parent.
  GraphemeRun(TextSpan const& parent);

  // Accessors.
  TextSpan const* text_span() const { return text_span_; }

#ifdef CWDEBUG
  // Get a view into the TextSpan parent of just the utf8 characters.
  std::u8string_view get_u8string_view() const;

  void print_on(std::ostream& os) const;
#endif

  // We have a custom printer.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

} // namespace ava::tui::terminal
