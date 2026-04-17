<div align="center">

# AVA

**A practical AI coding agent for terminal, desktop, and web.**

[![Rust](https://img.shields.io/badge/Rust-100%25-dea584?style=flat-square&logo=rust)](https://www.rust-lang.org/)
[![License: MIT](https://img.shields.io/badge/license-MIT-yellow?style=flat-square)](LICENSE)

AVA reads code, edits files, runs commands, and helps you finish software work without leaving your repo.

It is designed to sit between OpenCode and PI Code: focused by default, extensible when needed.

<img src="docs/screenshots/main-chat.png" width="700" alt="AVA screenshot" />

</div>

## Get Started

Install from source:

```bash
git clone https://github.com/ASF-GROUP/AVA.git && cd AVA
cargo install --path crates/ava-tui
```

Add credentials:

```bash
ava auth login openrouter
```

Security note:

1. Prefer AVA's connect flow, environment variables, or keychain-backed credential storage.
2. Avoid manually editing `~/.ava/credentials.json` unless you intentionally want plaintext local storage.

Run AVA:

```bash
ava                          # TUI
ava "fix the login bug"      # headless
ava serve                    # web mode
```

## Core AVA

- Solo-first coding agent
- 9 default built-in tools: `read`, `write`, `edit`, `bash`, `glob`, `grep`, `web_fetch`, `web_search`, `git_read`
- Works in TUI, desktop, and web
- MCP support
- Commands and Skills support
- Stable plugin architecture for optional advanced capability
- Session persistence and safety features for real repo work

## Official Providers

AVA 3.3 supports a smaller set of officially supported providers that are actively tested and tuned:

1. Anthropic
2. OpenAI
3. Google Gemini
4. Ollama
5. OpenRouter
6. GitHub Copilot
7. Inception
8. Alibaba
9. ZAI / ZhipuAI
10. Kimi
11. MiniMax

Provider variants should appear as routing or region options inside a provider, not as separate providers.

## Customization

Default visible customization is centered on:

1. MCPs
2. Commands
3. Skills

Plugins are a core part of AVA's identity, but plugin-owned UI and settings should only appear when installed.

## Configuration

```text
~/.ava/
├── credentials.json     # API keys
├── config.yaml          # core settings
├── mcp.json             # MCP servers
├── tools/               # custom tool definitions
├── themes/              # custom themes
└── AGENTS.md            # global instructions
```

## Documentation

1. [docs/README.md](docs/README.md) - documentation index by section and audience
2. [AGENTS.md](AGENTS.md) - source of truth for repo workflow, conventions, and architecture
3. [docs/project/roadmap.md](docs/project/roadmap.md) - current product direction
4. [docs/project/backlog.md](docs/project/backlog.md) - pending work only
5. [docs/testing/desktop-testing.md](docs/testing/desktop-testing.md) - practical desktop regression workflow
6. [docs/testing/README.md](docs/testing/README.md) - testing and verification entrypoint
7. [docs/architecture/README.md](docs/architecture/README.md) - architecture entrypoint, capability audits, and transition docs
8. [docs/architecture/agent-backend-capability-audit-m1.md](docs/architecture/agent-backend-capability-audit-m1.md) - current coding-agent backend capability inventory
9. [docs/architecture/agent-backend-capability-comparison-m2.md](docs/architecture/agent-backend-capability-comparison-m2.md) - comparison matrix for AVA vs external coding-agent backends
10. [docs/architecture/cross-surface-runtime-map-m4.md](docs/architecture/cross-surface-runtime-map-m4.md) - runtime wiring map across interactive TUI, headless CLI, desktop, and web
11. [docs/architecture/cross-surface-behavior-audit-m5.md](docs/architecture/cross-surface-behavior-audit-m5.md) - shared-vs-divergent backend behavior audit across surfaces
12. [docs/architecture/shared-backend-contract-m6.md](docs/architecture/shared-backend-contract-m6.md) - canonical shared-backend contract for cross-surface semantics
13. [docs/architecture/backend-correction-roadmap-m7.md](docs/architecture/backend-correction-roadmap-m7.md) - implementation roadmap for backend contract adoption
14. [docs/architecture/backend-contract-exceptions.md](docs/architecture/backend-contract-exceptions.md) - versioned backend-contract exception registry
15. [docs/architecture/crate-map.md](docs/architecture/crate-map.md) - current crate and dependency map
16. [docs/extend/README.md](docs/extend/README.md) - extension and customization overview
17. [docs/reference/providers-and-auth.md](docs/reference/providers-and-auth.md) - provider IDs, aliases, and auth behavior
18. [docs/reference/commands.md](docs/reference/commands.md) - slash commands and CLI surfaces
19. [docs/reference/credential-storage.md](docs/reference/credential-storage.md) - credential storage and security guidance
20. [CLAUDE.md](CLAUDE.md) - compatibility entrypoint pointing back to the active docs

## Contributing

```bash
just check
```

## License

[MIT](LICENSE)
