

# AVA v2.1

> AI coding assistant — Rust CLI/TUI with multi-agent orchestration, code review, and MCP plugins

**Official domains:**
[useava.dev](https://useava.dev) | [avacli.dev](https://avacli.dev) | [tryava.dev](https://tryava.dev) | [ava.engineering](https://ava.engineering)

A Rust-first AI coding assistant with an interactive TUI, autonomous agent execution, multi-agent workflows, code review, voice input, and a Tauri desktop app. 19 built-in tools plus dynamic MCP and custom tool support.

Verified: All 19 tools, 5 modes, 3 providers pass E2E (2026-03-08). See [test matrix](docs/development/test-matrix.md).

## Installation

```bash
# Quick install (Linux/macOS)
curl -fsSL https://raw.githubusercontent.com/ASF-GROUP/AVA/master/install.sh | sh
```

The installer detects your OS and architecture, downloads the latest release, verifies the SHA256 checksum, and installs `ava` to `~/.ava/bin/`.

The CLI and TUI are a single Rust binary with zero runtime dependencies — no Node.js, no NPM, no Python. This eliminates an entire class of supply chain risks common in JavaScript-based tools.

### Desktop App

Download the desktop app from the [releases page](https://github.com/ASF-GROUP/AVA/releases).

| Platform              | Download                            |
|-----------------------|-------------------------------------|
| macOS (Apple Silicon) | `AVA_aarch64-apple-darwin.dmg`      |
| macOS (Intel)         | `AVA_x86_64-apple-darwin.dmg`       |
| Windows               | `AVA_x86_64-pc-windows-msvc.exe`    |
| Linux (x64)           | `.deb`, `.AppImage`, or `.rpm`      |

### Build from Source

If you prefer to audit the code yourself, build from source. This is the recommended path for security-conscious users.

```bash
git clone https://github.com/ASF-GROUP/AVA.git
cd AVA

# Review the code, then run the full test suite
cargo test --workspace
cargo clippy --workspace

# Build a release binary (~15MB)
cargo build --release --bin ava

# The binary is at target/release/ava — copy it wherever you like
cp target/release/ava ~/.local/bin/

# Or just run it directly
./target/release/ava
```

**Prerequisites**: [Rust toolchain](https://rustup.rs/) (1.75+). No Node.js required for the CLI.

> Building from source lets you verify every line of code, run the full test suite (~820 tests), and confirm nothing unexpected is compiled in. See [SECURITY.md](SECURITY.md) for our security policy and vulnerability reporting process.

### Desktop App (from source)

```bash
# Additional prerequisites: Node.js 20+, pnpm 10+
pnpm install
npm run tauri dev          # Development
npm run tauri build        # Production build
```

## Quick Start

```bash
# Add your API key
mkdir -p ~/.ava
cat > ~/.ava/credentials.json << 'EOF'
{
  "providers": {
    "openrouter": { "api_key": "YOUR_OPENROUTER_KEY" }
  }
}
EOF

# Launch the interactive TUI
ava

# Headless mode (batch/CI)
ava "refactor the auth module" --headless --provider openrouter --model anthropic/claude-sonnet-4

# JSON output (scripting)
ava "list all TODO comments" --headless --json

# Code review
ava review --staged
ava review --diff main..HEAD --format markdown

# Multi-agent workflow
ava "build the new API" --workflow plan-code-review
```

## CLI Flags & Subcommands

```
ava [GOAL] [OPTIONS]
  -c, --continue           Resume last session
      --session <ID>       Resume a specific session
  -m, --model <MODEL>      LLM model to use
      --provider <NAME>    LLM provider (anthropic, openai, openrouter, gemini, ollama)
      --max-turns <N>      Maximum agent turns (default: 20)
      --yolo               Auto-approve all tool executions
      --headless           Force headless mode (no TUI)
      --json               Output NDJSON events (implies headless)
      --multi-agent        Use Commander multi-agent mode
      --workflow <NAME>    Run workflow pipeline (plan-code-review, code-review, plan-code)
      --voice              Enable continuous voice input (requires --features voice)
      --theme <NAME>       Theme name (default: "default")

ava review [OPTIONS]
      --staged             Review staged changes
      --diff <RANGE>       Review a diff range (e.g. "main..HEAD")
      --commit <SHA>       Review a specific commit
      --working            Review unstaged working directory changes
      --format <FMT>       Output format: text, json, markdown (default: text)
      --focus <AREA>       Focus area for review (default: all)
      --fail-on <LEVEL>    Exit 1 on issues at/above: critical, warning, suggestion, any
      --provider <NAME>    LLM provider
  -m, --model <MODEL>      LLM model
      --max-turns <N>      Maximum turns (default: 10)
```

## Architecture

```
                   ┌──────────────┐
                   │   ava-tui    │  CLI/TUI binary (Ratatui + Crossterm + Tokio)
                   └──────┬───────┘
                          │
           ┌──────────────┼──────────────┐
           ▼              ▼              ▼
     ┌──────────┐  ┌───────────┐  ┌────────────┐
     │ava-agent │  │ava-tools  │  │ava-session │
     │(loop +   │  │(19 tools +│  │(SQLite +   │
     │ reflect) │  │ MCP/custom│  │ FTS5)      │
     └────┬─────┘  └───────────┘  └────────────┘
          │
     ┌────┴─────┐
     │ ava-llm  │  6 providers (Anthropic, OpenAI, Gemini, OpenRouter, Ollama + mock)
     └──────────┘
```

**Rust-first**: All CLI/agent code is Rust (~21 crates, ~49K lines). TypeScript is retained only for the Tauri desktop webview.

### Key crates

| Crate | Purpose |
|-------|---------|
| `ava-tui` | CLI/TUI binary — the primary interface |
| `ava-agent` | Agent execution loop, reflection, stuck detection |
| `ava-llm` | LLM providers + connection pooling + circuit breaker |
| `ava-tools` | Tool trait, registry, 19 built-in tools |
| `ava-praxis` | Multi-agent orchestration (Director pattern), workflow pipelines, code review |
| `ava-session` | Session persistence (SQLite + FTS5) |
| `ava-memory` | Persistent memory/recall |
| `ava-permissions` | Command classification, path safety, risk-based approval |
| `ava-context` | Context window management + condensation strategies |
| `ava-codebase` | Code indexing (BM25 + PageRank) |
| `ava-mcp` | MCP (Model Context Protocol) client + config |
| `ava-config` | Configuration + credentials management |
| `ava-sandbox` | Command sandboxing (bwrap/sandbox-exec) |

## Tool Surface (19 built-in)

| Group | Count | Tools |
|-------|------:|-------|
| Core | 11 | read, write, edit, bash, glob, grep, multiedit, apply_patch, test_runner, lint, diagnostics |
| Memory | 3 | remember, recall, memory_search |
| Session | 3 | session_search, session_list, session_load |
| Codebase | 1 | codebase_search |
| Git | 1 | git_read (review subcommand) |

Plus dynamic MCP tools and TOML custom tools (`~/.ava/tools/`, `.ava/tools/`).

## Configuration

```
~/.ava/
├── config.yaml          # Provider, model, and agent settings
├── credentials.json     # API keys per provider
├── mcp.json             # Global MCP server configuration
└── tools/               # Custom TOML tool definitions

.ava/                    # Project-level overrides
├── mcp.json             # Project MCP config (overrides global by server name)
└── tools/               # Project-specific custom tools
```

Provider priority: `--provider/--model` flags > `AVA_PROVIDER`/`AVA_MODEL` env vars > `~/.ava/config.yaml`.

LLM providers (Rust CLI): **Anthropic**, **OpenAI**, **Gemini**, **OpenRouter**, **Ollama** (5 built-in + external via OpenRouter gateway).

## Development Commands

```bash
# Rust (primary)
cargo test --workspace
cargo clippy --workspace
cargo run --bin ava

# Desktop app (TypeScript + Tauri)
npm run tauri dev
npm run lint
npm run format:check
npx tsc --noEmit
npm run test:run
```

## Contributing

1. Check [docs/development/roadmap.md](docs/development/roadmap.md) for current phase
2. Read [CLAUDE.md](CLAUDE.md) for coding conventions
3. Run `cargo test --workspace && cargo clippy --workspace` before committing
4. Commits use [Conventional Commits](https://conventionalcommits.org)

## License

MIT
