#pragma once

#include <array>
#include <cstddef>
#include <cwchar>

namespace terminal {

// class GraphemeCluster
//
// Stores the wide characters that ncursesw associates with one terminal-cell complex character.
// The first element must be the spacing/base character, the remaining four elements may be non-spacing
// combining characters, if any.
//
// The storage has exactly ncursesw's CCHARW_MAX capacity: if fewer than CCHARW_MAX elements are
// used then the next element is L'\0', while a full cluster is not terminated in-place.
//
// Code that needs to pass the contents to an API requiring a null-terminated wchar_t string,
// such as setcchar, must copy the data to a CCHARW_MAX + 1 buffer and append L'\0' there.
//
class GraphemeCluster
{
 public:
  static constexpr wchar_t const* space_ = L" ";
  static constexpr std::size_t capacity = 5;  // The same value that ncursesw uses for CCHARW_MAX.
  using Storage = std::array<wchar_t, capacity>;

 private:
  Storage storage_{};

 public:
  // Copy at most `capacity` wide characters from a null-terminated source string.
  // If the source terminates before the cluster is full, the terminator is copied and the
  // remaining storage stays zero-filled.  If the source has capacity or more
  // non-null elements, the cluster is full and contains no in-place terminator.
  explicit GraphemeCluster(wchar_t const* characters)
  {
    for (std::size_t i = 0; i < storage_.size(); ++i)
    {
      storage_[i] = characters[i];
      if (characters[i] == L'\0')
        break;
    }
  }

  // Construct a blank cluster, matching ncurses' conventional space character for a cell
  // containing only the background rendition.
  GraphemeCluster() : GraphemeCluster(space_) { }

  // Wrap already-normalized cluster storage.
  //
  // The caller is responsible for preserving the class invariant:
  // either the array is full, or the first unused slot contains L'\0'.
  explicit GraphemeCluster(Storage storage) : storage_(storage) { }

  // Assignment operator.
  GraphemeCluster& operator=(GraphemeCluster const& orig)
  {
    std::wcsncpy(storage_.data(), orig.data(), storage_.size());
    // From https://en.cppreference.com/cpp/string/wide/wcsncpy:
    // If count is reached before the entire string src was copied, the resulting wide character array is not null-terminated.
    return *this;
  }

  // Accessors.
  Storage const& storage() const { return storage_; }
  Storage& storage() { return storage_; }

  // Return number of valid wide characters stored.
  std::size_t length() const
  {
    wchar_t const* terminator = static_cast<wchar_t const*>(std::wmemchr(storage_.data(), L'\0', storage_.size()));
    return terminator == nullptr ? storage_.size() : static_cast<std::size_t>(terminator - storage_.data());
  }

  // Access the fixed-size storage. The number of valid characters are returned by length().
  wchar_t const* data() const { return storage_.data(); }
  wchar_t* data() { return storage_.data(); }

  // Return true iff data() is guaranteed to be zero terminated.
  bool is_zero_terminated() const { return length() < storage_.size(); }
};

} // namespace terminal
