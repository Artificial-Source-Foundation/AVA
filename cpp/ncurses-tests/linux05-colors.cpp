// Source - https://stackoverflow.com/a/79930240
// Posted by Giorgos Xou, modified by community. See post 'Timeline' for change history
// Retrieved 2026-04-29, License - CC BY-SA 4.0

#include <ncurses.h>

int main()
{
  SCREEN* screen;

  // Initialize screen with TERM=xterm-direct
  // (that's your best bet since most people don't use direct).
  screen = newterm("xterm-direct", stdout, stdin);

  // Check whether or not was initialized.
  if (!screen) {
    fprintf(stderr, "Failed to initialize terminal.\n");
    return 1;
  }

  // Set screen as active.
  set_term(screen);

  // Start colors :P
  start_color();

  // Assign terminal-default fore/back-ground to color number -1
  // (-1 now works with transparent windows too).
  use_default_colors();

  // Initialize color-pairs with direct-colors
  // (No need of init_color or init_extended_color).
  init_extended_pair(3, 0xff12f8, 0x2d1d3d);
  init_extended_pair(2, 0x7aaa00, -1);          // default terminal background
  init_extended_pair(1, 0xff5f00, 0x7d1d3d);

  // Draw/Print some stuff
  color_set(2, NULL);
  printw("Hello direct-colors!\n");
  attr_set(A_BOLD | A_ITALIC, 3, NULL);
  printw("Hello direct-colors!\n");
  attr_set(A_BOLD | A_REVERSE, 3, NULL);
  printw("Hello direct-colors!\n");
  attr_set(A_ITALIC, 1, NULL);
  printw("Hello direct-colors!\n");
  attr_set(A_ITALIC | A_REVERSE, 1, NULL);
  printw("Hello direct-colors!\n");
  attr_set(A_NORMAL, 0, NULL);
  printw("Hello direct-colors!\n");

  // Display + wait until key + end stuff.
  refresh();
  getch();
  endwin();
  delscreen(screen);
}
