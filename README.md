# AVA

AVA is a native C++23 agentic coding tool. This branch is a ground-up implementation with 0.1 complete, 0.2 focused on TUI/tool hardening, and 0.32 shipping the interactive TUI on an `ncursesw` runtime: one terminal binary, OpenAI first, safe built-in tools, build/plan modes, permission prompts, tool visibility, and append-only JSONL sessions.

## Build

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Sanitizer build:

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

## Run

```sh
./build/ava
./build/ava connect openai
./build/ava --mode plan
./build/ava --continue
./build/ava --session <id-or-prefix>
```

When stdin/stdout are not a terminal, AVA falls back to a line-oriented shell for scripting:

```sh
printf '/glob **/*.cpp\n/quit\n' | ./build/ava --continue
```

## Configuration

AVA follows XDG paths on Linux:

- Config: `$XDG_CONFIG_HOME/ava/`, fallback `~/.config/ava/`
- Auth: `$XDG_CONFIG_HOME/ava/auth.json`, fallback `~/.config/ava/auth.json`
- Sessions: `$XDG_STATE_HOME/ava/sessions/`, fallback `~/.local/state/ava/sessions/`

OpenAI auth can be created with `ava connect openai`, which runs a local OAuth callback on `http://localhost:1455/auth/callback` and stores the resulting token owner-only. Auth files also support OAuth-style tokens and API keys:

```json
{"openai":{"type":"oauth","access_token":"...","refresh_token":"...","expires_at":1893456000}}
```

```json
{"openai":{"type":"api_key","api_key":"sk-..."}}
```

The built-in default is `openai/gpt-5.5`. Override models with `$XDG_CONFIG_HOME/ava/models.json`, and prompts with `$XDG_CONFIG_HOME/ava/prompts/<provider>/<family>/<mode>.txt`.

## Interactive Commands

- `/help`: show commands
- `/mode`: toggle build/plan mode
- `/sessions`: list resumable sessions for the current workspace
- `/read <path>`: read a file through permissions
- `/write <path> <text>`: write a file through permissions
- `/glob <pattern>`: list readable matching files
- `/grep <text> [glob]`: literal text search
- `/bash <command>`: run an argv-style permissioned command
- `/quit`: exit and print a resume command

## 0.2 Notes

- Real OpenAI requests use the local `curl` executable as the HTTP transport with a sanitized execution path.
- Tool calling is implemented through the provider contract and the built-in dispatcher.
- `apply_patch` currently supports up to 32 exact text replacements through an `edits` array.
- `question` is exposed as a tool, but AVA does not yet have a modal user-question workflow; the assistant is instructed to ask directly.
- Interactive TUI permission prompts exist for backend `ask` decisions; non-TTY mode still fails closed.
- Deferred: multiple providers, plugins, MCP, full session tree UI, compaction, LSP, web fetch, persistent permission rules, and a modal user-question workflow.

## 0.32 TUI Notes

- The interactive TUI now enters a wide-character ncurses (`ncursesw`) session for terminal mode, input, resize, mouse, and screen drawing.
- The visible layout remains composer-first: compact identity strip, role-aware transcript lines, compact tool cards, and a bottom-pinned AVA-style composer with the elevated surface, blue rail, and `❯` prompt.
- The slash palette opens above the composer with command metadata, keyboard focus cues, and narrow-terminal fallback.
- Permission requests replace the composer with an approval dock. `Deny` stays the default focus; `A` allows once and `D` denies.
- Non-TTY stdin/stdout still use the line shell fallback for scripts and tests.
- 0.32 does not add providers, persistent permission rules, session-wide allows, streaming, MCP, plugins, or a session tree UI.

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
