# AVA

> The middle ground between minimalist and bloated AI coding assistants.

**Official domains:**
[useava.dev](https://useava.dev) | [avacli.dev](https://avacli.dev) | [tryava.dev](https://tryava.dev) | [ava.engineering](https://ava.engineering)

AVA is lean out of the box (6 tools, clean TUI) but infinitely extensible through plugins. Write plugins in TypeScript, Python, or any language — they run as isolated subprocesses so a broken plugin can never crash AVA.

## Why AVA?

| | Pi | **AVA** | OpenCode |
|---|---|---|---|
| Tools | 3 | **6 default + 8 extended** | 15+ |
| Plugins | DIY | **TypeScript/Python SDK** | npm (in-process, crashy) |
| Plugin isolation | N/A | **Subprocess (safe)** | In-process (unsafe) |
| TUI | Basic | **Full featured** | Full featured |
| Extensibility | High (manual) | **High (easy)** | High (complex) |

## Quick Start

```bash
# Install
curl -fsSL https://raw.githubusercontent.com/ASF-GROUP/AVA/master/install.sh | sh

# Or build from source
git clone https://github.com/ASF-GROUP/AVA.git && cd AVA
cargo build --release --bin ava
cp target/release/ava ~/.local/bin/

# Configure
mkdir -p ~/.ava
echo '{"providers":{"openrouter":{"api_key":"YOUR_KEY"}}}' > ~/.ava/credentials.json

# Run
ava                                          # Interactive TUI
ava "fix the login bug" --headless           # Headless mode
ava review --staged                          # Code review
```

## Plugin System

AVA has a two-tier plugin system:

### Simple: TOML Tools (any language)

Drop a file in `~/.ava/tools/deploy.toml`:

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

The agent can now call `deploy` like any built-in tool.

### Power: Plugins (TypeScript/Python/any language)

```bash
ava plugin init my-plugin              # Scaffold a new plugin
ava plugin add ./my-plugin             # Install it
ava plugin list                        # See what's installed
```

Plugins hook into the agent lifecycle via JSON-RPC:

```typescript
import { createPlugin } from "@ava-ai/plugin";

createPlugin({
  "tool.before": async (ctx, { tool, args }) => {
    if (tool === "read" && args.file_path.includes(".env")) {
      throw new Error("Blocked: don't read .env files");
    }
  },
  "session.start": async (ctx) => {
    console.error("Session started!");
  },
});
```

12 hook types: `auth`, `auth.refresh`, `request.headers`, `tool.before`, `tool.after`, `agent.before`, `agent.after`, `session.start`, `session.end`, `config`, `event`, `shell.env`.

See [docs/plugins.md](docs/plugins.md) for the full guide.

## Architecture

21 Rust crates, ~40K LOC, 1,502 tests. Single binary, no runtime dependencies.

```
ava-tui          CLI/TUI binary (Ratatui)
├── ava-agent    Agent loop + reflection + stuck detection
│   ├── ava-llm      8 LLM providers (Anthropic, OpenAI, Gemini, etc.)
│   ├── ava-tools    6 default + 8 extended tools
│   └── ava-plugin   Power plugin system (JSON-RPC, subprocess isolation)
├── ava-session  SQLite persistence + FTS5 search
├── ava-context  Token tracking + condensation
├── ava-praxis   Multi-agent orchestration
└── ava-config   Configuration + credentials
```

See [docs/architecture/crate-map.md](docs/architecture/crate-map.md) for the full dependency map.

## CLI Reference

```
ava [GOAL] [OPTIONS]
  -m, --model <MODEL>      LLM model
      --provider <NAME>    LLM provider
      --max-turns <N>      Maximum agent turns (default: 20)
      --yolo               Auto-approve all tool executions
      --headless           Non-interactive mode
      --json               NDJSON output (implies headless)
      --multi-agent        Multi-agent mode

ava review [OPTIONS]       Code review
ava plugin [COMMAND]       Plugin management (list/add/remove/info/init)
```

## Tools

| Group | Tools |
|-------|-------|
| Default (6) | `read`, `write`, `edit`, `bash`, `glob`, `grep` |
| Extended (8) | `apply_patch`, `web_fetch`, `web_search`, `multiedit`, `ast_ops`, `lsp_ops`, `code_search`, `git_read` |
| Dynamic | MCP servers + TOML custom tools + plugins |

## Configuration

```
~/.ava/
├── config.yaml          # Settings
├── credentials.json     # API keys
├── mcp.json             # MCP servers
├── tools/               # TOML custom tools
└── plugins/             # Installed plugins
```

## Development

```bash
cargo test --workspace           # 1,502 tests
cargo clippy --workspace         # Zero warnings policy
cargo run --bin ava              # Run from source
```

See [CLAUDE.md](CLAUDE.md) for architecture conventions and [docs/development/](docs/development/) for roadmap and backlog.

## License

MIT
