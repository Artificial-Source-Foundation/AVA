#include "Session.h"
#include <cstdlib>
#include <clocale>

// This header must be included last.
#include "private_convert.h"

namespace terminal {

Session::Session() : default_rendition_(ColorPair{-1})
{
  setlocale(LC_ALL, "");
  initscr();

  wchar_t fill[] = L" ";

  if (has_colors())
  {
    start_color();
    Color foreground_color{0xffffff};
    Color background_color{0x2a2222};
    default_rendition_ = Rendition{create_color_pair(foreground_color, background_color)};
  }

  cbreak();
  noecho();
}

Session::~Session()
{
  endwin();
}

int Session::rows() const
{
  return LINES;
}

void Session::refresh()
{
  ::refresh();
}

int Session::get_wch()
{
  wint_t wch;
  ::get_wch(&wch);
  return wch;
}

ColorPair Session::create_color_pair(Color foreground, Color background)
{
  // Support for non-direct terminals has not be implemented yet.
  ASSERT(COLORS == 16777216);

  if (COLORS == 16777216)
  {
    // On a direct color indexing terminal, the color itself is the color index.
    // Therefore we do not need to call init_color or init_extended_color.

    // The next color pair index.
    int color_pair_index = color_pairs_.size() + 1;
    // Initialize the new color pair.
    ::init_extended_pair(color_pair_index, foreground.as_int(), background.as_int());
    // Create the new ColorPair from the new color pair index.
    color_pairs_.push_back(color_pair_index);
  }

  // This function should push the new ColorPair to the end of colors_pairs_.
  return color_pairs_.back();
}

} // namespace terminal
