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
ava --print "summarize this repo"
ava --rpc
```

`--continue` resumes the newest session for the current workspace. `--session` resumes an exact ID or a unique prefix. On exit, AVA prints the command needed to resume the current session.

## Modes

- Build mode: normal coding mode. Workspace edits are allowed unless a path or command is risky.
- Plan mode: read/search is allowed, but source-code mutation is denied. Planning markdown may be written.

Press Tab in the TUI to accept an open autocomplete suggestion. When no palette is open, Tab first tries file-path completion for the active token; if no path candidates match, it toggles mode. `/mode` also switches modes.

## TUI Layout

The interactive TUI runs on wide-character ncurses (`ncursesw`) for terminal mode, keyboard/mouse input, resize handling, and screen drawing. The layout is composer-first:

- The top strip shows AVA identity, mode, provider, model, and basic help.
- The transcript renders user, assistant, inline thinking, tool, audit, and error entries with distinct text markers.
- Tool activity appears as compact lifecycle cards for provider announcements, streamed arguments, execution, progress, completion, and errors. Tool details can expand, and backend-provided truncation/omission counts, spill paths, unified diffs, changed paths, failure status, and linked permission audit decisions are rendered when present.
- On wide terminals, the sidebar shows backend-owned activity, modified files, cwd, branch, session, provider/model, reasoning status when available, usage when available, and loaded context source count.
- The bottom composer is fixed to the bottom rows and uses the old AVA visual language: elevated `#1A1F2E` surface, primary-blue left rail, `❯` prompt glyph, mode badge, provider/model metadata, token metadata slot, and a spinner-only working indicator.
- Enter submits; Shift+Enter, Ctrl+Enter, or Alt+Enter inserts a newline. Arrow Up/Down move inside a multiline draft and recall prompt history at the draft boundary before falling back to transcript scrolling, Home/Ctrl+A and End/Ctrl+E move to the current line boundary, Ctrl+Left/Right, Alt+Left/Right, or Alt+B/F move by word, Ctrl+] jumps forward to the next typed character and Ctrl+Alt+] jumps backward, Delete or Ctrl+D removes the character after the cursor while the draft has text, Ctrl+W or Alt+Backspace deletes the previous word, Alt+D or Alt+Delete deletes the next word, Ctrl+K deletes to line end and joins the next line when already at line end, Page Up/Page Down or mouse wheel scroll transcript history even when the pointer is over the composer, Ctrl+Z or Ctrl+- undoes the last composer edit, Ctrl+Y yanks the last killed composer text, Ctrl+C clears a non-empty composer draft and exits only when the draft is empty, Ctrl+D exits the current TUI loop when the composer is empty, Ctrl+L opens the model selector between turns, Ctrl+P cycles to the next configured model between turns, Shift+Ctrl+P cycles to the previous model when the terminal reports that enhanced key sequence, Shift+Tab or Ctrl+T cycles backend-declared reasoning levels for the active model when available, and Esc dismisses the active palette or starts the safe clear-input flow. While AVA is actively responding, Esc requests a cooperative stop and keeps the TUI open; stopped turns say to submit a new prompt to continue. During an active assistant or `/compact` run, typing a draft and pressing Enter queues a backend-owned follow-up turn. Typing `/steer your note` during an active run queues steering for the next safe provider boundary. Pending queued items render above the composer; `/restore` or Alt+Up restores the latest pending queued item to the draft before it starts. Stopping the active run skips pending queued items, and queue outcomes render as transcript audit entries with delivery guidance.
- Bracketed paste is enabled while the TUI is active. Pasted multi-line text is normalized into the draft instead of being treated as submitted commands, and tall drafts show a `draft +N above` indicator when earlier draft lines are hidden.

Set `NO_COLOR=1` to render the TUI without ANSI styling while preserving the same visible layout, modal content, and width bounds. `/settings` reports this as the plain `NO_COLOR` display mode. Full custom theme files are not implemented yet.

TUI keybindings are semantic and can be overridden with `$XDG_CONFIG_HOME/ava/keybinds.json`, for example:

```json
{
  "tui.input.submit": "Enter",
  "tui.input.newLine": ["Shift+Enter", "Ctrl+Enter", "Alt+Enter"],
  "autocomplete_accept": "Tab",
  "tui.editor.deleteCharBackward": ["Backspace", "Ctrl+H"],
  "tui.editor.cursorLeft": ["Left", "Ctrl+B", "Alt+H"],
  "tui.editor.cursorWordRight": ["Ctrl+Right", "Alt+Right", "Alt+F", "Alt+W"],
  "delete_to_line_start": "Ctrl+U",
  "undo": "Ctrl+Z",
  "yank": "Ctrl+Y",
  "model_select": "Ctrl+L",
  "model_cycle_forward": "Ctrl+P",
  "model_cycle_backward": "Shift+Ctrl+P",
  "message_dequeue": "Alt+Up",
  "tui.select.confirm": ["Enter", "Space"],
  "tui.select.cancel": ["Escape", "Ctrl+W"],
  "variant_cycle": ["Shift+Tab", "Ctrl+T"]
}
```

Use a string for one key, an array of strings for multiple keys, or the existing comma-separated string form for compatibility. Action keys may use AVA snake_case names or matching Pi-style namespaced ids such as `tui.editor.cursorLineStart`, `tui.input.submit`, `tui.select.confirm`, `app.tools.expand`, and `app.message.dequeue`; Pi ids without an AVA equivalent are rejected with an action-specific error. Named keys include `Space`, and unbound Space still inserts text normally. Select-list bindings under `tui.select.*` are modal-scoped, so `tui.select.confirm: "Space"` confirms highlighted rows without changing composer submission, and `tui.select.cancel: "Ctrl+W"` does not remove composer delete-word behavior. Vim-style `Alt+H/J/K/L` cursor aliases and `Alt+W` word-right are accepted for custom cursor bindings. When a legacy camelCase alias and a current AVA or namespaced key both target the same AVA action, the current key wins. Custom bindings may intentionally shadow default keys, but a `keybinds.json` file that assigns the same key to more than one configured action in the same context is rejected by the loader, rendered as a startup alert, and AVA starts with the default bindings. Run `/reload` or `/reload keybindings` in the TUI after editing `keybinds.json` to apply changes without restarting; reload errors stay visible and the previous active bindings remain in use. Current named actions include composer submission/editing, transcript scroll, palette navigation, select-list navigation/confirm/cancel/page, prompt placeholders, `mode_toggle`, `variant_cycle`, `interrupt`, and `exit`. `/help`, `/hotkeys`, and `/keybindings` show the effective bindings that are active in the TUI and point back to the config/reload workflow.

The slash palette opens above the composer while typing `/`. Use arrows to move focus, Tab or Enter to insert the selected command or argument suggestion, and Esc to dismiss without clearing the draft. The selected item is visually highlighted with a `›` marker. Disabled planned commands remain visible with a reason and are not submitted as model prompts. Argument suggestions are only shown from backend/session data sources, such as configured models, resumable sessions with names/labels, loaded context sources, configured MCP servers, plugin metadata, and bounded workspace path candidates for `/read`, `/write`, `/glob`, and `/grep` include globs. Slash path candidates are relative to the current directory, skip reference-code/build/cache-heavy folders, and keep directories open for nested completion. Large bracketed pastes collapse to a visible `[paste #N ...]` marker in the composer, behave as one editable unit for character/word cursor movement and deletion, and expand back to the original pasted text when submitted.

Typing `@` in normal prompt text opens file-reference suggestions for bounded workspace-relative paths. Suggestions use case-insensitive fuzzy matching, include paths with spaces as quoted `@"path with spaces"` tokens, and keep directories open for nested completion. When the prompt is submitted, AVA reads each referenced file through the permissioned read path and appends bounded file content to the user message sent to the provider.

Normal prompt text also opens path suggestions when the active token is path-like, such as `src/`, `./src`, or `"my folder/`. Pressing Tab on a bare token such as `main` forces path completion and either inserts the single match or opens a path palette for multiple matches. These completions only edit the draft text; use `@` when the file content should be attached to the provider prompt.

`/connect` opens a centered provider login modal. Type to search providers, use arrows to move selection, press Enter to confirm, then choose a login method. OpenAI shows ChatGPT Pro/Plus browser OAuth, headless OAuth, and API key options; OAuth waiting modals keep the URL and `C` copy shortcut visible, and browser OAuth opens the default browser automatically. `/login` is an alias.

## Permission Prompts

Interactive permission requests replace the composer with an approval dock. The dock shows the tool, command or path summary, risk label, reason, selected action, and key help. File mutation prompts show a backend-provided unified diff before approval when AVA can safely compute one; otherwise the prompt falls back to the conservative summary without inventing a diff in the TUI.

- `A`: allow once
- `D`: deny
- `R`: remember the selected allow or deny action as a persistent exact-match rule when available
- `Enter` or Space: confirm the selected action
- `Esc`: deny
- Tab, Left, or Right: move selection

`Deny` is the default focused action. `A` and `D` are one-shot decisions. When persistent rules are available, the dock also shows remembered allow/deny rule choices; `R` toggles the selected action between one-shot and remembered. A remembered TUI choice creates an exact workspace-scoped persistent rule for the prompt fields before resolving the current prompt. Permission decisions are recorded in the session audit trail and export when a session is active. Use `/permissions audit [query]` to inspect recent session decisions, and use `/permissions` to list, diagnose, explain, add, or remove persistent rules. RPC clients can create process-local exact-match session grants with `allow_session`; those grants are inspectable, revocable, clearable, and not persisted across restarts.

## Commands

- `/help`: show commands and hotkeys
- `/hotkeys` or `/keybindings`: show effective TUI keybindings
- `/reload [keybindings]`: reload TUI keybindings from `keybinds.json`
- `/mode`: toggle build/plan mode
- `/details` or Ctrl+O: toggle tool detail expansion in the TUI
- `/copy [tool|diff]`: copy the latest AVA message, latest tool-card details, or latest unified diff through the terminal clipboard path
- `/thinking`: toggle inline thinking block visibility in the TUI without changing provider reasoning mode
- `/connect`: open provider and login method modals; `/login` is an alias
- `/models [query|provider/model]`: list configured models and provider/model capabilities; `/model` is an alias. Exact `/model` or `/models` and Ctrl+L open the TUI model selector; Ctrl+P cycles to the next configured model between turns.
- `/sessions [--archived] [query|id]`: show the session tree for this workspace, optionally filtered by id, name, label, branch metadata, path, archive state, or timestamp; `/tree` is an alias
- `/sessions rename <id> <name|--clear>`: rename a session without switching to it
- `/sessions labels <id> <label...|--clear>`: set or clear labels on a session without switching to it
- `/sessions archive <id> --confirm` and `/sessions unarchive <id>`: hide or restore a non-current session without deleting its JSONL file
- `/fork [name]`: fork the current session at its latest entry and switch to the new branch
- `/clone [name]`: clone the full current session and switch to the copy
- `/new [name]`: start a fresh session and switch to it
- `/resume [id]`: resume/switch to an existing session by exact id or unique prefix; exact `/resume` opens the TUI session selector, where PageUp/PageDown page through rows, Ctrl+S or Ctrl+T cycles recent/name/path sort, Ctrl+N toggles named sessions only, Ctrl+P toggles path display, Ctrl+A shows/hides archived sessions, Ctrl+R restores a rename command, Ctrl+L restores a labels command, and Ctrl+D twice archives or restores the highlighted session
- `/name <name|--clear>`: set or clear the current session display name; `/rename` is an alias
- `/labels <label...|--clear>`: set or clear current session labels; `/label` is an alias. Labels are unique, non-empty strings separated by spaces.
- `/context [query|source]`: list loaded context sources, optionally filtered by path or source type
- `/compact [instructions]`: generate and record a provider summary
- `/export`: export this session as markdown
- `/stats`: show session counts, usage, cost, and resume/export hints; `/status` is an alias
- `/permissions <list|audit|diagnose|explain|add|remove> ...`: inspect session permission audits and manage persistent permission rules; `/permission-rules` and `/perms` are aliases
- `/read <path>`: read a file
- `/write <path> <text>`: write a file through permission checks using atomic replacement where practical
- `/glob <pattern>`: list readable matching files
- `/grep <text> [glob]`: search readable files for literal text
- `/bash <command>`: run a conservative permissioned command
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
ava --print "summarize this repo"
printf 'summarize this repo\n' | ava --print
ava --print "summarize this repo" --json
```

Text print mode writes final assistant text to stdout and diagnostics to stderr. JSON print mode writes newline-delimited runtime events to stdout. Prompt turns require configured auth for the active provider.

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
```

See `docs/headless-protocol.md` for the complete stdout/stderr contract, event types, RPC commands, and exit-code behavior. RPC `list_commands` and `invoke_command` expose project/global prompt commands, skills, plugin commands, and MCP prompt commands through the shared command registry.

## Current Limits

- Built-in providers are OpenAI, Anthropic, Kimi, Moonshot, and OpenRouter. OpenAI remains the default production path. Anthropic uses native Messages API support for tools and reasoning. Kimi, Moonshot, and OpenRouter use OpenAI-compatible shims with built-in profile tests; live credentialed smokes are still release-validation work.
- Built-in model-visible tools are `read_file`, `list_directory`, `write_file`, `edit_file`, `apply_patch`, `glob`, `grep`, `bash`, `webfetch`, `websearch`, `skill`, `question`, and capability-gated LSP tools for diagnostics, document symbols, workspace symbols, definitions, and references.
- Local plugin and MCP foundations exist for command discovery, plugin diagnostics, plugin prompt/skill resources and commands, MCP stdio tool discovery/calls, MCP read-style resources, and MCP prompts through command-registry entries. The stable local plugin authoring guide lives in `docs/plugin-system.md`, the compatibility policy lives in `docs/plugin-compatibility-policy.md`, and a minimal sample lives under `examples/plugins/todo/`; representative golden/audit/failure contract tests now cover the v1 foundation.
- The HTTP transport uses the local `curl` executable.
- The TUI now renders assistant text, inline thinking blocks, and tool lifecycle updates live. Tool cards show backend-provided truncation, spill, and diff metadata when those fields are present; the live runtime event path still has summary-only tool results for some backend producers.
- Permission `ask` decisions open a TUI prompt in interactive mode and fail closed in non-TTY/headless mode unless a supported read/search allow policy is supplied or an RPC client answers `permission_requested` with `permission_reply`. Workspace-policy `allow` decisions can still proceed without a resolver, while policy `deny` decisions are never upgraded by headless flags. File mutation asks include `diff_preview` and `diff_truncated` when the backend can safely provide a unified diff. Successful RPC permission/question replies emit `permission_replied` or `question_replied` events before their in-band response; permission replies may include a bounded `reason` field to explain the resolution, and denial reasons are preserved in tool errors and permission audits. Session permission decisions can be inspected with `/permissions audit [query]`. Persistent permission rules are managed with `/permissions list`, `/permissions diagnose`, `/permissions explain <rule_id>`, `/permissions add ...`, and `/permissions remove <rule_id>`; the TUI command and remembered prompt choices use the same protected rule files as RPC rule commands. Matching persistent rules are checked before interactive TUI prompts. RPC `allow_session` replies create process-local exact-match grants that can be listed with `permission_grants`, revoked with `permission_grant_revoke`, or cleared with `permission_grants_clear`.
- Shared runtime events now include backend-owned compaction, bounded provider/compaction retry, retry countdown ticks, cancel-request, and terminal canceled markers. The TUI renders those as audit/status items instead of inferring lifecycle state from command text; retry and compaction markers show attempt totals, retry delays, and countdown remaining time when the backend emits them.
- Interactive/TUI mode supports local modals for provider login plus question prompts with single-select, multi-select, custom text, and cancel handling. RPC clients can answer `question_requested` with `question_reply` using `answer`, `selected`, or `selected_options` for multi-select prompts.
