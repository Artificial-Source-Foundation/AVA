# AVA Codebase Structure

## Top Level

| Path | Purpose |
|------|---------|
| `crates/` | Rust workspace with the main runtime, tools, HQ, config, storage, and TUI |
| `src/` | SolidJS desktop/web frontend |
| `src-tauri/` | Tauri host plus Rust IPC commands |
| `docs/` | Live documentation set |
| `design/` | Pencil source for the desktop redesign |
| `e2e/` | Playwright end-to-end coverage |
| `.ava/` | Repo-local agent rules, HQ memory, and project config |

## Rust Workspace

AVA currently ships with 21 Rust crates:

- `ava-tui` — CLI/TUI binary, headless mode, and web server entrypoints
- `ava-agent` — agent loop, routing, tool execution, prompts, and stack runtime
- `ava-hq` — HQ multi-agent orchestration
- `ava-llm` — model provider implementations and routing
- `ava-tools` — built-in tool registry and execution middleware
- `ava-config` — config, credentials, model catalog, trust, and routing settings
- `ava-session` — session persistence and diff tracking
- `ava-memory` — persistent memory storage
- `ava-db` — SQLite migrations and shared models
- `ava-types` — shared domain types
- `ava-auth` — OAuth and credential flows
- `ava-codebase` — indexing, symbols, ranking, and search
- `ava-context` — context management and compaction
- `ava-permissions` — permissions, audit, and command classification
- `ava-platform` — filesystem and shell abstractions
- `ava-sandbox` — sandboxed command execution
- `ava-mcp` — MCP client/server integration
- `ava-plugin` — plugin discovery and runtime
- `ava-extensions` — extension loading and hooks
- `ava-acp` — external agent integration via ACP
- `ava-validator` — validation pipeline

## Documentation Map

| Path | Purpose |
|------|---------|
| `docs/README.md` | docs entry point |
| `docs/backlog.md` | verified open work and recent completions |
| `docs/hq/README.md` | HQ architecture and UX notes |
| `docs/plugins.md` | TOML custom tools and MCP guide |
| `docs/releasing.md` | release flow |
| `docs/troubleshooting/` | platform-specific fixes |
| `CHANGELOG.md` | release history |
| `CLAUDE.md` | architecture and contributor conventions |
| `AGENTS.md` | AI coding agent instructions |
