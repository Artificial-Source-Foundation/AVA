<div align="center">

# AVA

**Your AI dev team in one app.**

[![Rust](https://img.shields.io/badge/Rust-100%25-dea584?style=flat-square&logo=rust)](https://www.rust-lang.org/)
[![License: MIT](https://img.shields.io/badge/license-MIT-yellow?style=flat-square)](LICENSE)

AVA is an AI coding agent that lives in your terminal, your browser, and your desktop.
Give it a task in plain English — it reads your code, makes changes, runs commands, and
gets the job done. One binary. No runtime dependencies.

<img src="docs/screenshots/main-chat.png" width="700" alt="AVA TUI screenshot" />

</div>

## Get started

**Install from source:**

```bash
git clone https://github.com/ASF-GROUP/AVA.git && cd AVA
cargo install --path crates/ava-tui
```

**Add your API key:**

```bash
ava --connect openrouter   # interactive setup
# or manually: ~/.ava/credentials.json
```

**Run:**

```bash
ava                          # terminal UI
ava "fix the login bug"      # headless mode
ava serve                    # web browser UI
```

## Features

- **15 LLM providers** — Anthropic, OpenAI, Gemini, Ollama, OpenRouter, Copilot, Inception, Alibaba, ZAI, ZhipuAI, Kimi, MiniMax, and more
- **9 built-in tools** — read, write, edit, bash, glob, grep, web_fetch, web_search, git_read
- **Multi-agent mode** — a Director assembles Leads and Workers to tackle complex tasks
- **MCP support** — connect any MCP server for extra tools
- **Plugin system** — extend with TOML tools, JSON-RPC plugins, or custom hooks
- **Mid-stream messaging** — steer, follow up, or queue tasks while the agent works
- **Session persistence** — pick up where you left off, with crash recovery
- **29 themes** — or bring your own via TOML
- **Runs everywhere** — TUI, desktop app (Tauri), or web browser

## Providers

| Provider | Auth | Models |
|----------|------|--------|
| Anthropic | API key | Claude 4, Sonnet, Haiku |
| OpenAI | OAuth or API key | GPT-5.4, o3, o4-mini |
| Google Gemini | OAuth or API key | Gemini 2.5 Pro, Flash |
| Ollama | Local (no key) | Llama, Mistral, etc. |
| OpenRouter | API key | 100+ models |
| GitHub Copilot | GitHub OAuth | GPT-5.4 |
| Inception | API key | Mercury 2 |
| Alibaba | API key | Qwen 3.5 Plus |
| ZAI / ZhipuAI | API key | GLM-4.7 |
| Kimi | API key | K2P5 |
| MiniMax | API key | MiniMax-M2 |

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| `Enter` | Send message / steer running agent |
| `Alt+Enter` | Queue follow-up for after current task |
| `Ctrl+L` | Session picker |
| `Ctrl+M` | Switch model |
| `Ctrl+Y` | Copy last response |
| `Ctrl+?` | Show all shortcuts |

## Configuration

```
~/.ava/
├── credentials.json     # API keys
├── config.yaml          # Settings
├── mcp.json             # MCP servers
├── tools/               # Custom TOML tools
├── themes/              # Custom themes
└── AGENTS.md            # Global instructions
```

## Documentation

- [Full docs](docs/) — architecture, crate map, plugin guide
- [CLAUDE.md](CLAUDE.md) — contributor conventions and project structure
- [Changelog](docs/development/CHANGELOG.md)

## Contributing

```bash
just check    # fmt + clippy + test (all-in-one)
```

See [CLAUDE.md](CLAUDE.md) for architecture details and development workflows.

## License

[MIT](LICENSE)
