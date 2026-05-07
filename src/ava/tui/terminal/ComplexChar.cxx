#include "ComplexChar.h"

#include "private_convert.h"

#include <cwchar>

#include "debug.h"

namespace terminal {

ComplexChar::ComplexChar(GraphemeCluster const& cell_character, Rendition rendition) : rendition_(rendition)
{
  // Now that curses.h is loaded - check that we used the right value for CCHARW_MAX.
  static_assert(GraphemeCluster::capacity == CCHARW_MAX, "GraphemeCluster::capacity must have the same value as CCHARW_MAX defined in curses.h!");

  // Copy the provided grapheme cluster to cell_character_.
  cell_character_ = cell_character;
}

} // namespace terminal
