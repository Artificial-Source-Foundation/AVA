<!-- Last verified: 2026-03-16. Run 'just check' to revalidate. -->

# AVA Documentation

AVA is a Rust-first AI coding assistant that runs as a CLI/TUI or Tauri desktop app. It sits between minimalist tools (Pi) and batteries-included IDEs (Cursor) -- lean defaults with opt-in power via MCP servers, TOML custom tools, and multi-agent orchestration.

**20 Rust crates, ~104K LOC, 1,798 tests, single binary.**

## Quick Start

```bash
# Build and run
cargo run --bin ava              # interactive TUI
cargo run --bin ava -- --help    # see all flags

# Or use just (recommended)
just check                       # fmt + clippy + nextest
just run                         # interactive TUI
just headless "your goal"        # headless mode

# Desktop app
npm run tauri dev
```

Credentials live at `~/.ava/credentials.json`. See [CLI Testing](#cli-testing) below.

## Architecture

```
User
  |
  +-- CLI/TUI (crates/ava-tui) --+
  |                               +--> Rust crates (crates/*) --> LLM APIs
  +-- Desktop (src/ + src-tauri) -+
```

Both interfaces share the same Rust backend. The desktop frontend (SolidJS) calls Rust via Tauri IPC.

### Crate Map

| Crate | Purpose | Files | LOC |
|-------|---------|------:|----:|
| `ava-tui` | CLI/TUI binary (Ratatui + Crossterm + Tokio) | 95 | 33,119 |
| `ava-agent` | Agent execution loop, tool calling, stuck detection, mid-stream messaging | 28 | 10,251 |
| `ava-llm` | LLM provider interface, connection pool, circuit breaker, retry, routing | 25 | 11,658 |
| `ava-tools` | Tool trait, registry, 6 default + 8 extended tools, TOML custom tools | 49 | 10,641 |
| `ava-permissions` | Permission rules, bash command classifier, risk levels, path safety | 19 | 6,412 |
| `ava-config` | Config management, credentials, model catalog, thinking budgets | 14 | 5,071 |
| `ava-praxis` | Multi-agent orchestration (Director pattern), ACP, artifacts, mailbox | 15 | 3,691 |
| `ava-context` | Token tracking, context condensation (sliding window, summarization) | 17 | 3,079 |
| `ava-mcp` | Model Context Protocol client/server, stdio + HTTP transport | 6 | 1,786 |
| `ava-types` | Shared types: Message, Session, ToolCall, AvaError, ContextAttachment | 7 | 1,755 |
| `ava-session` | Session persistence (SQLite), bookmarks, conversation tree | 3 | 1,609 |
| `ava-cli-providers` | External CLI agent integration (Claude Code, etc.) | 9 | 1,510 |
| `ava-auth` | OAuth (PKCE, device code), Copilot token exchange, API key management | 8 | 1,499 |
| `ava-codebase` | Code indexing (BM25 + PageRank), dependency graph, semantic search | 10 | 1,269 |
| `ava-platform` | File system and shell abstractions | 4 | 989 |
| `ava-memory` | Persistent key-value memory with SQLite FTS5 | 2 | 778 |
| `ava-sandbox` | OS-level sandboxing (bwrap on Linux, sandbox-exec on macOS) | 8 | 670 |
| `ava-extensions` | Extension system: hooks, native/WASM loaders | 5 | 509 |
| `ava-db` | SQLite connection pool, session/message models | 4 | 444 |
| `ava-validator` | Code validation pipeline with retry orchestration | 3 | 299 |

Full crate dependency graph: [architecture/crate-map.md](architecture/crate-map.md)

### Tool Surface

| Tier | Count | Tools |
|------|------:|-------|
| Default | 6 | `read`, `write`, `edit`, `bash`, `glob`, `grep` |
| Extended | 8 | `apply_patch`, `web_fetch`, `web_search`, `multiedit`, `ast_ops`, `lsp_ops`, `code_search`, `git_read` |
| Always-on helpers | -- | `todo_read`, `todo_write`, `question`, `task`, `codebase_search`, memory tools, session tools |
| Dynamic | -- | MCP server tools + TOML custom tools |

Extended tools are available but not sent to the LLM unless `extended_tools` is enabled.

## Adding Tools

### TOML Custom Tools

Drop a `.toml` file in `~/.ava/tools/` (global) or `.ava/tools/` (project-local). See [plugins.md](plugins.md) for the full format and examples.

```toml
name = "deploy"
description = "Deploy the current branch to staging"

[[params]]
name = "env"
type = "string"
required = true
description = "Target environment"

[execution]
type = "shell"
command = "scripts/deploy.sh {{env}}"
timeout_secs = 120
```

### MCP Servers

Configure in `~/.ava/mcp.json` (global) or `.ava/mcp.json` (project-local). See [plugins.md](plugins.md) for details.

```json
{
  "servers": [
    {
      "name": "filesystem",
      "transport": {
        "type": "stdio",
        "command": "npx",
        "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"]
      }
    }
  ]
}
```

## Extending AVA

AVA supports multiple extension mechanisms:

1. **TOML Custom Tools** -- simple shell/script tools defined in TOML files
2. **MCP Servers** -- connect any MCP-compatible server (stdio or HTTP)
3. **Project Instructions** -- `AGENTS.md`, `.ava/rules/*.md`, `CLAUDE.md` auto-injected into system prompt
4. **Lifecycle Hooks** -- `.ava/hooks/*.toml` for pre/post events
5. **Custom Slash Commands** -- `.ava/commands/` for project-specific commands
6. **Agent Configs** -- `.ava/agents.toml` for sub-agent configuration
7. **Native/WASM Extensions** -- `ava-extensions` crate (advanced)

See [plugins.md](plugins.md) for detailed plugin/extension documentation.

## CLI Testing

```bash
# Smoke test
cargo run --bin ava -- "Reply with SMOKE_OK" --headless \
  --provider openrouter --model anthropic/claude-haiku-4.5 --max-turns 3

# Multi-agent
cargo run --bin ava -- "goal" --headless --multi-agent \
  --provider openrouter --model anthropic/claude-haiku-4.5

# Mid-stream messaging
cargo run --bin ava -- "goal" --headless \
  --follow-up "also run tests" \
  --provider openrouter --model anthropic/claude-haiku-4.5
```

## Document Index

| Document | Purpose |
|----------|---------|
| [CLAUDE.md](../CLAUDE.md) | Architecture, conventions, commands -- primary AI agent reference |
| [AGENTS.md](../AGENTS.md) | AI agent instructions for working on AVA |
| [architecture/crate-map.md](architecture/crate-map.md) | Detailed crate dependency map with key types |
| [plugins.md](plugins.md) | TOML custom tools and MCP server guide |
| [architecture/](architecture/) | System design, data flow, Praxis multi-agent |
| [development/roadmap.md](development/roadmap.md) | Sprint history and status |
| [development/backlog.md](development/backlog.md) | Open backlog items |
| [reference-code/](reference-code/) | Competitor source code notes (12 projects) |
| [troubleshooting/](troubleshooting/) | Common issues and fixes |
