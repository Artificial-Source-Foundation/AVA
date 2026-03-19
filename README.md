<div align="center">

# AVA

### AI coding agent for your terminal

[![Rust](https://img.shields.io/badge/Rust-100%25-dea584?style=flat-square&logo=rust)](https://www.rust-lang.org/)
[![Tests](https://img.shields.io/badge/tests-1%2C712_passing-brightgreen?style=flat-square)](https://github.com/ASF-GROUP/AVA)
[![Crates](https://img.shields.io/badge/crates-21-blue?style=flat-square)](https://github.com/ASF-GROUP/AVA/tree/master/crates)
[![License: MIT](https://img.shields.io/badge/license-MIT-yellow?style=flat-square)](LICENSE)
[![Version](https://img.shields.io/badge/version-2.1.0-purple?style=flat-square)](https://github.com/ASF-GROUP/AVA/releases)

Lean by default. Infinitely extensible. Pure Rust.

[Getting Started](#getting-started) · [Features](#features) · [Multi-Agent](#praxis-multi-agent) · [Docs](https://useava.dev)

</div>

---

AVA is an AI coding agent that runs in your terminal. It ships with 6 tools and a clean TUI, then scales to multi-agent orchestration, MCP servers, custom tools, and plugins — without ever leaving the command line.

Built entirely in Rust. Single binary. No runtime dependencies.

## Getting Started

**Install from source:**

```bash
git clone https://github.com/ASF-GROUP/AVA.git && cd AVA
cargo build --release --bin ava
cp target/release/ava ~/.local/bin/
```

**Add a provider:**

```bash
mkdir -p ~/.ava
echo '{"providers":{"openrouter":{"api_key":"YOUR_KEY"}}}' > ~/.ava/credentials.json
```

**Run:**

```bash
ava                                    # Interactive TUI
ava "fix the login bug" --headless     # Headless mode
ava serve --port 8080                  # Web browser mode
```

AVA supports 8 LLM providers out of the box: **Anthropic**, **OpenAI**, **Gemini**, **Ollama**, **OpenRouter**, **GitHub Copilot**, **Inception**, and a mock provider for testing.

## Features

### Four Interfaces

| Mode | Command | Description |
|------|---------|-------------|
| **TUI** | `ava` | Full-featured terminal UI with syntax highlighting, tool approval, and session management |
| **Headless** | `ava "goal" --headless` | Non-interactive mode for CI/CD and scripting |
| **Web** | `ava serve --port 8080` | Browser-based UI served over HTTP + WebSocket |
| **Desktop** | `npm run tauri dev` | Native desktop app (SolidJS + Tauri) |

### Tools

AVA ships with 6 default tools and 8 extended tools. Dynamic tools load at runtime from MCP servers and TOML definitions.

| Tier | Tools |
|------|-------|
| **Default** (6) | `read` `write` `edit` `bash` `glob` `grep` |
| **Extended** (8) | `apply_patch` `web_fetch` `web_search` `multiedit` `ast_ops` `lsp_ops` `code_search` `git_read` |
| **Agent** (1) | `plan` — inline plan editing with agent-to-TUI communication |
| **Dynamic** | MCP servers, TOML custom tools, plugins |

### Edit Reliability

AVA's `edit` tool uses a 15-strategy cascade including 3-way merge and diff-match-patch — the highest reliability of any terminal-based coding agent. If one strategy fails, it automatically falls through to the next.

### Mid-Stream Messaging

Interact with the agent while it works. Three tiers of message injection:

| Tier | Key | Behavior |
|------|-----|----------|
| **Steering** | `Enter` | Interrupts after current tool — redirects the agent |
| **Follow-up** | `Alt+Enter` | Queued for after the current task completes |
| **Post-complete** | `Ctrl+Alt+Enter` | Grouped pipeline (G1, G2, G3...) that runs after all work finishes |

### Permission System

A 9-step inspector classifies every bash command across 8 safety tags and 5 risk levels. Even in auto-approve mode, critical commands (rm -rf /, sudo, fork bombs) are always blocked.

```bash
ava --auto-approve         # Auto-approve safe operations
ava --yolo                 # Alias for --auto-approve
```

### Prompt Caching & Performance

- Anthropic `cache_control` on system prompts and tool definitions
- Auto-retry middleware with exponential backoff for read-only tools
- Stream silence timeout (90s, configurable, per-chunk reset)
- tiktoken-rs BPE token counting
- Circuit breaker (5-failure threshold, 30s cooldown) on all remote providers

## Praxis (Multi-Agent)

Praxis is AVA's multi-agent orchestration system. An LLM-powered Director analyzes task complexity and assembles the right team.

```
Director (strongest model)
├── Scouts (cheapest model — read codebase, produce summaries)
├── Board of Directors (3 SOTA models vote on approach — complex tasks only)
└── Leads (strong model — "Backend Lead", "QA Lead", etc.)
    └── Workers (mid-tier model — "Pedro (Jr. Backend)", "Sofia (Jr. Frontend)")
```

### Intelligence Levels

| Level | Complexity | What Happens |
|-------|-----------|--------------|
| **1** | Simple fix | 1 worker + 1 QA worker. No leads needed. |
| **2** | Multi-file, clear scope | Scouts → plan → user review → 2-3 leads with workers |
| **3** | Major refactor | Scouts → Board of Directors (3 models vote) → user approves → full team |

### Smart Model Routing

Each role uses the right model tier for cost efficiency:

| Role | Tier | Examples |
|------|------|---------|
| Scouts | Cheapest | Haiku, Flash, Mercury |
| Workers | Mid-tier | Sonnet, GPT-5.3 |
| Leads | Strong | Sonnet, Opus |
| Director | Strongest | Opus, GPT-5.4 |
| Board | Top per provider | Best from each configured provider |

```bash
ava "refactor the auth system" --multi-agent
```

## Extensibility

### TOML Custom Tools

Drop a file in `~/.ava/tools/` and the agent can call it like any built-in:

```toml
name = "deploy"
description = "Deploy to environment"

[[params]]
name = "env"
type = "string"
required = true

[execution]
type = "shell"
command = "./deploy.sh {{env}}"
```

### MCP Servers

Configure in `~/.ava/mcp.json` or `.ava/mcp.json`:

```json
{
  "servers": {
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"]
    }
  }
}
```

### Power Plugins

Plugins run as isolated subprocesses (JSON-RPC) — a broken plugin can never crash AVA. Write them in TypeScript, Python, or any language.

```typescript
import { createPlugin } from "@ava-ai/plugin";

createPlugin({
  "tool.before": async (ctx, { tool, args }) => {
    if (tool === "read" && args.file_path.includes(".env")) {
      throw new Error("Blocked: don't read .env files");
    }
  },
});
```

12 lifecycle hooks: `auth`, `auth.refresh`, `request.headers`, `tool.before`, `tool.after`, `agent.before`, `agent.after`, `session.start`, `session.end`, `config`, `event`, `shell.env`.

### Project Instructions

AVA auto-discovers instruction files and injects them into the agent's system prompt:

- `~/.ava/AGENTS.md` — global rules
- `AGENTS.md`, `CLAUDE.md` — project-level (ancestor walk to `.git` boundary)
- `.ava/rules/*.md` — project-local rules
- `.cursorrules`, `.github/copilot-instructions.md` — compatible with existing workflows

## Architecture

21 Rust crates. ~107K lines of Rust. 1,712 tests. Single binary.

```
ava (binary)
├── ava-agent        Agent loop, reflection, stuck detection, smart completion
│   ├── ava-llm          8 LLM providers + streaming + prompt caching
│   ├── ava-tools        6 default + 8 extended tools, middleware pipeline
│   ├── ava-praxis       Multi-agent orchestration (Director → Leads → Workers)
│   └── ava-plugin       Plugin system (JSON-RPC, subprocess isolation)
├── ava-tui          Terminal UI (Ratatui + Crossterm + Tokio)
├── ava-session      Session persistence (SQLite + FTS5)
├── ava-memory       Persistent memory (SQLite + FTS5)
├── ava-context      Token tracking + context condensation
├── ava-codebase     Code indexing (BM25 + PageRank)
├── ava-mcp          Model Context Protocol client/server
├── ava-permissions  Permission rules + bash command classifier
├── ava-config       Config, credentials, model catalog
├── ava-auth         OAuth + credential flows
├── ava-sandbox      Command sandboxing (bwrap / sandbox-exec)
├── ava-platform     File system + shell abstractions
├── ava-extensions   Extension system (hooks, native/WASM)
├── ava-validator    Validation pipeline
├── ava-db           SQLite connection pool
├── ava-types        Shared types
└── ava-cli-providers External CLI agent integration
```

## Configuration

```
~/.ava/
├── config.yaml          # Settings (model, provider, compaction, etc.)
├── credentials.json     # API keys per provider
├── mcp.json             # MCP server definitions
├── AGENTS.md            # Global agent instructions
├── tools/               # TOML custom tools
├── plugins/             # Installed plugins
└── themes/              # Custom TOML themes (29 built-in)
```

### Provider Credentials

```json
{
  "providers": {
    "anthropic": { "api_key": "sk-ant-..." },
    "openrouter": { "api_key": "sk-or-..." },
    "ollama": { "base_url": "http://localhost:11434" }
  }
}
```

## CLI Reference

```
ava [GOAL] [OPTIONS]

Options:
  -m, --model <MODEL>         LLM model (e.g., anthropic/claude-sonnet-4)
      --provider <NAME>       LLM provider
      --max-turns <N>         Maximum agent turns (default: 20)
      --auto-approve          Auto-approve safe tool executions
      --headless              Non-interactive mode
      --json                  NDJSON output (implies --headless)
      --multi-agent           Enable Praxis multi-agent mode
      --follow-up <MSG>       Queue a follow-up message (Tier 2)
      --later <MSG>           Queue a post-complete message (Tier 3)

Commands:
  ava serve [--port PORT]     Start web server mode
  ava review [--staged]       Code review
  ava plugin [SUBCOMMAND]     Plugin management (list/add/remove/init)
```

### Slash Commands

| Command | Description |
|---------|-------------|
| `/model [provider/model]` | Show or switch model |
| `/sessions` | Session picker (`Ctrl+L`) |
| `/permissions` | Toggle permission level |
| `/theme [name]` | Switch theme (29 built-in) |
| `/compact [focus]` | Compact conversation context |
| `/commit` | Inspect commit readiness |
| `/later <msg>` | Queue post-complete message |
| `/queue` | View queued messages |
| `/export [file]` | Export conversation |
| `/copy` | Copy last response (`Ctrl+Y`) |
| `/help` | Show all commands |

## Contributing

```bash
# Build and run
cargo run --bin ava

# Run all checks (format + lint + test)
just check

# Or individually
cargo fmt --all
cargo clippy --workspace
cargo test --workspace          # 1,712 tests

# Smoke test with a real provider
cargo run --bin ava -- "Reply with SMOKE_OK" --headless \
  --provider openrouter --model anthropic/claude-haiku-4.5 --max-turns 3
```

### Adding a Tool

1. Create `crates/ava-tools/src/core/your_tool.rs`
2. Implement the `Tool` trait (`name`, `description`, `parameters`, `execute`)
3. Register in `register_core_tools()` — default tier requires strong justification; prefer Extended
4. Add tests: `cargo test -p ava-tools`

### Adding an LLM Provider

1. Create `crates/ava-llm/src/providers/your_provider.rs`
2. Implement the `LLMProvider` trait
3. Add tests: `cargo test -p ava-llm`

See [CLAUDE.md](CLAUDE.md) for full architecture conventions and [docs/](docs/) for detailed documentation.

## License

[MIT](LICENSE)

---

<div align="center">

[useava.dev](https://useava.dev) · [avacli.dev](https://avacli.dev) · [tryava.dev](https://tryava.dev) · [ava.engineering](https://ava.engineering)

</div>
