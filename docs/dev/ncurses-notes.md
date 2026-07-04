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
- Keep `ESCDELAY` deliberately short (`terminal_escape_delay_ms() == 100`) so a plain Escape dismisses palettes/modals quickly while split CSI/OSC/DCS input is still buffered by AVA's escape-sequence parser.
- Handle resize through `KEY_RESIZE`; query dimensions with `getmaxyx(stdscr, rows, cols)` after the resize event.
- `nocbreak()` line-input mode can change resize behavior and should not be used by AVA's interactive TUI loop.

## Backgrounds And Colors

- Do not rely on terminal-default transparent backgrounds for AVA surfaces.
- Pair foreground colors with the active AVA background color; otherwise text cells can reveal the terminal wallpaper/theme.
- If the terminal supports color redefinition, custom dark colors can better approximate AVA's truecolor design.
- If AVA later splits rendering into windows, use `wbkgrnd()`/`werase()` on each window so every window owns its background.

## Manual Probes

Carlo Wood's `origin/cpp-ncurses-tests` branch contains small manual programs for initialization, screen size, resize delivery, panels, and background wrapping. Treat those as reference probes rather than production sources.

The automated `tui_composer` suite covers `newterm` without a real TTY for xterm/screen terminfo and
tmux-, kitty-, wezterm-, and ssh-like environment variables. This validates initialization and bounded
rendering, not full terminal behavior. For real PTY/tmux validation, use the opt-in smoke test. It is
registered with CTest and skips unless `AVA_TUI_TMUX_SMOKE=1` is set:

```sh
AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -R ava_tui.tmux_smoke --output-on-failure
```

For quick manual probing, the equivalent shape is:

```sh
tmux new-session -d -s ava-tui-test -x 100 -y 30 './build/ava'
tmux capture-pane -t ava-tui-test -p
tmux kill-session -t ava-tui-test
```

Use that smoke to verify bracketed paste enable/disable, Escape dismissal latency, resize redraw, mouse wheel/click/drag delivery, Unicode cursor placement, and visible flicker before claiming broader terminal-driver stability.

Manual PTY/tmux checklist:

- Start AVA in a clean pane, resize through narrow/wide/tall/short shapes, and confirm the transcript and composer stay bounded.
- Toggle a modal or palette, press Escape, and confirm the hardware cursor is hidden while the overlay is active and returns to the composer afterward.
- Paste a small multiline block and a large bracketed paste; confirm paste markers are not rendered as text and terminal paste mode is disabled after exit.
- Scroll with the mouse wheel, click palette rows, drag-select composer text, and use Shift+Arrow plus Ctrl+C to select/copy composer text; confirm terminal selection still works outside AVA-specific mouse actions.
- Repeat once inside tmux and once over an ssh-like terminal when available, recording `$TERM`, `$TERM_PROGRAM`, `$COLORTERM`, and `$TMUX`.

## Current Terminal Feature Boundaries

- AVA intentionally keeps a full ncurses redraw per frame. Batch 4 stress coverage remains within budget, so differential redraw/damage tracking is deferred until a real PTY/tmux baseline shows flicker or latency that full redraw cannot meet.
- Cursor flicker is reduced by hiding the hardware cursor during repaint and restoring it only after the composer cursor is moved. Overlay surfaces keep the hardware cursor hidden.
- CSI-u/Kitty keyboard handling covers AVA-supported modified Enter forms, Kitty keypad printable/navigation reports, Shift+Left/Right and Shift+Up/Down character-selection reports, Shift+Ctrl/Alt+Left/Right word-selection reports, Shift+Home/End line-selection reports, Shift+Backspace/Delete delete reports, Ctrl+Backspace session-selector reports, and AVA's known Ctrl/Alt editor chords with base-layout fallback for supported keys. TUI entry pushes Kitty keyboard flags `5` for disambiguated escape codes plus alternate/base-layout key reporting, queries Kitty support, and falls back to xterm modifyOtherKeys (`CSI >4;2m`) when Kitty reports zero flags or only device attributes reply. Restore disables AVA-owned modifyOtherKeys fallback and pops the Kitty keyboard stack. xterm modifyOtherKeys reports (`CSI 27;<modifier>;<codepoint>~`) are parsed for AVA-supported shortcuts and printable text. ncurses shifted vertical arrows are accepted through `kUP2`/`kDN2`, `kri`/`kind`, and guarded `KEY_SUP`/`KEY_SDOWN`/`KEY_SR`/`KEY_SF` paths because tmux-compatible terminfos often expose `CSI 1;2 A/B` as scroll-key capabilities; shifted Delete is accepted through `kDC2` and guarded `KEY_SDC` reports. Key-release semantics, arbitrary custom key ids, and unmapped non-Latin chords remain unsupported; repeat CSI-u events are treated as ordinary input.
- Mouse support consumes wheel, left-click, and composer left-drag selection actions in AVA. Raw SGR/legacy drag and release reports are parsed distinctly so release does not masquerade as click, and raw SGR composer drag selection can replace the selected text on the next edit. Right-click menus and terminal text-selection integration are not implemented.
- IME behavior is validated only through Unicode width/cursor placement and manual smoke testing; AVA does not expose a dedicated IME composition protocol.
- Terminal images, artifact panes, arbitrary OSC clipboard/image negotiation, and synchronized-output capability negotiation remain out of scope for the ncurses frontend hardening slice; explicit copy commands use the existing OSC52 clipboard write path.
