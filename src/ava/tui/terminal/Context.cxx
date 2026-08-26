#include "sys.h"
#include "Context.h"
#include "ColorPalette.h"

#include <array>
#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <limits>

// This header must be included last.
#include "private_convert.h"

namespace ava::tui::terminal {
namespace {

// Return the terminal's nearest available indexed color for the concrete RGB `color`.
//
// RGB distances are compared without rounding by scaling the 8-bit requested components by 1000 and the ncurses
// components by 255. Ties select the lower terminal color index for deterministic behavior.
int nearest_indexed_color(Color color)
{
  int const rgb = color.as_int();
  // Pass a concrete RGB color; handle the default-color sentinel before requesting a nearest palette entry.
  ASSERT(rgb >= 0);

  int const requested_red = (rgb >> 16) & 0xff;
  int const requested_green = (rgb >> 8) & 0xff;
  int const requested_blue = rgb & 0xff;
  std::int64_t nearest_distance = std::numeric_limits<std::int64_t>::max();
  int nearest_index = -1;

  auto const consider = [&](int color_index, int red, int green, int blue) {
    std::int64_t const red_delta = static_cast<std::int64_t>(red) - requested_red;
    std::int64_t const green_delta = static_cast<std::int64_t>(green) - requested_green;
    std::int64_t const blue_delta = static_cast<std::int64_t>(blue) - requested_blue;
    std::int64_t const distance = red_delta * red_delta + green_delta * green_delta + blue_delta * blue_delta;
    if (distance < nearest_distance)
    {
      nearest_distance = distance;
      nearest_index = color_index;
    }
  };

  if (COLORS == 256)
  {
    // The extended xterm palette is standardized across xterm-compatible 256-color terminals. Do not use color_content here:
    // ncurses cannot query a terminal's palette and, with xterm's mutable-color terminfo entry, reports repeated basic colors
    // rather than the terminal's actual color cube. Likewise, do not reprogram the palette: TERM can name xterm-256color while
    // the actual emulator (for example Konsole) does not implement xterm's palette-changing control sequence.
    constexpr std::array<int, 6> cube_levels{0, 95, 135, 175, 215, 255};
    for (int red = 0; red != 6; ++red)
    {
      for (int green = 0; green != 6; ++green)
      {
        for (int blue = 0; blue != 6; ++blue)
          consider(16 + 36 * red + 6 * green + blue, cube_levels[red], cube_levels[green], cube_levels[blue]);
      }
    }
    for (int gray = 0; gray != 24; ++gray)
    {
      int const level = 8 + 10 * gray;
      consider(232 + gray, level, level, level);
    }
    return nearest_index;
  }

  for (int color_index = 0; color_index < COLORS; ++color_index)
  {
    int red;
    int green;
    int blue;
    if (::extended_color_content(color_index, &red, &green, &blue) == ERR)
      continue;

    consider(color_index, (red * 255 + 500) / 1000, (green * 255 + 500) / 1000, (blue * 255 + 500) / 1000);
  }

  // A color-capable terminal must expose at least one readable palette entry; initialize Context only after start_color succeeds.
  ASSERT(nearest_index >= 0);
  return nearest_index;
}

// Convert `color` to a terminal color index, using `fallback_default_index` when -1 default colors are unsupported.
//
// Direct-color terminals use RGB values as indices. Other terminals select their nearest existing palette entry.
int terminal_color_index(Color color, bool direct_color, bool default_colors_enabled, int fallback_default_index)
{
  if (color.is_default())
    return default_colors_enabled ? -1 : fallback_default_index;
  return direct_color ? color.as_int() : nearest_indexed_color(color);
}

} // namespace

Context::Context(FILE* outfd, FILE* infd) : default_rendition_(ColorPair{{}, 0})
{
  setlocale(LC_ALL, "");

  // From https://invisible-island.net/ncurses/man/curs_util.3x.html
  // use_env   use_tioctl   Summary
  // TRUE      TRUE         ncurses updates LINES and COLUMNS based on operating system calls.
  ::use_env(TRUE);
  ::use_tioctl(TRUE);

  bool use_stdscr = outfd == nullptr && infd == nullptr;

  if (use_stdscr)
  {
    initscr();
    stdscr_.init_as_stdscr();
  }
  else
  {
    BasicScreen first_screen(nullptr, outfd, infd);
    first_screen_ = std::move(first_screen);
    // newterm created a screen-specific stdscr just as initscr does; wrap it so Context initialization and callers can use
    // the same BasicWindow interface with either constructor mode.
    stdscr_.init_as_stdscr();
  }

  wchar_t fill[] = L" ";

  if (has_colors())
  {
    start_color();
    // Enable ncurses' default-color extension: after this succeeds, color
    // number -1 in init_pair/init_extended_pair means the terminal's default
    // foreground or background color instead of an RGB/direct-color index.
    default_colors_enabled_ = ::use_default_colors() == OK;
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

  if (use_stdscr)
  {
    bool const direct_color = COLORS == 0x1000000;
    if (!direct_color)
      // Probe the terminal for its color palette using OSC 4.
      color_palette_ = ColorPalette::create(*this);
  }
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
  bool const direct_color = COLORS == 0x1000000;
  int fallback_foreground_index = COLOR_WHITE;
  int fallback_background_index = COLOR_BLACK;
  if (!default_colors_enabled_ && !direct_color)
  {
    fallback_foreground_index = nearest_indexed_color(Color{0xffffff});
    fallback_background_index = nearest_indexed_color(Color{0x000000});
  }
  int const foreground_index = terminal_color_index(foreground, direct_color, default_colors_enabled_, fallback_foreground_index);
  int const background_index = terminal_color_index(background, direct_color, default_colors_enabled_, fallback_background_index);

  // On a direct-color terminal an RGB value is itself the color index. Indexed terminals instead use their nearest palette color.
  int const color_pair_index = static_cast<int>(color_pairs_.size()) + 1;
  int const status = ::init_extended_pair(color_pair_index, foreground_index, background_index);
  // init_extended_pair returns ERR when the pair index exceeds COLOR_PAIRS or a color index is out of range; keep
  // the number of created pairs within the terminal limit and pass valid Color values.
  ASSERT(status == OK);
  color_pairs_.push_back(ConvertToColorPair{color_pair_index});

  // This function should push the new ColorPair to the end of colors_pairs_.
  return color_pairs_.back();
}

bool Context::has_colors()
{
  return ::has_colors();
}

} // namespace ava::tui::terminal
