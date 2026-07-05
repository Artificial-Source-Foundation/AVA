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

The optional Qt Quick desktop prototype additionally requires Qt 6.5+ with QML, Quick, and Quick Controls 2. See `docs/desktop-qml.md`.

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
./build/ava --resume
./build/ava --session <id-or-prefix>
./build/ava --session-id <id-or-prefix>
./build/ava "summarize this repo"
./build/ava @README.md "summarize this file"
./build/ava --print "summarize this repo"
./build/ava --tools read_file,grep "inspect safely"
./build/ava --tools read,grep,find,ls "inspect with Pi-style names"
./build/ava --rpc
```

Bare prompt text and `--print` both run one prompt and exit. Multiple positional prompt tokens are joined with spaces, so `ava summarize this repo` is equivalent to `ava "summarize this repo"`. CLI `@path` text file arguments are expanded through the same bounded permissioned file-reference path used by the TUI, including quoted shell arguments for paths with spaces. Add `--json`, `--output json`, or Pi-compatible `--mode json` to emit runtime events instead of final text only. `--tools`, `--exclude-tools`, `--no-builtin-tools`, and `--no-tools` control model-visible tools for TUI, print, and RPC sessions; AVA accepts native names plus Pi aliases such as `read`, `write`, `edit`, `find`, and `ls`, but still advertises one native provider schema per operation. These flags are separate from `--allow-tool`, which only controls permission auto-approval. `--rpc`, `--output rpc`, or Pi-compatible `--mode rpc` starts the JSONL stdio RPC MVP for automation clients; RPC prompt requests can import local image paths through `attachments:["path.png"]` or Pi-style inline uploads through `images:[{"type":"image","data":"...","mimeType":"image/png"}]` for image-capable models. See `docs/headless-protocol.md` for request and event shapes.

When stdin/stdout are not a terminal and no headless mode is selected, AVA falls back to a line-oriented shell for scripting:

```sh
printf '/glob **/*.cpp\n/quit\n' | ./build/ava --continue
```

## Configuration

AVA follows XDG paths on Linux:

- Config: `$XDG_CONFIG_HOME/ava/`, fallback `~/.config/ava/`
- Auth: `$XDG_CONFIG_HOME/ava/auth.json`, fallback `~/.config/ava/auth.json`
- Sessions: `$XDG_STATE_HOME/ava/sessions/`, fallback `~/.local/state/ava/sessions/`
- Project trust: `$XDG_STATE_HOME/ava/project-trust.json`, fallback `~/.local/state/ava/project-trust.json`

OpenAI auth can be created with `ava connect openai`, which opens a login picker for ChatGPT Pro/Plus browser OAuth, ChatGPT Pro/Plus headless device OAuth, or an OpenAI API key. Browser OAuth opens the default browser and listens on `http://localhost:1455/auth/callback`; headless OAuth prints `https://auth.openai.com/codex/device` plus a user code. OAuth credentials are refreshed automatically before use when a refresh token is available. Auth files also support OAuth-style tokens and API keys:

```json
{"openai":{"type":"oauth","access_token":"...","refresh_token":"...","expires_at":1893456000}}
```

```json
{"openai":{"type":"api_key","api_key":"sk-..."}}
```

The built-in default is `openai/gpt-5.5`. Override models with `$XDG_CONFIG_HOME/ava/models.json`, prompts with `$XDG_CONFIG_HOME/ava/prompts/<provider>/<family>/<mode>.txt`, replace the selected system prompt with `SYSTEM.md` or `--system-prompt`, or append with `APPEND_SYSTEM.md` or repeated `--append-system-prompt` flags. Global prompt resources live under `$XDG_CONFIG_HOME/ava`; project prompt resources live under `$WORKSPACE/.ava` and require `/trust project`. CLI prompt flags win over prompt resource files for the current process.

## Interactive Commands

- `/help`: show commands and hotkeys
- `/hotkeys` or `/keybindings`: show effective TUI hotkeys
- `/keybindings init [--force]`: create or explicitly replace a validated keybindings starter file
- `/keybindings import <path> [--force]`: validate and install a keybindings JSON file
- `/keybindings set <action> <key>[,<key>...]`: validate and edit one keybinding action
- `/keybindings reset <action>`: remove one override so the action inherits built-in defaults
- `/keybindings validate`: validate `keybinds.json` without changing active bindings
- `/theme [dark|light|plain|custom-name|reset]`: show or persist the TUI display theme
- `/reload [all|theme|models|prompts|trust|compaction|keybindings|auth|permissions|lsp|mcp|plugins]`: reload supported runtime config domains in the running TUI and report restart-required domains
- `/mode`: toggle build/plan mode
- `/details` or Ctrl+O: toggle TUI tool detail expansion
- Ctrl+G: open the current composer draft in `$VISUAL` or `$EDITOR`
- Ctrl+V: import a PNG/JPEG/WebP/GIF image from the clipboard as a pending attachment when a supported clipboard helper is available
- `/tool [query]`: show the latest or matching expanded tool card in the TUI; `/tools` is an alias
- `/diff [query]`: show the latest or matching unified tool diff in the TUI
- `/copy [tool|diff|permission] [query]`: copy the latest AVA message, latest or matching tool-card details, latest or matching unified diff, or latest or matching permission audit details in the TUI
- `/thinking`: toggle inline thinking block visibility without changing provider reasoning mode
- `/attach <path>`: import a local PNG/JPEG/WebP/GIF image into session-owned attachment storage and send it with the next normal TUI prompt; `/image` is an alias

The TUI theme precedence is `NO_COLOR`, then `AVA_TUI_THEME`, then `display.json` including custom themes under `$XDG_CONFIG_HOME/ava/themes/*.json`, then terminal background inference from `COLORFGBG`, then the built-in dark fallback.
- `/connect`: open provider and login method modals; `/login` is an alias
- `/models [query|provider/model]`: list configured models and capabilities; `/model` is an alias, Ctrl+L opens the TUI model selector, and Ctrl+P cycles to the next configured or scoped enabled model between turns
- `/scoped-models`: open the TUI scoped model-cycle selector to enable, disable, and order the Ctrl+P cycle; Ctrl+S persists the saved cycle in `models.json`
- `/sessions [--archived] [query|id]`, `/sessions rename <id> <name|--clear>`, `/sessions labels <id> <label...|--clear>`, `/sessions archive <id> --confirm`, or `/sessions unarchive <id>`: show the resumable session tree, rename/label sessions, or hide/restore sessions without deleting their JSONL files; `/tree` is an alias for the tree view
- `/fork [name]`: fork the current session at its latest entry and switch to the branch
- `/clone [name]`: clone the full current session and switch to the copy
- `/new [name]`: start a fresh session and switch to it
- `/resume [id]`: resume/switch to an existing session by exact id or unique prefix; exact `/resume` opens the TUI session selector, where PageUp/PageDown page through rows, Ctrl+S or Ctrl+T cycles recent/name/path sort, Ctrl+N toggles named sessions only, Ctrl+P toggles path display, Ctrl+A shows/hides archived sessions, Ctrl+R restores a rename command, Ctrl+L or Shift+L restores a labels command, Shift+T toggles label update timestamps, and Ctrl+D twice or Ctrl+Backspace twice archives or restores the highlighted session when the selector search is empty
- `/name <name|--clear>`: set or clear the current session display name; `/rename` is an alias
- `/labels <label...|--clear>`: set or clear current session labels; `/label` is an alias
- `/context [query|source]`: list prompt, system-prompt resource, context, prompt-command, skill, and plugin freshness with current/changed/missing status
- `/trust [status|project|deny|clear]`: inspect or change this workspace's project-resource trust decision
- `/compact [instructions]`: generate and record a provider summary
- `/export [markdown|html|jsonl] [path]`: export this session as Markdown, safe self-contained HTML, or raw AVA JSONL; `/export <file.html>` writes Pi-style HTML and `/export <file.jsonl>` writes re-importable JSONL through the permissioned file path
- `/import <path.jsonl> --confirm`: validate an AVA JSONL session archive, create a new local session, and switch to it; without `--confirm`, AVA only previews the entry count
- `/stats`: show session counts, usage, cost, and resume/export hints; `/status` is an alias
- `/permissions <list|audit|diagnose|explain|add|remove> ...`: inspect session permission audits and manage persistent permission rules; `/permission-rules` and `/perms` are aliases
- `/read <path>`: read a file through permissions
- `/write <path> <text>`: write a file through permissions using atomic replacement where practical
- `/glob <pattern>`: list readable matching files
- `/grep <text> [glob]`: literal text search
- `/bash <command>`: run an argv-style permissioned command
- `!<command>` or `!!<command>`: run the same permissioned shell helper directly from the composer; output stays visible/audited and is not injected into provider context unless you paste it into a later prompt
- `/plugins list|inspect|enable|disable|validate|failures|prompts|prompt|skills|skill`: inspect and manage local plugins
- `/plugin run <id> <command> [arguments_json]`: run a plugin command through the permissioned extension path
- `/mcp list|inspect|tools|restart`: inspect configured MCP servers and discover tools
- `/quit`: exit and print a resume command

## 0.2 Notes

- Real OpenAI requests use the local `curl` executable as the HTTP transport with a sanitized execution path.
- Tool calling is implemented through the provider contract and the built-in dispatcher.
- Startup tool visibility flags filter provider schemas and dispatch by native model-visible tool name, with Pi aliases accepted at the CLI edge. `--tools read,grep,find,ls` maps to `read_file`, `grep`, `glob`, and `list_directory`; `--exclude-tools find` removes the native `glob` schema. `/find` and `/ls` are TUI/line-shell aliases for the same permissioned `/glob` and `list_directory` paths.
- `apply_patch` currently supports up to 32 exact text replacements through an `edits` array.
- `question` opens an interactive TUI modal with single-select, multi-select, custom-answer, secret-entry, and cancel handling. Headless RPC clients can answer question requests through the protocol.
- Interactive TUI permission prompts exist for backend `ask` decisions; file mutation asks show backend-provided unified diffs when available. TUI prompts support one-shot allow/deny plus remembered exact allow/deny rule choices backed by the protected persistent-rule store. Permission decisions are persisted in session audit entries and can be inspected with `/permissions audit`, grouped with `/permissions audit summary`, or drilled into with `/permissions audit show`; denied tool details, `/tool [query]` inspections, and copied tool/permission payloads include the permission request id plus `/permissions audit show <request_id>` and `/permissions diagnose <request_id>` follow-ups when linked audit metadata is available. Non-TTY mode still fails closed unless an explicit headless allow policy is supplied or RPC replies are provided, and text print mode writes the same denial details to stderr.
- Historical 0.2 deferrals have mostly moved into the backend: multiple providers, plugins, MCP, LSP, persistent permission rules, interactive rule-management commands, and session tree/fork/clone RPC contracts now exist. Remaining follow-up work is product polish such as deeper audit navigation, provider-generated branch summaries, automatic LSP recipes, and richer diff navigation.

## 0.32 TUI Notes

- The interactive TUI now enters a wide-character ncurses (`ncursesw`) session for terminal mode, input, resize, mouse, and screen drawing.
- The visible layout remains composer-first: compact identity strip, role-aware transcript lines, compact tool cards, and a bottom-pinned AVA-style composer with the elevated surface, blue rail, and `❯` prompt.
- The composer is intentionally quiet: no persistent keybinding help or transcript status line is rendered in the input area.
- Shift+Enter or Ctrl+Enter inserts a newline; Alt+Enter submits while idle and queues a follow-up during an active assistant/compact run. Arrow Up/Down move inside multiline drafts and recall prompt history at the draft boundary before falling back to transcript scroll. Home/Ctrl+A and End/Ctrl+E move to the current line boundary. Ctrl+Left/Right, Alt+Left/Right, or Alt+B/F move by word-like segment and stop at punctuation boundaries in paths or dotted names. Ctrl+] jumps forward to the next typed character, Ctrl+Alt+] jumps backward, Delete or Ctrl+D deletes the character after the cursor while the draft has text, Ctrl+W or Alt+Backspace deletes the previous word segment, Alt+D or Alt+Delete deletes the next word segment, Ctrl+K deletes to line end and joins the next line when already at line end, Ctrl+- undoes the last edit, Ctrl+Z suspends AVA to the shell and redraws after `fg`, Ctrl+V imports a clipboard image as a pending attachment when supported, Ctrl+L opens the model selector between turns, Ctrl+P cycles to the next configured or scoped enabled model between turns, and Ctrl+D exits when the composer is empty.
- The slash palette opens above the composer with command metadata, keyboard focus cues, and narrow-terminal fallback.
- `/attach <path>` imports a supported local image into AVA-managed session storage, and Ctrl+V can import a clipboard image through `wl-paste` or `xclip` on Linux. Both paths show a pending attachment row above the composer, report the detected terminal preview mode in `/settings`, emit a row-reserved inline preview on Kitty/iTerm2-compatible terminals outside tmux/screen, and send the image with the next normal prompt. Unsupported terminals, tmux/screen, and plain display mode keep the textual metadata fallback.
- Fresh TUI launches without provider credentials render a setup row with `/connect`/`/login`, CLI, environment-variable, and auth-file guidance; submitting a provider prompt before connecting repeats the same guidance while offline slash commands still work.
- TUI display can use `/theme dark|light|plain|custom-name|reset` to persist a built-in or custom theme in `$XDG_CONFIG_HOME/ava/display.json`; `/reload theme` applies hand edits without restarting. Custom themes live under `$XDG_CONFIG_HOME/ava/themes/*.json`. `AVA_TUI_THEME=dark|light|plain` overrides persisted themes for the current process, and standard `NO_COLOR=1` remains the highest-precedence plain display override. `/settings` reports the active theme/source and exposes selectable theme, model selector, and scoped model-cycle rows.
- During an active assistant or `/compact` run, Enter or Alt+Enter on a draft queues a backend-owned follow-up turn. `/steer ...` queues steering for the next safe provider boundary. Pending queued items render above the composer, and `/restore` or Alt+Up restores the latest pending queued item to the draft before it starts. Stopped turns say to submit a new prompt to continue; pending queued items skipped by stop/finish render as transcript audit entries with delivery guidance.
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
- That slice keeps OpenAI as the default production path, hardens Anthropic native Messages support with offline/fake coverage, and adds DeepSeek/Kimi/Moonshot/OpenRouter-compatible contract coverage for request shape, reasoning content, usage/error parsing, auth/header/base URL behavior, and context-overflow classification.
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
