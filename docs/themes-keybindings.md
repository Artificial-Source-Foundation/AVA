# Themes And Keybindings

This guide covers the user-editable TUI display and keyboard shortcut files.
For the wider configuration layout, see [CONFIG.md](CONFIG.md) and
[USAGE.md](USAGE.md).

AVA uses XDG paths on Linux. If `XDG_CONFIG_HOME` is unset or not absolute,
AVA falls back to `~/.config`.

| Setting | Path |
| --- | --- |
| AVA config directory | `$XDG_CONFIG_HOME/ava` or `~/.config/ava` |
| Display theme selection | `$XDG_CONFIG_HOME/ava/display.json` |
| Custom theme files | `$XDG_CONFIG_HOME/ava/themes/*.json` |
| Keybinding overrides | `$XDG_CONFIG_HOME/ava/keybinds.json` |

## Theme selection

AVA has three built-in display modes:

| Theme | Meaning |
| --- | --- |
| `dark` | Built-in dark ncurses palette. This is the final fallback. |
| `light` | Built-in light ncurses palette. |
| `plain` | No ANSI styling/color. Layout, modal text, and width behavior remain available. |

Persist the TUI theme with `/theme`:

```text
/theme                 # show configured and active theme
/theme dark
/theme light
/theme plain
/theme ocean           # custom theme named "ocean"
/theme reset           # clear the stored theme and return to automatic fallback
```

The setting is stored as:

```json
{
  "theme": "light"
}
```

`/theme reset` writes an empty display object instead of deleting the file:

```json
{
}
```

`/theme default` and `/theme auto` are accepted reset aliases. Built-in aliases
such as `ava-dark`, `ava-light`, `none`, `no-color`, and `no_color` are accepted
when AVA reads config, but the user-facing `/theme` form is `dark`, `light`,
`plain`, a custom theme name, or `reset`.

During an interactive TUI run, AVA watches `display.json` and the selected custom
theme file. Valid hand edits are applied automatically; invalid JSON, unsupported
theme names, duplicate custom theme names, or invalid custom theme files are
reported with path-specific diagnostics while the previous active theme remains
in use. Use `/reload theme` when you want an explicit retry or diagnostic.

## Theme precedence and environment variables

The active theme is resolved in this order:

1. `NO_COLOR` with any non-empty value forces `plain` mode.
2. `AVA_TUI_THEME=dark|light|plain` overrides persisted config for this process.
3. `display.json` selects a built-in theme or a valid custom theme.
4. `COLORFGBG` background inference selects `light` or `dark` when available.
5. Built-in `dark` is the fallback.

Examples:

```sh
AVA_TUI_THEME=light ava
AVA_TUI_THEME=plain ava
NO_COLOR=1 ava
COLORFGBG='15;0' ava
```

`AVA_TUI_THEME` only selects built-in themes. Use `display.json` or `/theme` for
custom themes. Unknown `AVA_TUI_THEME` values are ignored rather than written to
config. `COLORFGBG` uses the last `;`- or `:`-separated numeric field as the
terminal background color index; light backgrounds select `light`, and darker
backgrounds select `dark`.

## Custom `themes/*.json`

Custom themes are global user files under `$XDG_CONFIG_HOME/ava/themes/*.json`.
AVA matches by the theme file's `name` field, not by the filename. Names must be
unique, non-empty, must not contain whitespace, `/`, or `\`, must not be `.` or
`..`, and must not conflict with a built-in/reset name.

Example:

```json
{
  "name": "ocean",
  "vars": {
    "primary": "#0066cc",
    "paper": 255
  },
  "colors": {
    "text": "",
    "muted": 242,
    "success": 34,
    "warning": "#ffaa00",
    "error": "#ff0000",
    "accent": "primary",
    "screenBg": "paper",
    "composerBg": 236,
    "toolBg": 235,
    "questionBg": 237
  }
}
```

`colors` must provide the eight required AVA roles below. It may also provide
`toolBg` and `questionBg`; each falls back to `composerBg` when omitted for
compatibility with existing custom themes.

| Role | Typical use |
| --- | --- |
| `text` | Main foreground text. |
| `muted` | Secondary text, metadata, and subtle borders. |
| `success` | Successful status. |
| `warning` | Warning status. |
| `error` | Error or denial status. |
| `accent` | Focus, selection, and brand accents. |
| `screenBg` | Main screen background. |
| `composerBg` | Composer/input background. |
| `toolBg` | Optional low-contrast tool-card background. |
| `questionBg` | Optional distinct question dock/modal background. |

Each color value can be:

- `""` for the terminal default color.
- An integer from `0` through `255` for an xterm-256 color.
- A `"#RRGGBB"` hex color, approximated to the nearest xterm-256 color.
- A variable name from `vars`; variables can themselves be integers, hex strings,
  `""`, or other variables.

AVA's theme schema is intentionally small. A custom theme controls these eight
required ncurses color roles plus optional tool-card and question backgrounds; it does not change typography, glyphs, layout, spacing,
individual markdown/syntax tokens, every permission/tool-card state, or package
resource behavior. `NO_COLOR` and `plain` mode override custom color output.

## Pi parity gaps for themes

AVA supports Pi-compatible behavior where it fits the current product boundary,
but the theme system is not a full Pi clone.

| Pi capability | AVA status |
| --- | --- |
| Large 51-token theme schema | Not implemented. AVA accepts the eight required roles and optional surface backgrounds above. |
| Project-local themes | Not implemented. Themes are loaded only from the user's global XDG config directory. |
| Package-delivered themes and package theme filters | Deferred with package/resource management. AVA does not install or activate theme packages. |
| Remote package/theme marketplace | Deferred pending provenance, trust, compatibility, rollback, and update policy. |
| First-run theme wizard | Deferred. Theme selection is available through `/theme` and `/settings`. |

## Keybinding overrides

AVA keybindings are semantic actions, not raw terminal escape rewrites. The
override file is a JSON object at `$XDG_CONFIG_HOME/ava/keybinds.json`:

```json
{
  "tui.input.submit": "Enter",
  "tui.input.newLine": ["Shift+Enter", "Ctrl+Enter"],
  "app.message.followUp": "Alt+Enter",
  "tui.editor.cursorLineStart": ["Home", "Ctrl+A"],
  "tui.editor.deleteToLineStart": "Ctrl+U",
  "app.models.clearAll": "Ctrl+X",
  "tui.select.confirm": ["Enter", "Space"],
  "tui.select.cancel": ["Esc", "Ctrl+W"]
}
```

Values can be a single string, an array of strings, or a comma-separated string
for compatibility. Missing actions inherit built-in defaults.

Manage the file from inside the TUI:

```text
/keybindings                         # show effective bindings
/hotkeys                             # alias for the same view
/keybindings init [--force]          # create or explicitly replace a starter file
/keybindings import <path> [--force] # validate and install an existing JSON file
/keybindings set <action> <key>[,<key>...]
/keybindings reset <action>          # remove one override and inherit defaults again
/keybindings validate                # check keybinds.json without applying it
/reload keybindings                  # apply valid edits in the running TUI
```

`/keybindings import` resolves relative paths from the current runtime directory.
`init` and `import` refuse to replace an existing `keybinds.json` unless
`--force` is supplied. `set` and `reset` preserve the other valid object entries
while changing only the requested action.

## Action ids and aliases

The generated starter file uses namespaced Pi-style ids where AVA has matching
semantics, such as `tui.input.submit`, `tui.editor.cursorLeft`,
`tui.select.confirm`, `app.models.clearAll`, and `app.session.resume`.

AVA also accepts its snake_case action names and many legacy/camel aliases. For
example, these all resolve to the same action:

```json
{
  "tui.editor.cursorLineStart": "Home",
  "cursor_line_start": "Ctrl+A",
  "cursorLineStart": "Ctrl+A"
}
```

When the same action is listed more than once, the higher-precedence current id
wins; for equal-precedence ids, the later entry wins. Pi ids that do not have an
AVA equivalent are rejected with an action-specific error instead of being
silently ignored.

Common user-facing action groups:

| Group | Canonical ids and accepted alias style |
| --- | --- |
| Input submission | `tui.input.submit` (`submit`), `tui.input.newLine` (`new_line`, `newLine`), `app.message.followUp` (`message_follow_up`, `followUp`) |
| Composer editing | `tui.editor.cursorLeft/Right/Up/Down`, `cursor_left`/`cursorLeft`; `tui.editor.cursorLineStart/End`; `tui.editor.cursorWordLeft/Right`; `tui.editor.deleteCharBackward/Forward`; `tui.editor.deleteWordBackward/Forward`; `tui.editor.deleteToLineStart/End`; `tui.editor.undo`, `tui.editor.redo`, `tui.editor.yank`, `tui.editor.yankPop` |
| App controls | `app.clear` (`clear_input`, `clear`), `app.interrupt` (`interrupt`), `app.exit` (`exit`), `app.editor.external` (`external_editor`, `externalEditor`), `app.suspend` (`suspend`), `app.clipboard.pasteImage` (`clipboard_paste_image`, `pasteImage`) |
| Completion and palettes | `tui.input.tab` (`autocomplete_accept`), `history_prev`, `history_next`, `palette_prev`, `palette_next`, `mode_toggle` |
| Select-list modals | `tui.select.up/down/pageUp/pageDown/confirm/cancel`, plus aliases like `select_prev`, `selectPrev`, `select_confirm`, and `selectConfirm` |
| Transcript and tool details | `tui.editor.pageUp`, `tui.editor.pageDown`, `app.tools.expand` (`details_toggle`, `expandTools`), `jump_to_bottom`, `message_prev`, `message_next` |
| Models and thinking | `app.model.select`, `app.model.cycleForward`, `app.model.cycleBackward`, `app.thinking.cycle`, `app.thinking.toggle`, `app.models.save`, `app.models.enableAll`, `app.models.clearAll`, `app.models.toggleProvider`, `app.models.reorderUp`, `app.models.reorderDown` |
| Sessions and trees | `app.session.new/tree/fork/resume/togglePath/toggleSort/toggleNamedFilter/rename/delete/deleteNoninvasive`, `app.tree.foldOrUp/unfoldOrDown/editLabel/toggleLabelTimestamp/filter.labeledOnly/filter.all` |

Run `/keybindings` to see the exact effective actions and keys for your build.
Some actions intentionally have no default key until a modal or feature makes
them useful.

## Key names and conflict rules

Key names are case-insensitive and tolerate common punctuation differences.
Supported names include letters with modifiers, arrows, editing keys, function
keys, and special keys such as:

- `Enter`, `Shift+Enter`, `Ctrl+Enter`, `Alt+Enter`
- `Backspace`, `Ctrl+Backspace`, `Delete`, `Alt+Delete`, `Insert`, `Clear`
- `Tab`, `Shift+Tab`, `Space`, `Ctrl+Space`, `Esc`
- `Up`, `Down`, `Left`, `Right`, `Ctrl+Left`, `Alt+Right`, `PageUp`, `PageDown`,
  `Home`, `End`
- `Ctrl+A` through supported control letters, `Ctrl+0` through `Ctrl+9`,
  `Ctrl+/`, `Ctrl+]`, `Ctrl+-`
- `Alt+B`, `Alt+D`, `Alt+F`, `Alt+H`, `Alt+J`, `Alt+K`, `Alt+L`, `Alt+W`,
  `Alt+Y`; `Meta+...` is accepted as an `Alt+...` spelling
- `F1` through `F12`

Most configured keys can belong to only one action. AVA validates conflicts in
the user override file and reports the key plus both actions when a conflict is
unsafe. Modal-scoped actions can share keys with normal composer actions: for
example, `tui.select.confirm: "Space"` confirms a highlighted select-list item
without making Space submit normal composer text. Session selector, model
selector, and tree actions are scoped the same way. The semantic `Ctrl+C`
actions for clear/copy/interrupt may share a key by design.

When an override assigns a key to a non-scoped action, AVA removes that key from
other conflicting inherited actions. Unbound Space still inserts text normally;
unbound enhanced keys such as `Ctrl+Space`, `Ctrl+digit`, and `Ctrl+/` remain
inert unless the terminal reports them and a binding uses them.

## Safe writes and validation behavior

Display and keybinding commands validate before they commit a config change.
AVA writes the target through an owner-only temporary file in the same directory,
syncs it, and atomically renames it into place. Existing symlink or non-regular
targets are rejected. Parent directories are created as needed.

Additional keybinding safeguards:

- `keybinds.json` must be a JSON object.
- Import sources and existing edit targets must be regular files.
- Keybinding imports and editable existing files are capped at 256 KiB.
- `init`, `import`, `set`, and `reset` validate the candidate JSON before the
  target is changed, then load the written file again as a final check.
- `/keybindings validate` never changes active bindings.
- A bad hand edit does not replace the current runtime keybindings; fix the file
  and run `/reload keybindings` again.

The safe-write path protects the target file replacement, but it is not a
multi-user merge protocol. Avoid simultaneous manual edits while running
`/keybindings set`, `/keybindings reset`, or `/theme`.
