# AVA

[![CI](https://github.com/Artificial-Source/AVA/actions/workflows/ci.yml/badge.svg?branch=develop)](https://github.com/Artificial-Source/AVA/actions/workflows/ci.yml)

AVA is a native C++23 agentic coding tool. The active default branch is `develop`; historical branches are kept under `archive/*`. The current backend baseline declares runtime version `1.0.0` and includes OpenAI and Kimi-for-coding live-verified provider paths, safe built-in tools, build/plan modes, permission prompts, tool visibility, append-only JSONL sessions, headless print/RPC modes, local plugin/MCP foundations, and an interactive TUI backed by wide-character ncurses (`ncursesw`). Backend release-position docs moved through the 0.60 platform catch-up, 0.65 provider-native hardening, bundled 0.70 reasoning/model lifecycle closeout, 0.75 extension foundation, 0.80 extension stabilization, and 0.90 release-candidate verification before this `1.0.0` runtime bump. A runtime version bump is not a published release by itself; tag, artifact, package, and external release publication steps remain separate manual operations.

## Clone and Build

Clone all submodules before configuring:

```sh
git clone --branch develop --single-branch --recurse-submodules https://github.com/Artificial-Source/AVA.git
cd AVA
```

If an existing clone was made without `--recurse-submodules`, recover it with:

```sh
git submodule update --init --checkout --recursive
```

`./autogen.sh` is an optional maintainer convenience. It initializes missing submodules at AVA's pinned commits, sets the repository's missing `push.recurseSubmodules` safety default, and prints CMake guidance; it does not update dependency branches, configure or build AVA, and is not needed after a recursive clone.

Build-only requirements:

- CMake 3.25+
- C++23 compiler
- Boost development headers and CMake package
- `ncursesw` development headers/library
- Git and configuration-time access to required dependency sources

The optional Qt Quick desktop prototype additionally requires Qt 6.5+ with QML, Quick, and Quick Controls 2. See `docs/desktop-qml.md`.

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
scripts/build.sh --build-dir build
scripts/run-tests.sh --build-dir build
```

Equivalent CMake presets are available for local development:

```sh
cmake --preset dev
scripts/build.sh
scripts/run-tests.sh
```

Sanitizer build:

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
scripts/build.sh --build-dir build-sanitize --jobs 2
scripts/run-tests.sh --build-dir build-sanitize --jobs 2
```

Or with presets:

```sh
cmake --preset sanitize
scripts/build.sh --build-dir build-sanitize --jobs 2
scripts/run-tests.sh --build-dir build-sanitize --jobs 2
```

`scripts/build.sh` and `scripts/run-tests.sh` default to the `build` tree and all available logical cores. Pass `--jobs N` (or set `CMAKE_BUILD_PARALLEL_LEVEL` / `CTEST_PARALLEL_LEVEL`) to cap concurrency; build options such as `--target` and CTest filters such as `-R` are forwarded. Both runners share a build-tree lock so builds and tests cannot modify one tree concurrently. Sanitizer examples use two jobs to limit memory pressure.

GitHub Actions runs both the normal and sanitizer test jobs on pushes and pull requests targeting `develop`. Dependabot is enabled for GitHub Actions updates on `develop`.

**For detailed cmake configuration options and build instructions see [CONTRIBUTING](docs/CONTRIBUTING.md).**

### Linux host artifact

Create the AVA-only host archive and checksum outside the checkout with:

```sh
scripts/package-linux.sh --output-dir /absolute/path/outside/AVA
```

The script builds Release `ava` plus the fake-provider smoke helper, stages only CMake component `ava`, extracts the archive fresh, verifies its checksum and CLI behavior, and runs the deterministic model smoke. `--binary /absolute/path/to/ava` accepts an existing binary; add `--fake-provider /absolute/path/to/ava_fake_provider_server` to run the model smoke in that mode. Without a fake helper the script prints an explicit skip.

This is a dynamically linked **host** artifact, not a portable Linux bundle. The destination needs compatible glibc, libstdc++, and libgcc runtimes; ncursesw/tinfo libraries plus a usable terminfo database; and `curl` on `PATH`. Cross-distribution compatibility must be checked on the intended host.

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
./build/ava --thinking high "reason about this change"
./build/ava --tools read_file,grep "inspect safely"
./build/ava --tools read,grep,find,ls "inspect with Pi-style names"
./build/ava --rpc
./build/ava --acp
```

Bare prompt text and `--print` both run one prompt and exit. Multiple positional prompt tokens are joined with spaces, so `ava summarize this repo` is equivalent to `ava "summarize this repo"`. CLI `@path` text file arguments are expanded through the same bounded permissioned file-reference path used by the TUI, including quoted shell arguments for paths with spaces. Add `--json`, `--output json`, or Pi-compatible `--mode json` to emit runtime events instead of final text only. `--thinking off|<level>` is a Pi-compatible startup alias for AVA's reasoning control; non-`off` values must be declared by the active model metadata, such as `low`, `medium`, `high`, or `xhigh` on the default GPT-5.5 profile. `--tools`, `--exclude-tools`, `--no-builtin-tools`, and `--no-tools` control model-visible tools for TUI, print, and RPC sessions; AVA accepts native names plus Pi aliases such as `read`, `write`, `edit`, `find`, and `ls`, but still advertises one native provider schema per operation. These visibility flags are separate from permission policy. `--allow read-only` auto-approves AVA operations classified as read-only; `--allow-tool <list>` auto-approves the named supported tool families and does not make hidden tools model-visible. `--rpc`, `--output rpc`, or Pi-compatible `--mode rpc` starts AVA's proprietary JSONL stdio RPC v1 endpoint for automation and custom clients; RPC prompt requests can import local image paths through `attachments:["path.png"]` or Pi-style inline uploads through `images:[{"type":"image","data":"...","mimeType":"image/png"}]` for image-capable models. See [`docs/rpc-protocol.md`](docs/rpc-protocol.md) for the normative request, response, event, resolver, and lifecycle contract. `--acp` starts the separate stable ACP v1 JSON-RPC 2.0 stdio endpoint for specifically configured editor/IDE clients; see [`docs/acp.md`](docs/acp.md) for Zed/JetBrains/CodeCompanion setup and exact evidence labels, [`docs/acp-support.json`](docs/acp-support.json) for the machine-checked profile, and the [`docs/interop/evidence`](docs/interop/evidence/README.md) policy. Configuration does not imply execution evidence. ACP does not replace AVA's proprietary JSONL RPC or migrate AVA sessions; AVA RPC is also distinct from generic JSON-RPC 2.0 and Pi RPC reference behavior.

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

The built-in default is `openai/gpt-5.5`; `/model` can also select `openai/gpt-5.6-sol`, `openai/gpt-5.6-terra`, or `openai/gpt-5.6-luna`. Override models with `$XDG_CONFIG_HOME/ava/models.json`, prompts with `$XDG_CONFIG_HOME/ava/prompts/<provider>/<family>/<mode>.txt`, replace the selected system prompt with `SYSTEM.md` or `--system-prompt`, or append with `APPEND_SYSTEM.md` or repeated `--append-system-prompt` flags. Global prompt resources live under `$XDG_CONFIG_HOME/ava`; project prompt resources live under `$WORKSPACE/.ava` and require `/trust project`. CLI prompt flags win over prompt resource files for the current process.

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
- `/context [query|source]`: list base prompt metadata, system-prompt resources, context files, prompt commands, skills, subagents, plugins, LSP config diagnostics, and freshness status
- `/trust [status|project|deny|clear]`: inspect or change this workspace's project-resource trust decision for commands, skills, subagents, plugins, MCP/LSP config, and system prompt resources
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

## Documentation

- Start with [`docs/README.md`](docs/README.md) for the full docs map.
- Use [`docs/USAGE.md`](docs/USAGE.md) for TUI commands, headless modes, tool visibility, and current limits.
- Use [`docs/CONFIG.md`](docs/CONFIG.md) for XDG paths, auth, models, prompts, subagents, project trust, and local resource layout.
- Use [`docs/TESTING.md`](docs/TESTING.md) for CTest, opt-in live smokes, terminal smokes, and release evidence.
- Use [`docs/rpc-protocol.md`](docs/rpc-protocol.md) for proprietary AVA RPC v1; use [`docs/acp.md`](docs/acp.md) and [`docs/interop/evidence/README.md`](docs/interop/evidence/README.md) for ACP client setup and evidence; [`docs/headless-protocol.md`](docs/headless-protocol.md) summarizes shared print/RPC headless behavior.
- Use [`docs/release-checklist.md`](docs/release-checklist.md) for the implemented local Linux host artifact and release-gate scope.
- Product and parity status lives under [`docs/product/`](docs/product/), [`docs/roadmap/`](docs/roadmap/), and [`docs/goals/`](docs/goals/); historical release ledgers live under [`docs/versions/`](docs/versions/).
