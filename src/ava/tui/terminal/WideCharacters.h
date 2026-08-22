#pragma once

#include <string>
#include <vector>
#include "debug.h"      // ASSERT

namespace ava::tui::terminal {

class WideCharacters
{
 public:
  // Metadata stored in `metadata_` at -say- index N that corresponds to the `wchar_t` stored in str_ at index N.
  struct Metadata
  {
    std::size_t utf8_begin;     // The offset into the parents text to the first byte of the UTF8 encoding of this Character.
    uint32_t columns;           // The number of terminal columns that this Character will occupy (unfortunately, this is just an approximation).
    uint8_t utf8_size;          // The total number of UTF8 bytes that are consumed by this Character.
    uint8_t whitespace : 1;     // True if this Character is white-space.
    uint8_t combining : 1;      // True if this Character continues the preceding compact grapheme cluster.

    // Printing this object is better done from the GraphemeRun that contains it.
    AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
  };

 private:
  std::wstring str_;                   // The wide characters of a run of grapheme clusters.
  std::vector<Metadata> metadata_;      // The meta data of the characters in str_.

 public:
  // Construct WideCharacters reserving `size_hint` wide characters for str_.
  WideCharacters(std::size_t size_hint) { str_.reserve(size_hint); }

  void copy_prefix(WideCharacters const& source, std::size_t size)
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

  // Accessors.
  std::wstring const& str() const { return str_; }
  std::vector<Metadata> const& metadata() const { return metadata_; }

  bool empty() const { return str_.empty(); }

  // Printing a std::wstring is too much work.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

} // namespace ava::tui::terminal
