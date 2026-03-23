<!-- Last verified: 2026-03-23. Run 'just check' to revalidate. -->

# AVA Documentation

AVA is a Rust-first AI coding assistant that runs as a CLI/TUI, web server, or Tauri desktop app. It sits between minimalist tools (Pi) and batteries-included IDEs (Cursor) -- lean defaults with opt-in power via MCP servers, TOML custom tools, and multi-agent orchestration.

**21 Rust crates in the root workspace, plus the Tauri desktop host in `src-tauri/`.**

## Quick Start

```bash
# Build and run
cargo run --bin ava              # interactive TUI
cargo run --bin ava -- --help    # see all flags
cargo run --bin ava -- serve --port 8080  # web browser mode

# Or use just (recommended)
just check                       # fmt + clippy + nextest
just run                         # interactive TUI
just headless "your goal"        # headless mode

# Desktop app
pnpm tauri dev
```

Credentials live at `~/.ava/credentials.json`. See [CLI Testing](#cli-testing) below.

## Architecture

```
User
  |
  +-- CLI/TUI (crates/ava-tui) -----+
  |                                   +--> Rust crates (crates/*) --> LLM APIs
  +-- Web (ava serve, HTTP + WS) ----+
  |                                   |
  +-- Desktop (src/ + src-tauri) ----+
```

All three interfaces share the same Rust backend. The desktop frontend (SolidJS) calls Rust via Tauri IPC. Web mode uses HTTP API + WebSocket for agent streaming.

### Crate Map

| Crate | Purpose |
|-------|---------|
| `ava-tui` | CLI/TUI binary plus headless/web entrypoints |
| `ava-agent` | Agent execution loop, tool calling, instruction loading |
| `ava-llm` | Provider interface, routing, retries, streaming |
| `ava-tools` | Tool trait, registry, default tool set, custom tool loading |
| `ava-permissions` | Permission rules, bash classification, path safety |
| `ava-config` | Config management, credentials, model catalog |
| `ava-praxis` | Multi-agent orchestration and planning flows |
| `ava-context` | Token tracking and context condensation |
| `ava-mcp` | MCP client/server support |
| `ava-types` | Shared types used across the workspace |
| `ava-plugin` | Plugin runtime and hook dispatch |
| `ava-session` | Session persistence and conversation state |
| `ava-cli-providers` | External CLI agent integrations |
| `ava-auth` | OAuth and API credential flows |
| `ava-codebase` | Code indexing and search |
| `ava-platform` | File system and shell abstractions |
| `ava-memory` | Persistent memory storage |
| `ava-sandbox` | Command sandboxing |
| `ava-extensions` | Native/WASM extension loading |
| `ava-db` | SQLite connection pool and DB models |
| `ava-validator` | Validation pipeline utilities |

Full crate dependency graph: [architecture/crate-map.md](architecture/crate-map.md)

### Tool Surface

| Tier | Count | Tools |
|------|------:|-------|
| Default | 9 | `read`, `write`, `edit`, `bash`, `glob`, `grep`, `web_fetch`, `web_search`, `git_read` |
| Built-in helpers | runtime | `task`, `todo_read`, `todo_write`, `question`, `plan`, and related session/memory helpers |
| Dynamic | runtime | MCP server tools + TOML custom tools |

The default set is the prompt-visible built-in surface. Other helpers are registered by runtime context or configuration.

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

## Documentation Index

### Quick Reference

| Document | Purpose |
|----------|---------|
| [CLAUDE.md](../CLAUDE.md) | Architecture, conventions, commands -- primary AI agent reference |
| [AGENTS.md](../AGENTS.md) | AI agent instructions for working on AVA |
| [codebase/](codebase/) | **Complete codebase reference** -- all 21 crates, frontend, plugins |

### Codebase Reference (codebase/)

Complete documentation for every crate and component:

| Doc | Contents |
|-----|----------|
| [codebase/README.md](codebase/README.md) | Entry point, quick navigation, dependency graph |
| [codebase/frontend.md](codebase/frontend.md) | SolidJS frontend, hooks, stores, Tauri IPC |
| [codebase/tauri-commands.md](codebase/tauri-commands.md) | 70+ Rust commands exposed to frontend |
| [codebase/plugins.md](codebase/plugins.md) | Plugin architecture, SDKs, hooks |

**Crate docs:** [ava-agent](codebase/ava-agent.md) • [ava-auth](codebase/ava-auth.md) • [ava-cli-providers](codebase/ava-cli-providers.md) • [ava-codebase](codebase/ava-codebase.md) • [ava-config](codebase/ava-config.md) • [ava-context](codebase/ava-context.md) • [ava-db](codebase/ava-db.md) • [ava-extensions](codebase/ava-extensions.md) • [ava-llm](codebase/ava-llm.md) • [ava-mcp](codebase/ava-mcp.md) • [ava-memory](codebase/ava-memory.md) • [ava-permissions](codebase/ava-permissions.md) • [ava-platform](codebase/ava-platform.md) • [ava-plugin](codebase/ava-plugin.md) • [ava-praxis](codebase/ava-praxis.md) • [ava-sandbox](codebase/ava-sandbox.md) • [ava-session](codebase/ava-session.md) • [ava-tools](codebase/ava-tools.md) • [ava-tui](codebase/ava-tui.md) • [ava-types](codebase/ava-types.md) • [ava-validator](codebase/ava-validator.md)

### Architecture & Design

| Document | Purpose |
|----------|---------|
| [architecture/crate-map.md](architecture/crate-map.md) | Detailed crate dependency map with key types |
| [architecture/plugin-system.md](architecture/plugin-system.md) | Power plugin system design |
| [architecture/](architecture/) | System design, data flow, Praxis multi-agent |

### Plugins & Extensions

| Document | Purpose |
|----------|---------|
| [plugins.md](plugins.md) | TOML custom tools and MCP server guide |

### Development

| Document | Purpose |
|----------|---------|
| [development/CHANGELOG.md](development/CHANGELOG.md) | Version history |
| [development/roadmap.md](development/roadmap.md) | Sprint history and status |
| [development/backlog.md](development/backlog.md) | Open backlog items |

### Reference & Troubleshooting

| Document | Purpose |
|----------|---------|
| [reference-code/](reference-code/) | Competitor source code notes (12 projects) |
| [troubleshooting/](troubleshooting/) | Common issues and fixes |
