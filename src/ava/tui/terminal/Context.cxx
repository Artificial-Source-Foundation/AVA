#include "sys.h"
#include "Context.h"

#include <clocale>
#include <cstdlib>

// This header must be included last.
#include "private_convert.h"

namespace ava::tui::terminal {

Context::Context() : default_rendition_(ColorPair{0})
{
  setlocale(LC_ALL, "");

  // From https://invisible-island.net/ncurses/man/curs_util.3x.html
  // use_env   use_tioctl   Summary
  // TRUE      TRUE         ncurses updates LINES and COLUMNS based on operating system calls.
  ::use_env(TRUE);
  ::use_tioctl(TRUE);

  initscr();

  stdscr_.init_as_stdscr();

  wchar_t fill[] = L" ";

  if (has_colors())
  {
    start_color();
    // Enable ncurses' default-color extension: after this succeeds, color
    // number -1 in init_pair/init_extended_pair means the terminal's default
    // foreground or background color instead of an RGB/direct-color index.
    int const status = ::use_default_colors();
    ASSERT(status == OK);
    Color foreground_color{0xffffff};
    Color background_color{};
    default_rendition_ = Rendition{create_color_pair(foreground_color, background_color)};
  }

  cbreak();
  noecho();
  nl();                 // Always translate the Enter key to a linefeed.
  meta(::stdscr, TRUE); // Always return 8-bit character codes.

  // Refresh stdscr once to consume its initial all-touched state, and to clear
  // whatever the previous program left on the physical terminal.
  // A still-touched stdscr would stage blanks over cells that other windows
  // already published to the virtual screen — explicitly, or implicitly from
  // the wrefresh(stdscr) that get_wch performs before blocking for input.
  // After this, application windows own all staging; never write through stdscr.
  stdscr_.refresh();
}

Context::~Context()
{
  endwin();
}

uint32_t Context::rows() const
{
  return LINES;
}

uint32_t Context::cols() const
{
  return COLS;
}

int Context::get_wch()
{
  wint_t wch;
  ::get_wch(&wch);
  return wch;
}

ColorPair Context::create_color_pair(Color foreground, Color background)
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
    int const status = ::init_extended_pair(color_pair_index, foreground.as_int(), background.as_int());
    ASSERT(status == OK);
    // Create the new ColorPair from the new color pair index.
    color_pairs_.push_back(color_pair_index);
  }

  // This function should push the new ColorPair to the end of colors_pairs_.
  return color_pairs_.back();
}

} // namespace ava::tui::terminal
