# AVA

[![CI](https://github.com/Artificial-Source/AVA/actions/workflows/ci.yml/badge.svg?branch=develop)](https://github.com/Artificial-Source/AVA/actions/workflows/ci.yml)

AVA is a native C++23 agentic coding tool. The active default branch is `develop`; historical branches are kept under `archive/*`. The shipped 1.0 backend MVP includes OpenAI and Kimi-for-coding live-verified provider paths, safe built-in tools, build/plan modes, permission prompts, tool visibility, append-only JSONL sessions, headless print/RPC modes, local plugin/MCP foundations, and an interactive TUI backed by wide-character ncurses (`ncursesw`). Backend release-position docs moved through the 0.60 platform catch-up, 0.65 provider-native hardening, bundled 0.70 reasoning/model lifecycle closeout, 0.75 extension foundation, 0.80 extension stabilization, and 0.90 release-candidate verification before this `1.0.0` runtime bump.

## Build

Dependencies:

- CMake 3.25+
- C++23 compiler
- Boost development headers and CMake package
- `ncursesw` development headers/library
- `curl` executable for provider HTTP transport

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

**For detailed cmake configuration options and build instructions see [CONTRIBUTING](CONTRIBUTING.md).**

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

OpenAI auth can be created with `ava connect openai`, which opens a login picker for ChatGPT Pro/Plus browser OAuth, ChatGPT Pro/Plus headless device OAuth, or an OpenAI API key. Browser OAuth opens the default browser and listens on `http://localhost:1455/auth/callback`; headless OAuth prints `https://auth.openai.com/codex/device` plus a user code. OAuth credentials are refreshed automatically before use when a refresh token is available. Auth files also support OAuth-style tokens and API keys:

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
- `/details` or Ctrl+O: toggle TUI tool detail expansion
- `/copy [tool|diff]`: copy the latest AVA message, latest tool-card details, or latest unified diff in the TUI
- `/thinking`: toggle inline thinking block visibility without changing provider reasoning mode
- `/connect`: open provider and login method modals; `/login` is an alias
- `/models [query|provider/model]`: list configured models and capabilities; `/model` is an alias, Ctrl+L opens the TUI model selector, and Ctrl+P cycles to the next configured model between turns
- `/sessions [--archived] [query|id]`, `/sessions rename <id> <name|--clear>`, `/sessions labels <id> <label...|--clear>`, `/sessions archive <id> --confirm`, or `/sessions unarchive <id>`: show the resumable session tree, rename/label sessions, or hide/restore sessions without deleting their JSONL files; `/tree` is an alias for the tree view
- `/fork [name]`: fork the current session at its latest entry and switch to the branch
- `/clone [name]`: clone the full current session and switch to the copy
- `/new [name]`: start a fresh session and switch to it
- `/resume [id]`: resume/switch to an existing session by exact id or unique prefix; exact `/resume` opens the TUI session selector, where PageUp/PageDown page through rows, Ctrl+S or Ctrl+T cycles recent/name/path sort, Ctrl+N toggles named sessions only, Ctrl+P toggles path display, Ctrl+A shows/hides archived sessions, Ctrl+R restores a rename command, Ctrl+L restores a labels command, and Ctrl+D twice archives or restores the highlighted session
- `/name <name|--clear>`: set or clear the current session display name; `/rename` is an alias
- `/labels <label...|--clear>`: set or clear current session labels; `/label` is an alias
- `/context [query|source]`: list loaded context sources
- `/compact [instructions]`: generate and record a provider summary
- `/export`: export this session as markdown
- `/stats`: show session counts, usage, cost, and resume/export hints; `/status` is an alias
- `/permissions <list|audit|diagnose|explain|add|remove> ...`: inspect session permission audits and manage persistent permission rules; `/permission-rules` and `/perms` are aliases
- `/read <path>`: read a file through permissions
- `/write <path> <text>`: write a file through permissions using atomic replacement where practical
- `/glob <pattern>`: list readable matching files
- `/grep <text> [glob]`: literal text search
- `/bash <command>`: run an argv-style permissioned command
- `/plugins list|inspect|enable|disable|validate|failures|prompts|prompt|skills|skill`: inspect and manage local plugins
- `/plugin run <id> <command> [arguments_json]`: run a plugin command through the permissioned extension path
- `/mcp list|inspect|tools|restart`: inspect configured MCP servers and discover tools
- `/quit`: exit and print a resume command

## 0.2 Notes

- Real OpenAI requests use the local `curl` executable as the HTTP transport with a sanitized execution path.
- Tool calling is implemented through the provider contract and the built-in dispatcher.
- `apply_patch` currently supports up to 32 exact text replacements through an `edits` array.
- `question` opens an interactive TUI modal with single-select, multi-select, custom-answer, secret-entry, and cancel handling. Headless RPC clients can answer question requests through the protocol.
- Interactive TUI permission prompts exist for backend `ask` decisions; file mutation asks show backend-provided unified diffs when available. TUI prompts support one-shot allow/deny plus remembered exact allow/deny rule choices backed by the protected persistent-rule store. Permission decisions are persisted in session audit entries and can be inspected with `/permissions audit`, while non-TTY mode still fails closed unless an explicit headless allow policy is supplied or RPC replies are provided.
- Historical 0.2 deferrals have mostly moved into the backend: multiple providers, plugins, MCP, LSP, persistent permission rules, interactive rule-management commands, and session tree/fork/clone RPC contracts now exist. Remaining follow-up work is product polish such as deeper audit navigation, provider-generated branch summaries, automatic LSP recipes, and full diff navigation.

## 0.32 TUI Notes

- The interactive TUI now enters a wide-character ncurses (`ncursesw`) session for terminal mode, input, resize, mouse, and screen drawing.
- The visible layout remains composer-first: compact identity strip, role-aware transcript lines, compact tool cards, and a bottom-pinned AVA-style composer with the elevated surface, blue rail, and `❯` prompt.
- The composer is intentionally quiet: no persistent keybinding help or transcript status line is rendered in the input area.
- Shift+Enter, Ctrl+Enter, or Alt+Enter inserts a newline. Arrow Up/Down move inside multiline drafts and recall prompt history at the draft boundary before falling back to transcript scroll. Home/Ctrl+A and End/Ctrl+E move to the current line boundary. Ctrl+Left/Right, Alt+Left/Right, or Alt+B/F move by word. Ctrl+] jumps forward to the next typed character, Ctrl+Alt+] jumps backward, Delete or Ctrl+D deletes the character after the cursor while the draft has text, Ctrl+W or Alt+Backspace deletes the previous word, Alt+D or Alt+Delete deletes the next word, Ctrl+K deletes to line end and joins the next line when already at line end, Ctrl+Z or Ctrl+- undoes the last edit, Ctrl+L opens the model selector between turns, Ctrl+P cycles to the next configured model between turns, and Ctrl+D exits when the composer is empty.
- The slash palette opens above the composer with command metadata, keyboard focus cues, and narrow-terminal fallback.
- During an active assistant or `/compact` run, Enter on a draft queues a backend-owned follow-up turn. `/steer ...` queues steering for the next safe provider boundary. Pending queued items render above the composer, and `/restore` or Alt+Up restores the latest pending queued item to the draft before it starts. Stopped turns say to submit a new prompt to continue; pending queued items skipped by stop/finish render as transcript audit entries with delivery guidance.
- Permission requests replace the composer with an approval dock. `Deny` stays the default focus; `A` allows once, `D` denies, and `R` toggles a remembered allow/deny rule choice when persistent rules are available. Mutation prompts render backend-provided diffs before approval when AVA can safely compute them.
- Non-TTY stdin/stdout still use the line shell fallback for scripts and tests.
- Later frontend work added live assistant/tool lifecycle updates, inline thinking visibility, and backend-provided tool detail/diff rendering in the TUI. 0.32 did not add providers, persistent permission rules, session-wide allows, MCP, plugins, or a session tree UI.

## 0.60 Backend Notes

- 0.60 is the backend platform catch-up line after the 0.32 ncursesw TUI baseline and the oversized 0.33 maturity ledger.
- The milestone position is Phases 0-4 complete for the approved backend scope, Phase 5 foundation complete, 0.65/0.70 provider and reasoning work covered by offline/fake validation with live smokes deferred, and 0.75 plugin/MCP foundations implemented but not 1.0-stable.
- Notable backend slices include semantic runtime events and command results, frontend-owned content adaptation, smaller helper modules, stronger headless CLI/RPC coverage, provider/model registry foundations, session/compaction/usage hardening, and line-first tool output windows.
- The command-registry foundation now exposes project/global prompt commands, skill prompts, plugin command contributions, and MCP prompts through one discoverable backend/RPC command surface.
- Use `docs/versions/0.60.md` for the platform catch-up position, `docs/versions/0.65.md` and `docs/versions/0.70.md` for provider/reasoning closeout status, `docs/versions/0.75.md` for the implemented extension foundation line, `docs/versions/0.80.md` for extension stabilization, `docs/versions/0.90.md` for release-candidate evidence, `docs/versions/1.0.md` for the shipped backend MVP, `docs/versions/0.33.md` for detailed slice evidence, and `docs/product/backend-capabilities-1.0.md` for the 1.0 backend capability checklist.

## 0.65 Backend Notes

- 0.65 is the provider-native hardening line after the 0.60 platform catch-up.
- That slice keeps OpenAI as the default production path, hardens Anthropic native Messages support with offline/fake coverage, and adds Kimi/Moonshot/OpenRouter-compatible contract coverage for request shape, reasoning content, usage/error parsing, auth/header/base URL behavior, and context-overflow classification.
- The 0.70 reasoning/model lifecycle closeout is bundled into this work for protocol docs, focused tests, and reasoning-change export polish.
- Live credentialed provider smokes are still release-validation evidence and are deferred unless credentials are available.

## Planning Docs

- `docs/versions/0.1.md`
- `docs/versions/0.2.md`
- `docs/versions/0.21.md`
- `docs/versions/0.32.md`
- `docs/versions/0.33.md`
- `docs/versions/0.60.md`
- `docs/versions/0.65.md`
- `docs/versions/0.70.md`
- `docs/versions/0.75.md`
- `docs/versions/0.80.md`
- `docs/versions/0.90.md`
- `docs/versions/1.0.md`
- `docs/CONFIG.md`
- `docs/USAGE.md`
- `docs/TESTING.md`
- `docs/CONTRIBUTING.md`
- `docs/roadmap/backend.md`
- `docs/product/backend-capabilities-1.0.md`
- `docs/product/mvp-baseline.md`
- `docs/product/product-plan.md`
- `docs/product/tooling-plan.md`
- `docs/product/architecture-plan.md`
- `docs/engineering/cpp-safety-rules.md`
