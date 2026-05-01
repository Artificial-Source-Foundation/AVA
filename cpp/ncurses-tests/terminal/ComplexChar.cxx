#include "ComplexChar.h"
#include <cwchar>

#define NCURSES_NOMACROS
#include <curses.h>

//FIXME: use libcwd
#include <cassert>
#define ASSERT assert

namespace terminal {

ComplexChar::ComplexChar(wchar_t const* variable_width_character, Attributes attributes, int color_pair) : attributes_(attributes), color_pair_(color_pair)
{
  std::wcsncpy(variable_width_character_, variable_width_character, sizeof(variable_width_character_));
  // From https://en.cppreference.com/cpp/string/wide/wcsncpy:
  // If count is reached before the entire string src was copied, the resulting wide character array is not null-terminated.
  //
  // This should never happen because variable_width_character should be at most four characters, but better safe than sorry.
  variable_width_character_[4] = L'\0';
  // Don't pass a variable width character of more than four wchar_t.
  ASSERT(std::wmemchr(variable_width_character, L'\0', sizeof(variable_width_character_)) != nullptr);
}

ComplexChar::ComplexChar(wchar_t const* variable_width_character, Attributes attributes) : ComplexChar(variable_width_character, attributes, 0) { }

ComplexChar::ComplexChar(wchar_t const* variable_width_character, int color_pair) : ComplexChar(variable_width_character, {}, color_pair) { }

ComplexChar::ComplexChar(wchar_t const* variable_width_character) : ComplexChar(variable_width_character, {}, 0) { }

} // namespace terminal
