# AVA

[![CI](https://github.com/Artificial-Source/AVA/actions/workflows/ci.yml/badge.svg?branch=develop)](https://github.com/Artificial-Source/AVA/actions/workflows/ci.yml)

AVA is a native C++23 agentic coding tool. The active default branch is `develop`; historical branches are kept under `archive/*`. The current 0.32 line ships a focused terminal binary with OpenAI-first auth, safe built-in tools, build/plan modes, permission prompts, tool visibility, append-only JSONL sessions, headless print/RPC modes, and an interactive TUI backed by wide-character ncurses (`ncursesw`).

## Build

Dependencies:

- CMake 3.25+
- C++23 compiler
- `ncursesw` development headers/library
- `curl` executable for OpenAI HTTP transport

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Equivalent CMake presets are available for local development:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Sanitizer build:

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Or with presets:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

GitHub Actions runs both the normal and sanitizer test jobs on pushes and pull requests targeting `develop`. Dependabot is enabled for GitHub Actions updates on `develop`.

## Run

```sh
./build/ava
./build/ava connect openai
./build/ava --mode plan
./build/ava --continue
./build/ava --session <id-or-prefix>
./build/ava --print "summarize this repo"
./build/ava --rpc
```

`--print` runs one prompt and exits. Add `--json` or `--output json` to emit runtime events instead of final text only. `--rpc` starts the JSONL stdio RPC MVP for automation clients; see `docs/headless-protocol.md` for request and event shapes.

When stdin/stdout are not a terminal and no headless mode is selected, AVA falls back to a line-oriented shell for scripting:

```sh
printf '/glob **/*.cpp\n/quit\n' | ./build/ava --continue
```

## Configuration

AVA follows XDG paths on Linux:

- Config: `$XDG_CONFIG_HOME/ava/`, fallback `~/.config/ava/`
- Auth: `$XDG_CONFIG_HOME/ava/auth.json`, fallback `~/.config/ava/auth.json`
- Sessions: `$XDG_STATE_HOME/ava/sessions/`, fallback `~/.local/state/ava/sessions/`

OpenAI auth can be created with `ava connect openai`, which runs a local OAuth callback on `http://localhost:1455/auth/callback` and stores the resulting token owner-only. OAuth credentials are refreshed automatically before use when a refresh token is available. Auth files also support OAuth-style tokens and API keys:

```json
{"openai":{"type":"oauth","access_token":"...","refresh_token":"...","expires_at":1893456000}}
```

```json
{"openai":{"type":"api_key","api_key":"sk-..."}}
```

The built-in default is `openai/gpt-5.5`. Override models with `$XDG_CONFIG_HOME/ava/models.json`, and prompts with `$XDG_CONFIG_HOME/ava/prompts/<provider>/<family>/<mode>.txt`.

## Interactive Commands

- `/help`: show commands and hotkeys
- `/hotkeys`: show effective TUI hotkeys
- `/mode`: toggle build/plan mode
- `/details`: toggle TUI tool detail expansion
- `/thinking`: toggle inline thinking block visibility without changing provider reasoning mode
- `/connect [provider] [api-key|oauth]`: store a provider credential; `/login` is an alias
- `/models [query|provider/model]`: list configured models and capabilities; `/model` is an alias
- `/sessions [query|id]`: list resumable sessions for the current workspace
- `/context [query|source]`: list loaded context sources
- `/compact [instructions]`: generate and record a provider summary
- `/export`: export this session as markdown
- `/stats`: show session counts, usage, cost, and resume/export hints; `/status` is an alias
- `/read <path>`: read a file through permissions
- `/write <path> <text>`: write a file through permissions using atomic replacement where practical
- `/glob <pattern>`: list readable matching files
- `/grep <text> [glob]`: literal text search
- `/bash <command>`: run an argv-style permissioned command
- `/quit`: exit and print a resume command

## 0.2 Notes

- Real OpenAI requests use the local `curl` executable as the HTTP transport with a sanitized execution path.
- Tool calling is implemented through the provider contract and the built-in dispatcher.
- `apply_patch` currently supports up to 32 exact text replacements through an `edits` array.
- `question` opens an interactive TUI modal with single-select, multi-select, custom-answer, secret-entry, and cancel handling. Headless RPC clients can answer question requests through the protocol.
- Interactive TUI permission prompts exist for backend `ask` decisions; file mutation asks show backend-provided unified diffs when available. Permission decisions are persisted in session audit entries, while non-TTY mode still fails closed unless an explicit headless allow policy is supplied or RPC replies are provided.
- Deferred: multiple fully selectable providers, plugins, MCP, full session tree UI, LSP, persistent permission rules, and full diff navigation.

## 0.32 TUI Notes

- The interactive TUI now enters a wide-character ncurses (`ncursesw`) session for terminal mode, input, resize, mouse, and screen drawing.
- The visible layout remains composer-first: compact identity strip, role-aware transcript lines, compact tool cards, and a bottom-pinned AVA-style composer with the elevated surface, blue rail, and `❯` prompt.
- The composer is intentionally quiet: no persistent keybinding help or transcript status line is rendered in the input area.
- The slash palette opens above the composer with command metadata, keyboard focus cues, and narrow-terminal fallback.
- During an active assistant or `/compact` run, Enter on a draft queues a backend-owned follow-up turn. `/steer ...` queues steering for the next safe provider boundary. Pending queued items render above the composer, and `/restore` restores the latest pending queued item to the draft before it starts. Queue lifecycle events render as transcript audit entries.
- Permission requests replace the composer with an approval dock. `Deny` stays the default focus; `A` allows once and `D` denies. Mutation prompts render backend-provided diffs before approval when AVA can safely compute them.
- Non-TTY stdin/stdout still use the line shell fallback for scripts and tests.
- Later frontend work added live assistant/tool lifecycle updates, inline thinking visibility, and backend-provided tool detail/diff rendering in the TUI. 0.32 did not add providers, persistent permission rules, session-wide allows, MCP, plugins, or a session tree UI.

## Planning Docs

- `docs/versions/0.1.md`
- `docs/versions/0.2.md`
- `docs/versions/0.21.md`
- `docs/versions/0.32.md`
- `docs/CONFIG.md`
- `docs/USAGE.md`
- `docs/TESTING.md`
- `docs/CONTRIBUTING.md`
- `docs/product/product-plan.md`
- `docs/product/tooling-plan.md`
- `docs/product/architecture-plan.md`
- `docs/engineering/cpp-safety-rules.md`
