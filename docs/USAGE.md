# AVA Usage

## Starting AVA

```sh
ava
ava auth login
ava login anthropic
ava auth login moonshot --api-key
ava connect openai
ava connect kimi --oauth-token
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

Press Tab in the TUI or use `/mode` to switch. If the slash palette is open, Tab accepts the highlighted command instead.

## TUI Layout

The interactive TUI runs on wide-character ncurses (`ncursesw`) for terminal mode, keyboard/mouse input, resize handling, and screen drawing. The layout is composer-first:

- The top strip shows AVA identity, mode, provider, model, and basic help.
- The transcript renders user, assistant, inline thinking, tool, audit, and error entries with distinct text markers.
- Tool activity appears as compact lifecycle cards for provider announcements, streamed arguments, execution, progress, completion, and errors. Tool details can expand, and backend-provided truncation/omission counts, spill paths, and unified diffs are rendered when present.
- On wide terminals, the sidebar shows backend-owned activity, modified files, cwd, branch, session, provider/model, reasoning status when available, usage when available, and loaded context source count.
- The bottom composer is fixed to the bottom rows and uses the old AVA visual language: elevated `#1A1F2E` surface, primary-blue left rail, `❯` prompt glyph, mode badge, provider/model metadata, token metadata slot, and a spinner-only working indicator.
- Enter submits, Shift+Enter inserts a newline, Page Up/Page Down or mouse wheel scroll transcript history even when the pointer is over the composer, Ctrl+Z undoes the last composer edit, Ctrl+Y yanks the last killed composer text, Ctrl+C clears a non-empty composer draft and exits only when the draft is empty, Ctrl+D exits the current TUI loop, Ctrl+T cycles backend-declared reasoning levels for the active model when available, and Esc dismisses the active palette or starts the safe clear-input flow. While AVA is actively responding, Esc requests a cooperative stop and keeps the TUI open. During an active assistant or `/compact` run, typing a draft and pressing Enter queues a backend-owned follow-up turn. Typing `/steer your note` during an active run queues steering for the next safe provider boundary. Pending queued items render above the composer; `/restore` restores the latest pending queued item to the draft before it starts. Queue outcomes render as transcript audit entries.
- Bracketed paste is enabled while the TUI is active. Pasted multi-line text is normalized into the draft instead of being treated as submitted commands, and tall drafts show a `draft +N above` indicator when earlier draft lines are hidden.

TUI keybindings are semantic and can be overridden with `$XDG_CONFIG_HOME/ava/keybinds.json`, for example:

```json
{
  "submit": "Enter",
  "new_line": "Shift+Enter",
  "autocomplete_accept": "Tab",
  "delete_to_line_start": "Ctrl+U",
  "undo": "Ctrl+Z",
  "yank": "Ctrl+Y",
  "variant_cycle": "Ctrl+T"
}
```

Use comma-separated strings for multiple keys. Current named actions include composer submission/editing, transcript scroll, palette navigation, prompt placeholders, `mode_toggle`, `variant_cycle`, `interrupt`, and `exit`. `/help` and `/hotkeys` show the effective bindings that are active in the TUI.

The slash palette opens above the composer while typing `/`. Use arrows to move focus, Tab or Enter to insert the selected command or argument suggestion, and Esc to dismiss without clearing the draft. The selected item is visually highlighted with a `›` marker. Disabled planned commands remain visible with a reason and are not submitted as model prompts. Argument suggestions are only shown from backend/session data sources, such as configured models, resumable sessions, loaded context sources, configured MCP servers, and plugin metadata.

`/connect` opens a centered provider-credential modal. Type to search providers, use arrows to move selection, press Enter to confirm, then choose API key or OAuth bearer token and paste the secret. `/login` is an alias.

## Permission Prompts

Interactive permission requests replace the composer with an approval dock. The dock shows the tool, command or path summary, selected action, and key help. File mutation prompts show a backend-provided unified diff before approval when AVA can safely compute one; otherwise the prompt falls back to the conservative summary without inventing a diff in the TUI.

- `A`: allow once
- `D`: deny
- `Enter` or Space: confirm the selected action
- `Esc`: deny
- Tab, Left, or Right: move selection

`Deny` is the default focused action. Permission decisions are recorded in the session audit trail and export when a session is active, but prompts do not create persistent allow/deny rules. RPC clients can create process-local exact-match session grants with `allow_session`; those grants are inspectable, revocable, clearable, and not persisted across restarts.

## Commands

- `/help`: show commands and hotkeys
- `/hotkeys`: show effective TUI hotkeys
- `/mode`: toggle build/plan mode
- `/details`: toggle tool detail expansion in the TUI
- `/thinking`: toggle inline thinking block visibility in the TUI without changing provider reasoning mode
- `/connect [provider] [api-key|oauth]`: open a provider login modal and store an API key or OAuth bearer token; `/login` is an alias
- `/models [query|provider/model]`: list configured models and provider/model capabilities; `/model` is an alias
- `/sessions [query|id]`: list sessions for this workspace, optionally filtered by session id or timestamp
- `/context [query|source]`: list loaded context sources, optionally filtered by path or source type
- `/compact [instructions]`: generate and record a provider summary
- `/export`: export this session as markdown
- `/stats`: show session counts, usage, cost, and resume/export hints; `/status` is an alias
- `/read <path>`: read a file
- `/write <path> <text>`: write a file through permission checks using atomic replacement where practical
- `/glob <pattern>`: list readable matching files
- `/grep <text> [glob]`: search readable files for literal text
- `/bash <command>`: run a conservative permissioned command
- `/quit`: exit

Planned but unavailable commands such as `/import`, `/new`, `/resume`, `/reload`, and `/logout` are recognized and return a disabled explanation instead of being sent to the model.

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

Text print mode writes final assistant text to stdout and diagnostics to stderr. JSON print mode writes newline-delimited runtime events to stdout. Prompt turns require configured OpenAI auth.

Headless modes are fail-closed for permission prompts unless an explicit supported read/search policy is supplied. Network access is separate: `--allow read-only` does not allow network prompts, while `--allow-tool webfetch` can auto-allow exact `webfetch` `network.fetch` prompts. In RPC mode, matching prompts are auto-allowed before `permission_requested`; non-matching asks still require `permission_reply`:

```sh
ava --print "inspect this project" --allow read-only
ava --print "read the README" --allow-tool read_file
ava --print "fetch release notes" --allow-tool webfetch
ava --rpc --allow read-only
```

Use RPC mode for a long-lived JSONL stdio automation client:

```sh
printf '%s\n' '{"id":"state","type":"get_state"}' | ava --rpc
```

See `docs/headless-protocol.md` for the complete stdout/stderr contract, event types, RPC commands, and exit-code behavior.

## Current Limits

- Built-in providers are OpenAI and Anthropic. Additional provider ids can store credentials for configured/provider-compatible model entries, but broader provider shims are still in progress.
- The HTTP transport uses the local `curl` executable.
- The TUI now renders assistant text, inline thinking blocks, and tool lifecycle updates live. Tool cards show backend-provided truncation, spill, and diff metadata when those fields are present; the live runtime event path still has summary-only tool results for some backend producers.
- Permission `ask` decisions open a TUI prompt in interactive mode and fail closed in non-TTY/headless mode unless a supported read/search allow policy is supplied or an RPC client answers `permission_requested` with `permission_reply`. File mutation asks include `diff_preview` and `diff_truncated` when the backend can safely provide a unified diff. Successful RPC permission/question replies emit `permission_replied` or `question_replied` events before their in-band response; permission replies may include a bounded `reason` field to explain the resolution, and denial reasons are preserved in tool errors and permission audits. RPC `allow_session` replies create process-local exact-match grants that can be listed with `permission_grants`, revoked with `permission_grant_revoke`, or cleared with `permission_grants_clear`.
- Shared runtime events now include backend-owned compaction, bounded provider/compaction retry, retry countdown ticks, cancel-request, and terminal canceled markers. The TUI renders those as audit/status items instead of inferring lifecycle state from command text; retry and compaction markers show attempt totals, retry delays, and countdown remaining time when the backend emits them.
- Interactive/TUI mode supports local modals for provider login plus question prompts with single-select, multi-select, custom text, and cancel handling. RPC clients can answer `question_requested` with `question_reply` using either `answer` or `selected`.
