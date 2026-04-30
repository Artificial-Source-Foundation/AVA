#include "Session.h"
#include <cstdlib>
#include <locale.h>
#include <curses.h>

// Sanity check.
#if NCURSES_WIDECHAR != 1
#error "NCURSES_WIDECHAR is expected to be defined to 1."
#endif

namespace terminal {

constexpr unsigned int dark_red = 0x2a2222;

Session::Session()
{
  setlocale(LC_ALL, "");
  initscr();

  wchar_t fill[] = L" ";
  setcchar(&background_cchar, fill, A_NORMAL, 0, nullptr);

  if (has_colors())
  {
    start_color();

    printw("TERM=%s\n", getenv("TERM"));
    printw("COLORS=%d COLOR_PAIRS=%d\n", COLORS, COLOR_PAIRS);
    printw("can_change_color=%d\n", can_change_color());

    if (COLORS > dark_red) {
      // Pair 1: white text on dark gray background.
      init_extended_pair(1, COLOR_WHITE, dark_red);
    }
    else
      init_pair(1, COLOR_WHITE, COLOR_BLACK);

    setcchar(&background_cchar, fill, A_NORMAL, 1, nullptr);
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

} // namespace terminal
