# AVA Usage

## Starting AVA

```sh
ava
ava connect openai
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

Press Tab in the TUI or use `/mode` to switch.

## TUI Layout

The interactive TUI runs on wide-character ncurses (`ncursesw`) for terminal mode, keyboard/mouse input, resize handling, and screen drawing. The layout is composer-first:

- The top strip shows AVA identity, mode, provider, model, and basic help.
- The transcript renders user, assistant, tool, and error entries with distinct text markers.
- Tool activity appears as compact cards with running, success, and error status markers.
- The bottom composer is fixed to the bottom rows and uses the old AVA visual language: elevated `#1A1F2E` surface, primary-blue left rail, `❯` prompt glyph, mode badge, provider/model metadata, and status text.
- Enter submits, Ctrl-J/Shift+Enter inserts a newline, Page Up/Page Down or mouse wheel scroll transcript history, and Esc twice clears composer input.

The slash palette opens above the composer while typing `/`. Use arrows or Tab to move focus, then Enter to insert the selected command. The selected item has a `>` marker and readable `selected` text when width permits.

## Permission Prompts

Interactive permission requests replace the composer with an approval dock. The dock shows the tool, command or path summary, selected action, and key help.

- `A`: allow once
- `D`: deny
- `Enter` or Space: confirm the selected action
- `Esc`: deny
- Tab, Left, or Right: move selection

`Deny` is the default focused action. Permission decisions are recorded in the session audit trail and export when a session is active, but prompts do not create persistent allow/deny rules or session-wide grants.

## Commands

- `/help`: show commands
- `/mode`: toggle build/plan mode
- `/sessions`: list sessions for this workspace
- `/read <path>`: read a file
- `/write <path> <text>`: write a file through permission checks using atomic replacement where practical
- `/glob <pattern>`: list readable matching files
- `/grep <text> [glob]`: search readable files for literal text
- `/bash <command>`: run a conservative permissioned command
- `/quit`: exit

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

Headless modes are fail-closed for permission prompts unless an explicit supported read/search policy is supplied. In RPC mode, matching prompts are auto-allowed before `permission_requested`; non-matching asks still require `permission_reply`:

```sh
ava --print "inspect this project" --allow read-only
ava --print "read the README" --allow-tool read_file
ava --rpc --allow read-only
```

Use RPC mode for a long-lived JSONL stdio automation client:

```sh
printf '%s\n' '{"id":"state","type":"get_state"}' | ava --rpc
```

See `docs/headless-protocol.md` for the complete stdout/stderr contract, event types, RPC commands, and exit-code behavior.

## Current Limits

- OpenAI is the only provider.
- The HTTP transport uses the local `curl` executable.
- Tool results are returned after the provider turn completes; there is no async streaming UI yet.
- Permission `ask` decisions open a TUI prompt in interactive mode and fail closed in non-TTY/headless mode unless a supported read/search allow policy is supplied or an RPC client answers `permission_requested` with `permission_reply`.
- In interactive/TUI mode, `question` does not open a modal yet; the assistant should ask the user directly. In RPC mode, clients can answer `question_requested` with `question_reply` using either `answer` or `selected`.
