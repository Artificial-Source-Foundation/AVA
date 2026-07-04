# AVA Usage

## Starting AVA

```sh
ava
ava auth login
ava login anthropic
ava auth login moonshot --api-key
ava connect openai
ava connect openai --headless-oauth
ava connect kimi --api-key-env KIMI_API_KEY
printf '%s\n' "$OPENAI_API_KEY" | ava connect openai --api-key-stdin
printf '%s\n' "$ANTHROPIC_API_KEY" | ava connect anthropic --api-key-stdin
ava connect moonshot --api-key-env MOONSHOT_API_KEY
ava --mode plan
ava --continue
ava --session <id-or-prefix>
ava --fork <id-or-prefix>
ava --name "release audit"
ava --session-dir ./ava-sessions
ava --no-session
ava --print "summarize this repo"
ava --rpc
```

`--continue` resumes the newest session for the current workspace. `--session` resumes an exact ID or a unique prefix. `--fork` creates a new branch from an exact ID or unique prefix. `--name` sets the startup session display name, and `--session-dir` selects a custom session storage directory for this process. `--no-session` runs with in-memory history and does not create a resumable JSONL session; it cannot be combined with `--continue`, `--session`, or `--fork`. On exit, AVA prints the command needed to resume the current session unless sessionless mode is active.

## Modes

- Build mode: normal coding mode. Workspace edits are allowed unless a path or command is risky.
- Plan mode: read/search is allowed, but source-code mutation is denied. Planning markdown may be written.

Press Tab in the TUI to accept an open autocomplete suggestion. Slash command names and non-file argument completions use Pi-style fuzzy matching, so `/gp` can find `/grep` and model searches such as `/models sonnet` or `/models 4sonnet` can find a Sonnet model. When no palette is open, Tab first tries file-path completion for the active token, including a slash-command argument token when command-specific completions do not match; if no path candidates match, it toggles mode. `/mode` also switches modes.

## TUI Layout

The interactive TUI runs on wide-character ncurses (`ncursesw`) for terminal mode, keyboard/mouse input, resize handling, and screen drawing. The layout is composer-first:

- The top strip shows AVA identity, mode, provider, model, and basic help.
- The transcript renders user, assistant, inline thinking, tool, audit, and error entries with distinct text markers.
- Tool activity appears as compact lifecycle cards for provider announcements, streamed arguments, execution, progress, completion, cancellation, and errors. Tool details can expand, and backend-provided truncation/omission counts, spill paths, unified diffs, changed paths, failure status, and linked permission audit decisions are rendered when present. Output previews stay bounded even when tool output is large, using backend-provided total/omitted counts when available. Expanded and copied tool details include permission audit/diagnose follow-up commands when a linked permission request id is available.
- On wide terminals, the sidebar shows backend-owned activity, modified files, cwd, branch, session, provider/model, reasoning status when available, usage when available, and loaded context source count.
- The bottom composer is fixed to the bottom rows and uses the old AVA visual language: elevated `#1A1F2E` surface, primary-blue left rail, `❯` prompt glyph, mode badge, provider/model metadata, token metadata slot, and a spinner-only working indicator.
- Enter submits; Shift+Enter or Ctrl+Enter inserts a newline, and Alt+Enter submits while idle or queues a follow-up during an active assistant/compact run. Arrow Up/Down move inside a multiline draft and recall prompt history at the draft boundary before falling back to transcript scrolling, Home/Ctrl+A and End/Ctrl+E move to the current line boundary, Ctrl+Left/Right, Alt+Left/Right, or Alt+B/F move by word-like segment with punctuation boundaries such as `/`, `.`, and `:` kept distinct, Ctrl+] jumps forward to the next typed character and Ctrl+Alt+] jumps backward, Delete or Ctrl+D removes the character after the cursor while the draft has text, Ctrl+W or Alt+Backspace deletes the previous word segment, Alt+D or Alt+Delete deletes the next word segment, Ctrl+U deletes to the current line start, Ctrl+K deletes to line end and joins the next line when already at line end, Page Up/Page Down or mouse wheel scroll transcript history even when the pointer is over the composer, and mouse clicks select visible slash-palette, file-reference, path-completion, or select-list modal rows when the terminal reports coordinates. Ctrl+- undoes the last composer edit, Ctrl+Z suspends AVA to the shell on Unix terminals and redraws after `fg`, Ctrl+Y yanks the last killed composer text, Alt+Y cycles the just-yanked kill-ring entry, Ctrl+C clears a non-empty composer draft and exits only when the draft is empty, Ctrl+D exits the current TUI loop when the composer is empty, Ctrl+G opens the draft in `$VISUAL` or `$EDITOR` and replaces the composer with the saved file when the editor exits successfully, Ctrl+V imports a clipboard image as a pending attachment when a supported helper is available, Ctrl+L opens the model selector between turns, Ctrl+P cycles to the next configured or scoped enabled model between turns, Shift+Ctrl+P cycles to the previous model when the terminal reports that enhanced key sequence, Shift+Tab cycles backend-declared reasoning levels for the active model when available, Ctrl+T toggles thinking block visibility without changing provider reasoning mode, and Esc dismisses the active palette or starts the safe clear-input flow. `/scoped-models` opens the model-cycle selector for Ctrl+P; changes apply to the running TUI immediately, and Ctrl+S persists the scoped cycle in `models.json`. While AVA is actively responding, Esc requests a cooperative stop and keeps the TUI open; stopped turns say to submit a new prompt to continue. During an active assistant or `/compact` run, typing a draft and pressing Enter or Alt+Enter queues a backend-owned follow-up turn. Typing `/steer your note` during an active run queues steering for the next safe provider boundary. Pending queued items render above the composer; `/restore` or Alt+Up restores the latest pending queued item to the draft before it starts. Stopping the active run skips pending queued items, and queue outcomes render as transcript audit entries with delivery guidance.
- Bracketed paste is enabled while the TUI is active. Pasted multi-line text is normalized into the draft instead of being treated as submitted commands, and tall drafts show a `draft +N above` indicator when earlier draft lines are hidden.

On a fresh config, if the selected provider has no stored or environment credential, the TUI shows a setup row before the first prompt with `/connect`/`/login`, CLI, environment variable, and auth-file paths. Submitting a provider prompt before connecting returns the same guidance while offline slash commands remain available.

Use `/theme dark`, `/theme light`, `/theme plain`, `/theme <custom-name>`, or `/theme reset` to persist AVA's TUI display mode in `$XDG_CONFIG_HOME/ava/display.json`. Custom theme files live under `$XDG_CONFIG_HOME/ava/themes/*.json` and define AVA's eight ncurses color roles. The `/settings` view also exposes selectable built-in and valid custom theme rows that write the same setting, plus model rows that open the model selector and scoped Ctrl+P cycle selector. The interactive TUI automatically reloads hand-edited `display.json` and the selected custom theme file; `/reload theme` remains available for an explicit retry or diagnostic. Set `AVA_TUI_THEME=dark|light|plain` to override the theme for the current process, or set standard `NO_COLOR=1` to render without ANSI styling while preserving the same visible layout, modal content, and width bounds; `NO_COLOR` wins over both environment and persisted theme values. When no explicit theme is set, AVA uses `COLORFGBG` when the terminal exports it to infer a light or dark built-in palette before falling back to dark. `/settings` reports the active display mode and source.

TUI keybindings are semantic and can be overridden with `$XDG_CONFIG_HOME/ava/keybinds.json`. Run `/keybindings init`
to create a validated starter file in that location, `/keybindings import <path> [--force]` to validate and install
an existing JSON file from the current runtime directory, or `/keybindings set <action> <key>[,<key>...]` to edit one
action from inside the TUI. Use `/keybindings reset <action>` to remove one override so that action inherits built-in
defaults again. The starter uses Pi-style action ids where AVA has equivalent actions and omits ambiguous shared
defaults so those actions continue to inherit the built-in bindings. `/settings` reports the active keybinding count
and shows actionable keybinding rows: open active bindings, validate `keybinds.json`, draft `/keybindings set`, and
reload live bindings. A hand-written override can look like:

```json
{
  "tui.input.submit": "Enter",
  "tui.input.newLine": ["Shift+Enter", "Ctrl+Enter"],
  "app.message.followUp": "Alt+Enter",
  "autocomplete_accept": "Tab",
  "tui.editor.deleteCharBackward": ["Backspace", "Ctrl+H"],
  "tui.editor.cursorLeft": ["Left", "Ctrl+B", "Alt+H"],
  "tui.editor.cursorWordRight": ["Ctrl+Right", "Alt+Right", "Alt+F", "Alt+W"],
  "delete_to_line_start": "Ctrl+U",
  "undo": "Ctrl+-",
  "app.suspend": "Ctrl+Z",
  "app.clipboard.pasteImage": "Ctrl+V",
  "yank": "Ctrl+Y",
  "app.clear": "Ctrl+C",
  "app.editor.external": "Ctrl+G",
  "model_select": "Ctrl+L",
  "model_cycle_forward": "Ctrl+P",
  "model_cycle_backward": "Shift+Ctrl+P",
  "app.models.enableAll": "Ctrl+A",
  "app.models.clearAll": "Ctrl+X",
  "app.models.reorderUp": "Alt+Up",
  "app.models.reorderDown": "Alt+Down",
  "message_dequeue": "Alt+Up",
  "app.session.togglePath": "Ctrl+P",
  "app.session.toggleSort": ["Ctrl+S", "Ctrl+T"],
  "app.session.toggleNamedFilter": "Ctrl+N",
  "app.session.rename": "Ctrl+R",
  "app.session.delete": "Ctrl+D",
  "app.session.deleteNoninvasive": "Ctrl+Backspace",
  "tui.select.confirm": ["Enter", "Space"],
  "tui.select.cancel": ["Escape", "Ctrl+W"],
  "variant_cycle": "Shift+Tab",
  "app.thinking.toggle": "Ctrl+T"
}
```

Use a string for one key, an array of strings for multiple keys, or the existing comma-separated string form for compatibility. Action keys may use AVA snake_case names or matching Pi-style namespaced ids such as `tui.editor.cursorLineStart`, `tui.input.submit`, `tui.select.confirm`, `app.clear`, `app.editor.external`, `app.suspend`, `app.clipboard.pasteImage`, `app.tools.expand`, `app.thinking.toggle`, `app.message.followUp`, `app.message.dequeue`, `app.models.save`, `app.models.enableAll`, `app.models.clearAll`, `app.models.toggleProvider`, `app.models.reorderUp`, `app.models.reorderDown`, `app.session.new`, `app.session.tree`, `app.session.fork`, `app.session.resume`, `app.session.togglePath`, `app.session.toggleSort`, `app.session.toggleNamedFilter`, `app.session.rename`, `app.session.delete`, `app.session.deleteNoninvasive`, `app.tree.foldOrUp`, `app.tree.unfoldOrDown`, `app.tree.editLabel`, `app.tree.toggleLabelTimestamp`, `app.tree.filter.labeledOnly`, and `app.tree.filter.all`; Pi ids without an AVA equivalent are rejected with an action-specific error. Named keys include `Space`, `Ctrl+Space`, `Ctrl+/`, `Ctrl+0` through `Ctrl+9`, `Insert`, `Clear`, and `F1` through `F12`; unbound Space still inserts text normally, and unbound Ctrl+Space, Ctrl+digit, or Ctrl+/ remains inert in prompts/selectors while raw NUL, Kitty CSI-u, and xterm modifyOtherKeys Ctrl+Space reports plus Kitty CSI-u and xterm modifyOtherKeys Ctrl+digit/Ctrl+/ reports can drive custom bindings. Select-list bindings under `tui.select.*`, session selector bindings under `app.session.*`, model selector bindings under `app.models.*`, and tree branch/label/filter/timestamp bindings under `app.tree.*` are modal-scoped where appropriate, so `tui.select.confirm: "Space"` confirms highlighted rows without changing composer submission, `tui.select.cancel: "Ctrl+W"` does not remove composer delete-word behavior, `app.models.clearAll: "Ctrl+X"` clears only the scoped model-cycle selector, `app.tree.filter.labeledOnly` maps to AVA's named/labeled session filter, `app.tree.filter.all` maps to AVA's archived-session visibility toggle, and selector actions can share keys with editor actions safely. `app.clear`, `tui.input.copy`, and `app.interrupt` share Ctrl+C by state: selected composer text copies, a non-empty draft clears, and an empty draft exits. Vim-style `Alt+H/J/K/L` cursor aliases and `Alt+W` word-right are accepted for custom cursor bindings. When a legacy camelCase alias and a current AVA or namespaced key both target the same AVA action, the current key wins. Custom bindings may intentionally shadow default keys, but a `keybinds.json` file that assigns the same key to more than one configured action in the same context is rejected by the loader, rendered as a startup alert, and AVA starts with the default bindings. Run `/keybindings validate` to check the file without changing active bindings; `/keybindings import <path> [--force]` performs the same validation before replacing `keybinds.json` and refuses accidental overwrite unless `--force` is present; `/keybindings set <action> <key>[,<key>...]` rewrites one action only after the full candidate file validates; `/keybindings reset <action>` removes all equivalent override aliases for one action. Then run `/reload` or `/reload keybindings` in the TUI after editing, importing, setting, or resetting `keybinds.json` to apply changes without restarting; reload errors stay visible and the previous active bindings remain in use. Current named actions include composer submission/editing, transcript scroll, palette navigation, select-list navigation/confirm/cancel/page, model select/cycle/scope actions, session selector open/new/fork/archive workflows, tree branch/label/filter/timestamp selector workflows, prompt placeholders, `clear_input`, `mode_toggle`, `variant_cycle`, `thinking_toggle`, `external_editor`, `suspend`, `clipboard_paste_image`, `message_follow_up`, `message_dequeue`, `interrupt`, and `exit`. `/help`, `/hotkeys`, `/keybindings`, and `/settings` show or summarize the effective bindings that are active in the TUI and point back to the init/import/set/reset/validate/config/reload workflow; pressing Enter on a `/hotkeys` or `/keybindings` row drafts `/keybindings set <action> ` for that action, and selecting keybinding rows in `/settings` can open `/hotkeys`, run validation, draft a general edit command, or reload live bindings.

The slash palette opens above the composer while typing `/`. Use arrows to move focus, Tab or Enter to insert the selected command or argument suggestion, and Esc to dismiss without clearing the draft. The selected item is visually highlighted with a `›` marker. Disabled planned commands remain visible with a reason and are not submitted as model prompts. Argument suggestions are only shown from backend/session data sources, such as configured models, resumable sessions with names/labels, loaded context sources, configured MCP servers, plugin metadata, and bounded workspace path candidates for `/read`, `/attach`, `/write`, `/glob`, and `/grep` include globs. Slash path candidates are relative to the current directory, skip reference-code/build/cache-heavy folders, and keep directories open for nested completion. Large bracketed pastes collapse to a visible `[paste #N ...]` marker in the composer, behave as one editable unit for character/word cursor movement and deletion, and expand back to the original pasted text when submitted.

Typing `@` in normal prompt text opens file-reference suggestions for bounded workspace-relative paths. Suggestions use case-insensitive fuzzy matching, include paths with spaces as quoted `@"path with spaces"` tokens, keep directories open for nested completion, and recognize token delimiters such as whitespace, `=`, single quotes, and prose wrappers like `(@src)` or `[@src]`. When the prompt is submitted, AVA reads each referenced file through the permissioned read path and appends bounded file content to the user message sent to the provider.

Normal prompt text also opens path suggestions when the active token is path-like, such as `src/`, `./src`, `file=src/`, `path='src/`, or `path="my folder/`, and when the cursor is at a new token after whitespace. Pressing Tab on a bare token such as `main` forces path completion and either inserts the single match or opens a path palette for multiple matches. These completions only edit the draft text; use `@` when the file content should be attached to the provider prompt.

Use `/attach <path>` or `/image <path>` in the TUI to import a local PNG, JPEG, WebP, or GIF image into AVA-managed session attachment storage. Relative paths resolve from the current runtime directory. Press Ctrl+V to import a clipboard image through `wl-paste` or `xclip` on Linux when one of those helpers exposes a supported image MIME type; `AVA_CLIPBOARD_IMAGE_FILE=/path/to/image.png` can be used as a deterministic override for scripted terminal smoke tests. The TUI shows a pending attachment row above the composer with the image name or clipboard label, MIME type, byte count, and detected preview mode, then sends the queued image with the next normal prompt. `/settings` reports the active image-preview capability, including Kitty/iTerm2-compatible terminals and text-only fallbacks under tmux/screen/unknown terminals. When the active terminal supports it, AVA loads the validated session-owned image bytes and emits a row-reserved inline preview through a trusted renderer side channel; unsupported terminals, tmux/screen, and `NO_COLOR`/plain mode keep textual metadata only. Attachment imports are rejected before a provider turn starts when the source is unsupported, empty, a symlink, non-regular, or over the session attachment cap.

Project `AGENTS.md` context files still load by default, but executable or model-influencing project resources are skipped until the workspace is trusted. Use `/trust status` to inspect the current decision and protected resources, `/trust project` to enable project prompt commands, project skills, project plugins, project MCP/LSP config, and project `SYSTEM.md` or `APPEND_SYSTEM.md` files for this workspace, `/trust deny` to keep them skipped, and `/trust clear` to remove this workspace's explicit decision.

Use `--system-prompt "text"` to replace the selected system prompt for the current AVA process, including built-in/provider prompt overrides and discovered `SYSTEM.md` files. Use repeated `--append-system-prompt "text"` flags to append extra instructions; when present, these CLI append values replace discovered `APPEND_SYSTEM.md` files. Loaded context, prompt commands, skills, and plugin prompt/skill resources are still appended after the CLI prompt text, and `/context` reports the CLI sources as inline.

`/connect` opens a centered provider login modal. Type to search providers, use arrows to move selection, press Enter to confirm, then choose a login method. OpenAI shows ChatGPT Pro/Plus browser OAuth, headless OAuth, and API key options; OAuth waiting modals keep the URL and `C` copy shortcut visible, and browser OAuth opens the default browser automatically. `/login` is an alias. This is the intended path from the first-run setup row to an authenticated TUI prompt.

## Permission Prompts

Interactive permission requests replace the composer with an approval dock. The dock shows the tool, command or path summary, risk label, reason, selected action, and key help. File mutation prompts show a backend-provided unified diff before approval when AVA can safely compute one; otherwise the prompt falls back to the conservative summary without inventing a diff in the TUI.

- `A`: allow once
- `D`: deny
- `R`: remember the selected allow or deny action as a persistent exact-match rule when available
- `Enter` or Space: confirm the selected action
- `Esc`: deny
- Tab, Left, or Right: move selection

`Deny` is the default focused action. `A` and `D` are one-shot decisions. When persistent rules are available, the dock also shows remembered allow/deny rule choices; `R` toggles the selected action between one-shot and remembered. A remembered TUI choice creates an exact workspace-scoped persistent rule for the prompt fields before resolving the current prompt. Permission decisions are recorded in the session audit trail and export when a session is active. Use `/permissions audit [query]` to inspect recent session decisions, `/permissions audit summary [query]` to group matching decisions by action, resolution, source, risk, operation, and tool, `/permissions audit show <entry_id|request_id>` to drill into a specific audit entry or request, `/permissions audit export [query]` to render matching decisions as a copyable Markdown table, `/diff [query]` to show the latest or matching unified tool diff in the TUI, `/copy permission [query]` to copy the latest or matching permission detail payload through the TUI clipboard path, `/permissions diagnose [query]` to explain recent denials with reasons and follow-up commands, and `/permissions` to list, explain, add, or remove persistent rules. RPC clients can create process-local exact-match session grants with `allow_session`; those grants are inspectable, revocable, clearable, and not persisted across restarts.

## Commands

- `/help`: show commands and hotkeys
- `/hotkeys` or `/keybindings`: show effective TUI keybindings and draft edits for selected actions
- `/keybindings init [--force]`: create or explicitly replace a validated keybindings starter file
- `/keybindings import <path> [--force]`: validate and install a keybindings JSON file
- `/keybindings set <action> <key>[,<key>...]`: validate and edit one keybinding action
- `/keybindings reset <action>`: remove one override so the action inherits built-in defaults
- `/keybindings validate`: validate `keybinds.json` without changing active bindings
- `/reload [keybindings|theme]`: reload TUI keybindings from `keybinds.json` or display theme from `display.json`
- `/theme [dark|light|plain|custom-name|reset]`: show or persist the TUI display theme in `display.json`
- `/mode`: toggle build/plan mode
- `/details` or Ctrl+O: toggle tool detail expansion in the TUI
- `/tool [query]`: show the latest or matching expanded tool card in the TUI transcript; `/tools` is an alias
- `/diff [query]`: show the latest or matching unified tool diff in the TUI transcript
- `/copy [tool|diff|permission] [query]`: copy the latest AVA message, latest or matching tool-card details, latest or matching unified diff, or latest or matching permission audit details through the terminal clipboard path
- `/thinking`: toggle inline thinking block visibility in the TUI without changing provider reasoning mode
- `/attach <path>`: import a local PNG/JPEG/WebP/GIF image and send it with the next normal TUI prompt; `/image` is an alias, and Ctrl+V imports a supported clipboard image when `wl-paste` or `xclip` can read one
- `/connect`: open provider and login method modals; `/login` is an alias
- `/models [query|provider/model]`: list configured models, provider/model capabilities, custom-model diagnostics, and unregistered-provider warnings; `/model` is an alias. Exact `/model` or `/models`, Ctrl+L, and the `/settings` model row open the TUI model selector with the same diagnostic signals; Ctrl+P cycles to the next configured or scoped enabled model between turns.
- `/scoped-models`: open the TUI scoped model-cycle selector. Enter toggles the highlighted model, Ctrl+A enables visible models, Ctrl+X clears visible models, Ctrl+P toggles the highlighted provider, Alt+Up/Alt+Down reorder enabled models, and Ctrl+S persists the current scope as top-level `scoped_model_cycle` in `models.json`; a missing field means all registered models are enabled, while an empty array means no models are enabled for cycling. The `/settings` model-cycle row opens the same selector.
- `/sessions [--archived] [query|id]`: show the session tree for this workspace, optionally filtered by id, name, label, branch metadata, path, archive state, or timestamp; `/tree` is an alias
- `/sessions rename <id> <name|--clear>`: rename a session without switching to it
- `/sessions labels <id> <label...|--clear>`: set or clear labels on a session without switching to it
- `/sessions archive <id> --confirm` and `/sessions unarchive <id>`: hide or restore a non-current session without deleting its JSONL file
- `/fork [name]`: fork the current session at its latest entry and switch to the new branch
- `/clone [name]`: clone the full current session and switch to the copy
- `/new [name]`: start a fresh session and switch to it
- `/resume [id]`: resume/switch to an existing session by exact id or unique prefix; exact `/resume` opens the TUI session selector, where PageUp/PageDown page through rows, Ctrl+S or Ctrl+T cycles recent/name/path sort, Ctrl+N toggles named sessions only, Ctrl+P toggles path display, Ctrl+A shows/hides archived sessions, Ctrl+R restores a rename command, Ctrl+L or Shift+L restores a labels command, Shift+T toggles label update timestamps, and Ctrl+D twice or Ctrl+Backspace twice archives or restores the highlighted session when the selector search is empty
- `/name <name|--clear>`: set or clear the current session display name; `/rename` is an alias
- `/labels <label...|--clear>`: set or clear current session labels; `/label` is an alias. Labels are unique, non-empty strings separated by spaces.
- `/context [query|source]`: list mode/model, prompt source, loaded system prompt resources, context files, prompt commands, skills, plugin manifest/resources, and current/changed/missing status, optionally filtered by prompt, command, skill, plugin, path, or source type
- `/trust [status|project|deny|clear]`: inspect or change the workspace trust decision for project-local commands, skills, plugins, MCP/LSP config, and system-prompt resources
- `/compact [instructions]`: generate and record a provider summary
- `/export [markdown|html] [path]`: export this session as Markdown or safe self-contained HTML. With no path, AVA prints the export text in the command output. With a path, AVA writes through the normal permissioned file-mutation path. `/export <file.html>` is treated as a Pi-style HTML file export.
- Session exports include sanitized textual image-attachment metadata only; raw attachment bytes are not embedded in Markdown or HTML exports.
- `/stats`: show session counts, usage, cost, and resume/export hints; `/status` is an alias
- `/permissions <list|audit|diagnose|explain|add|remove> ...`: inspect session permission audits, group matching decisions with `/permissions audit summary [query]`, drill into `/permissions audit show <entry_id|request_id>`, render `/permissions audit export [query]` Markdown tables, explain recent denials with `/permissions diagnose [query]`, and manage persistent permission rules; `/permission-rules` and `/perms` are aliases
- `/read <path>`: read a file
- `/write <path> <text>`: write a file through permission checks using atomic replacement where practical
- `/glob <pattern>`: list readable matching files
- `/find <pattern>`: Pi-style alias for `/glob <pattern>`
- `/ls [path]`: Pi-style directory-list alias for the native list tool
- `/grep <text> [glob]`: search readable files for literal text
- `/bash <command>`: run a conservative permissioned command
- `!<command>` or `!!<command>`: run the same permissioned shell helper directly from the composer; output stays visible/audited and is not injected into provider context unless you paste it into a later prompt
- `/plugins list|inspect|enable|disable|validate|failures|prompts|prompt|skills|skill`: inspect and manage local plugins
- `/plugin run <id> <command> [arguments_json]`: run a plugin command through the permissioned extension path
- `/mcp list|inspect|tools|restart`: inspect configured MCP servers and discover tools
- `/quit`: exit

See `docs/plugin-system.md` for the stable local plugin authoring guide and `examples/plugins/todo/` for a minimal sample plugin.

Planned but unavailable commands such as `/import` and `/logout` are recognized and return a disabled explanation instead of being sent to the model.

## Non-TTY Mode

When stdin/stdout are not both terminals and no headless mode is selected, AVA uses a line shell instead of the TUI. This keeps tests and scripts non-interactive.

```sh
printf '/sessions\n/quit\n' | ava --continue
```

## Headless Modes

Use print mode for one-shot automation:

```sh
ava "summarize this repo"
ava summarize this repo
ava --no-session "summarize without saving history"
ava --name "release audit" "audit the release notes"
ava --fork <id-or-prefix> --name "alternate fix" "try a smaller patch"
ava --session-dir ./tmp-sessions --print "use an isolated session directory"
ava @README.md "summarize this file"
ava -p "@docs/notes with spaces.md" "summarize this file"
ava --print "summarize this repo"
printf 'summarize this repo\n' | ava --print
ava --print "summarize this repo" --json
ava --mode json "summarize this repo"
ava --tools read_file,grep "inspect files safely"
ava --tools read,grep,find,ls "inspect files with Pi-style names"
ava --no-tools --tools read_file "read only the file I name"
```

Bare prompt text uses the same one-shot path as `--print`; multiple positional prompt tokens are joined with spaces. CLI `@path` text file arguments are collected separately from prompt words and expanded through the same bounded permissioned file-reference path used by the TUI. Quote the shell argument when the path contains spaces, as in `"@docs/notes with spaces.md"`. Text print mode writes final assistant text to stdout and diagnostics to stderr. JSON print mode writes newline-delimited runtime events to stdout and can be selected with `--json`, `--output json`, or Pi-compatible `--mode json`. `--mode text` is a compatibility alias for one-shot text print mode. Prompt turns require configured auth for the active provider.

Use `--name` with interactive, print, or RPC startup to set the display name before the first turn. Use `--fork <id-or-prefix>` to start a new persisted branch from an existing session; it can be combined with `--name` but not with `--session`, `--continue`, or `--no-session`. Use `--session-dir <dir>` when a process should read and write sessions somewhere other than the default XDG state directory. Use `--no-session` when the current process should not write a resumable session file. Session data remains available to in-process commands while AVA is running, but `--continue`, `--session`, `--fork`, and persisted session listings ignore it.

Tool visibility is process-local and applies to interactive, print, and RPC sessions. Use `--tools name[,name...]` to advertise and dispatch only selected tools, `--exclude-tools name[,name...]` to remove tools, `--no-builtin-tools` to hide AVA built-ins while leaving enabled plugin/MCP tools available, and `--no-tools` to hide everything unless an explicit `--tools` list re-enables names. AVA accepts native names and Pi aliases: `read` maps to `read_file`, `write` to `write_file`, `edit` to `edit_file`, `find` to `glob`, and `ls` to `list_directory`. `--exclude-tools` wins over `--tools`. These flags do not grant permissions; combine them with `--allow-tool` only when a headless visible tool should also auto-approve a matching backend permission prompt.

Headless modes are fail-closed for permission prompts unless an explicit supported read/search policy is supplied. This applies to backend decisions whose action is `ask`; operations allowed directly by the workspace policy do not create a prompt. Skill loading, network access, and MCP access are separate: `--allow read-only` does not allow those prompts, while `--allow-tool skill`, `--allow-tool webfetch`, `--allow-tool websearch`, and `--allow-tool mcp` can auto-allow exact matching prompts for those tool families. In RPC mode, matching prompts are auto-allowed before `permission_requested`; non-matching asks still require `permission_reply`:

```sh
ava --print "inspect this project" --allow read-only
ava --print "read the README" --allow-tool read_file
ava --print "list and search files" --allow-tool list_directory,glob,grep
ava --print "load a listed skill" --allow-tool skill
ava --print "fetch release notes" --allow-tool webfetch
ava --print "search current release notes" --allow-tool websearch
ava --rpc --allow read-only
```

Use RPC mode for a long-lived JSONL stdio automation client:

```sh
printf '%s\n' '{"id":"state","type":"get_state"}' | ava --rpc
printf '%s\n' '{"id":"state","type":"get_state"}' | ava --mode rpc
```

See `docs/headless-protocol.md` for the complete stdout/stderr contract, event types, RPC commands, and exit-code behavior. RPC `prompt` requests can include `attachments:["path/to/image.png"]` for local files or Pi-style `images:[{"type":"image","data":"...","mimeType":"image/png"}]` uploads to import PNG/JPEG/WebP/GIF images into AVA-managed session attachment storage for image-capable models. RPC `export` returns the Markdown session export; `export_html` returns HTML unless `outputPath`/`output_path` is supplied, in which case AVA writes the HTML through the permissioned file path. RPC `list_commands` and `invoke_command` expose project/global prompt commands, skills, plugin commands, and MCP prompt commands through the shared command registry.

## Current Limits

- Built-in providers are OpenAI, Anthropic, Kimi, Moonshot, and OpenRouter. OpenAI remains the default production path. Anthropic uses native Messages API support for tools and reasoning. Kimi, Moonshot, and OpenRouter use OpenAI-compatible shims with built-in profile tests; live credentialed smokes are still release-validation work.
- Built-in model-visible tools are `read_file`, `list_directory`, `write_file`, `edit_file`, `apply_patch`, `glob`, `grep`, `bash`, `webfetch`, `websearch`, `skill`, `question`, and capability-gated LSP tools for diagnostics, document symbols, workspace symbols, definitions, and references. For Pi muscle memory, `/find` runs the native glob path, `/ls` runs the native directory-list path, and visibility flags accept Pi `find`/`ls` aliases without adding duplicate provider schemas.
- Local plugin and MCP foundations exist for command discovery, plugin diagnostics, plugin prompt/skill resources and commands, MCP stdio tool discovery/calls, MCP read-style resources, and MCP prompts through command-registry entries. The stable local plugin authoring guide lives in `docs/plugin-system.md`, the compatibility policy lives in `docs/plugin-compatibility-policy.md`, and a minimal sample lives under `examples/plugins/todo/`; representative golden/audit/failure contract tests now cover the v1 foundation.
- The HTTP transport uses the local `curl` executable.
- The TUI now renders assistant text, lightweight Markdown headings/nested lists with ordered-marker normalization, loose-list continuation indentation, task markers/lazy blockquotes/pipe tables with Pi-style borders, narrow fallback, block spacing around headings/dividers/fenced code, visible HTML-like literal tags, and inline code cells/fenced source, JavaScript/TypeScript, HTML, CSS, YAML, and diff syntax coloring/inline styling/bare web URL and email styling/link fallbacks, OSC 8 hyperlinks on terminals that advertise support, inline thinking blocks, and tool lifecycle updates live. Tool cards show backend-provided truncation, spill, diff metadata, and distinct canceled state when those fields are present; the live runtime event path still has summary-only tool results for some backend producers.
- Permission `ask` decisions open a TUI prompt in interactive mode and fail closed in non-TTY/headless mode unless a supported read/search allow policy is supplied or an RPC client answers `permission_requested` with `permission_reply`. Workspace-policy `allow` decisions can still proceed without a resolver, while policy `deny` decisions are never upgraded by headless flags. File mutation asks include `diff_preview` and `diff_truncated` when the backend can safely provide a unified diff. Successful RPC permission/question replies emit `permission_replied` or `question_replied` events before their in-band response; permission replies may include a bounded `reason` field to explain the resolution. Denied tool results preserve the backend reason, risk, resolution reason, generated permission request id, and follow-up `/permissions audit show <request_id>` plus `/permissions diagnose <request_id>` commands in structured tool details; text print mode also writes those details to stderr, and TUI expanded/copied tool cards mirror the follow-ups from linked audit metadata. On narrow or `NO_COLOR` terminals, expanded permission cards split decision, risk, request id, reason, command, and audit/diagnose follow-ups into separate text rows instead of relying on color. Session permission decisions can be inspected with `/permissions audit [query]`, grouped with `/permissions audit summary [query]`, drilled into with `/permissions audit show <entry_id|request_id>`, rendered as a Markdown table with `/permissions audit export [query]`, or summarized as denial diagnostics with `/permissions diagnose [query]`. Persistent permission rules are managed with `/permissions list`, `/permissions diagnose`, `/permissions explain <rule_id>`, `/permissions add ...`, and `/permissions remove <rule_id>`; the TUI command and remembered prompt choices use the same protected rule files as RPC rule commands. Matching persistent rules are checked before interactive TUI prompts. RPC `allow_session` replies create process-local exact-match grants that can be listed with `permission_grants`, revoked with `permission_grant_revoke`, or cleared with `permission_grants_clear`.
- Shared runtime events now include backend-owned compaction, bounded provider/compaction retry, retry countdown ticks, cancel-request, and terminal canceled markers. The TUI renders those as audit/status items instead of inferring lifecycle state from command text; retry and compaction markers show attempt totals, retry delays, and a live countdown audit/status item when the backend emits retry ticks.
- Interactive/TUI mode supports local modals for provider login plus question prompts with single-select, multi-select, custom text, and cancel handling. RPC clients can answer `question_requested` with `question_reply` using `answer`, `selected`, or `selected_options` for multi-select prompts.
