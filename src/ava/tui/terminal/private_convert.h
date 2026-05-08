// private_convert.h
//
// This header may *only* be included from terminal/*.cxx files.

#include "ComplexChar.h"

#define NCURSES_NOMACROS
#include <curses.h>

#undef getbegyx
#undef getmaxyx
#undef getparyx
#undef getsyx
#undef getyx
#undef setsyx
#undef L_ctermid
#undef L_cuserid
#undef L_tmpnam
#undef offsetof
#undef P_tmpdir
#undef stderr
#undef stdin
#undef stdout

// Sanity check.
#if NCURSES_WIDECHAR != 1
#error "NCURSES_WIDECHAR is expected to be defined to 1."
#endif

using Attributes = terminal::Attributes;
using Attribute = terminal::Attribute;
using ComplexChar = terminal::ComplexChar;

attr_t convert_to_attr(Attributes attributes);
Attributes convert_to_Attributes(attr_t attributes);
cchar_t convert_to_cchar(ComplexChar const& complex_char);
ComplexChar convert_to_ComplexChar(cchar_t const& cchar);
