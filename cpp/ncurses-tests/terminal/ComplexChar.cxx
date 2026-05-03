#include "ComplexChar.h"
#include <cwchar>
#include "debug.h"

#include "private_convert.h"

namespace terminal {

ComplexChar::ComplexChar(wchar_t const* cell_character, Rendition rendition) : rendition_(rendition)
{
  // Now that curses.h is loaded - check that we used the right value for CCHARW_MAX.
  static_assert(sizeof(cell_character_) / sizeof(cell_character_[0]) == CCHARW_MAX,
      "ComplexChar::CCHARW_MAX must have the same value as what is defined in curses.h!");

  // Copy the cell_character to cell_character_ and zero terminate it.
  std::wcsncpy(cell_character_, cell_character, CCHARW_MAX);
  // From https://en.cppreference.com/cpp/string/wide/wcsncpy:
  // If count is reached before the entire string src was copied, the resulting wide character array is not null-terminated.
  //
  // This should never happen because cell_character should be at most four characters, but better safe than sorry.
  cell_character_[CCHARW_MAX - 1] = L'\0';

  // Assert that the above claim that cell_character is at most CCHARW_MAX-1 wide characters, is true.
  // Don't pass a variable width character of more than four wchar_t.
  ASSERT(std::wmemchr(cell_character, L'\0', CCHARW_MAX) != nullptr);
}

} // namespace terminal
