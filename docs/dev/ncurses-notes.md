# ncursesw Notes

These notes capture the ncurses behavior AVA relies on for the 0.32 TUI.

## Build

- Require wide-character ncurses with `CURSES_NEED_WIDE TRUE` before `find_package(Curses)`.
- Keep the compile-time `NCURSES_WIDECHAR` guard in the TUI curses path so a plain curses build fails early.
- Only add `panelw` if AVA starts using panel overlays. Permission prompts currently draw through the main screen path.

## Terminal Lifecycle

- Call `setlocale(LC_ALL, "")` before initializing curses so UTF-8 input and output use the active locale.
- Use `newterm`/`endwin` under `CursesSession` so terminal state restoration stays RAII-bound.
- Keep TTY checks outside curses initialization; non-TTY use must continue through the line shell.

## Input And Resize

- Use `cbreak()` and `noecho()` for interactive input.
- Handle resize through `KEY_RESIZE`; query dimensions with `getmaxyx(stdscr, rows, cols)` after the resize event.
- `nocbreak()` line-input mode can change resize behavior and should not be used by AVA's interactive TUI loop.

## Backgrounds And Colors

- Do not rely on terminal-default transparent backgrounds for AVA surfaces.
- Pair foreground colors with the active AVA background color; otherwise text cells can reveal the terminal wallpaper/theme.
- If the terminal supports color redefinition, custom dark colors can better approximate AVA's truecolor design.
- If AVA later splits rendering into windows, use `wbkgrnd()`/`werase()` on each window so every window owns its background.

## Manual Probes

Carlo Wood's `origin/cpp-ncurses-tests` branch contains small manual programs for initialization, screen size, resize delivery, panels, and background wrapping. Treat those as reference probes rather than production sources.
