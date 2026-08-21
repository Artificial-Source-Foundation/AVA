#pragma once

#include <string>
#include <vector>
#include "debug.h"      // ASSERT

namespace ava::tui::terminal {

class Characters
{
 public:
  // Meta data of the Character, stored in `characters_meta_` at -say- index N
  // that corresponds to the `wchar_t` stored in wide_characters_ at index N.
  struct CharacterMeta
  {
    std::size_t utf8_begin;     // The offset into the parents text to the first byte of the UTF8 encoding of this Character.
    uint32_t columns;           // The number of terminal columns that this Character will occupy (unfortunately, this is just an approximation).
    uint8_t utf8_size;          // The total number of UTF8 bytes that are consumed by this Character.
    uint8_t whitespace : 1;       // True if this Character is white-space.
    uint8_t combining : 1;        // True if this Character continues the preceding compact grapheme cluster.

    // Printing this object is better done from the TextSpanView that contains it.
    AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
  };

 private:
  std::wstring wide_;                // The wide characters that make up this view.
  std::vector<CharacterMeta> meta_;  // The meta data of the characters in wide_characters_.

 public:
  // Construct Characters reserving `size_hint` wide characters for wide_.
  Characters(std::size_t size_hint) { wide_.reserve(size_hint); }

  void copy_prefix(Characters const& source, std::size_t size)
  {
    wide_.reserve(size);
    wide_.assign(source.wide_.begin(), source.wide_.begin() + size);
    meta_.reserve(size);
    meta_.assign(source.meta_.begin(), source.meta_.begin() + size);
  }

  void remove_prefix(std::size_t size)
  {
    // Paranoia check.
    ASSERT(wide_.size() == meta_.size() && size <= wide_.size());
    wide_.erase(wide_.begin(), wide_.begin() + static_cast<std::ptrdiff_t>(size));
    meta_.erase(meta_.begin(), meta_.begin() + static_cast<std::ptrdiff_t>(size));
  }

  void push_back(CharacterMeta character_meta, wchar_t character_wide)
  {
    wide_ += character_wide;
    meta_.push_back(character_meta);
  }

  size_t utf8_size() const
  {
    size_t size = 0;
    for (CharacterMeta const& character_meta : meta_)
      size += character_meta.utf8_size;
    return size;
  }

  // Accessors.
  std::wstring const& wide() const { return wide_; }
  std::vector<CharacterMeta> const& meta() const { return meta_; }

  bool empty() const { return wide_.empty(); }

  // Printing a std::wstring is too much work.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

} // namespace ava::tui::terminal
