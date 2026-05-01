#include "Session.h"
#include <cstdlib>
#include <locale.h>

// This header must be included last.
#include "private_convert.h"

namespace terminal {

constexpr unsigned int dark_red = 0x2a2222;

Session::Session()
{
  setlocale(LC_ALL, "");
  initscr();

  wchar_t fill[] = L" ";

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

    background_cchar_.set_color_pair(1);
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

} // namespace terminal
