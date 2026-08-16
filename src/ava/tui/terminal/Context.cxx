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
    // use_default_colors returns ERR when the terminal does not support default colors; only initialize a Context on
    // a terminal with the default-color capability.
    ASSERT(status == OK);
    Color foreground_color{0xffffff};
    Color background_color{};
    default_rendition_ = Rendition{create_color_pair(foreground_color, background_color)};
  }

  cbreak();
  noecho();
  nl();                 // Always translate the Enter key to a linefeed.
  meta(::stdscr, TRUE); // Always return 8-bit character codes.
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
  // If this fires, the TUI was started on a terminal without direct color; run it only on a terminal reporting
  // COLORS == 16777216, or implement the non-direct-color path first.
  ASSERT(COLORS == 16777216);

  if (COLORS == 16777216)
  {
    // On a direct color indexing terminal, the color itself is the color index.
    // Therefore we do not need to call init_color or init_extended_color.

    // The next color pair index.
    int color_pair_index = color_pairs_.size() + 1;
    // Initialize the new color pair.
    int const status = ::init_extended_pair(color_pair_index, foreground.as_int(), background.as_int());
    // init_extended_pair returns ERR when the pair index exceeds COLOR_PAIRS or a color index is out of range; keep
    // the number of created pairs within the terminal limit and pass valid Color values.
    ASSERT(status == OK);
    // Create the new ColorPair from the new color pair index.
    color_pairs_.push_back(color_pair_index);
  }

  // This function should push the new ColorPair to the end of colors_pairs_.
  return color_pairs_.back();
}

} // namespace ava::tui::terminal
