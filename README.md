# AVA

> The Obsidian of AI Coding — Desktop AI coding app with a virtual dev team and community plugins

A Tauri 2.0 + SolidJS desktop application for AI-assisted software development. Features autonomous agent loop, hierarchical **Team Lead + Senior Leads + Junior Devs + Validator** architecture, 22 tools, multi-provider LLM support (14 providers), and long-term memory.

## Quick Start

```bash
# Prerequisites: Node.js 20+, Rust 1.70+

# Clone and install
git clone https://github.com/g0dxn4/AVA.git
cd AVA
npm install

# Set up environment (optional - can also configure in Settings UI)
cp .env.example .env
# Edit .env with your API keys

# Run desktop app
npm run tauri dev

# Or build CLI
npm run build:packages && npm run build:cli
node cli/dist/index.js --help
```

## Current Status

**Phase 1 (Desktop App) and Phase 1.5 (Polish) complete.** Ready for Phase 2 (Plugin Ecosystem).

| Phase | Status |
|-------|--------|
| Foundation (Epics 1-3) | Done |
| Infrastructure (Epics 4-7) | Done |
| Agent System (Epics 8-14) | Done |
| Enhancement (Epics 15-21) | Done |
| ACP/A2A Protocols (Epics 25-26) | Done |
| **Phase 1: Desktop App** | **Done** |
| **Phase 1.5: Polish** | **Done** |
| Phase 2: Plugin Ecosystem | Next |

See [docs/ROADMAP.md](docs/ROADMAP.md) for full roadmap.

## Architecture

```
                    ┌─────────────┐
                    │  Team Lead  │  Strategic Planning
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
        ┌──────────┐ ┌──────────┐ ┌──────────┐
        │ Sr. Lead │ │ Sr. Lead │ │ Sr. Lead │  Domain Specialists
        │(Frontend)│ │(Backend) │ │(Testing) │
        └────┬─────┘ └────┬─────┘ └────┬─────┘
              │            │            │
         Jr. Devs     Jr. Devs     Validator
        (file-level)  (file-level)  (QA Gate)
```

## Tech Stack

| Layer | Technology |
|-------|------------|
| Desktop | Tauri 2.0 (Rust) — ~5MB app, 30MB RAM |
| Frontend | SolidJS + TypeScript (~7KB runtime) |
| Styling | Tailwind CSS v4 |
| Database | SQLite via tauri-plugin-sql |
| Tools | File ops via tauri-plugin-fs, shell via tauri-plugin-shell |
| CLI | Node.js (secondary interface) |

## Project Structure

```
packages/
├── core/              # Business logic (~56,500 lines, 1778 tests)
│   └── src/
│       ├── agent/     # Autonomous loop, subagents, recovery
│       ├── commander/ # Team Lead → Senior Leads → Junior Devs
│       │   └── parallel/  # Concurrent execution, DAG scheduler
│       ├── tools/     # 22 tools (file, web, task, browser, plan)
│       ├── context/   # Token tracking, compaction
│       ├── memory/    # Episodic, semantic, procedural
│       ├── validator/ # QA pipeline (syntax, types, lint)
│       ├── codebase/  # Indexer, PageRank, repo map
│       ├── config/    # Settings, credentials
│       ├── permissions/   # Safety, risk assessment
│       ├── session/   # State, checkpoints, forking
│       ├── mcp/       # MCP protocol client
│       └── ...        # diff, git, hooks, extensions, bus, auth, llm
├── platform-tauri/    # Tauri implementations
└── platform-node/     # Node.js implementations

cli/                   # CLI interface (secondary)
src/                   # Tauri SolidJS frontend (~16,000+ lines)
docs/                  # Documentation & memory bank
```

## Development Commands

```bash
# Desktop app
npm run tauri dev

# Build packages (required before CLI)
npm run build:packages

# Build + run CLI
npm run build:cli
node cli/dist/index.js --help

# Code quality
npm run lint          # Oxlint + ESLint
npm run format        # Biome format
npx tsc --noEmit      # Type check

# Testing
npm run test          # Vitest watch
npm run test:run      # Single run

# Maintenance
npm run knip          # Find dead code
npm run analyze       # Bundle size
```

## Configuration

API keys can be set via:
1. **Settings UI** in the desktop app (stored in localStorage)
2. **Environment variables** (see `.env.example`)
3. **~/.estela/credentials.json** for CLI mode

Supported providers:
- **Anthropic** — Direct API or OAuth
- **OpenAI** — Direct API or OAuth
- **Google** — Direct API or OAuth
- **OpenRouter** — Gateway to multiple models
- **Mistral, Groq, DeepSeek, xAI, Cohere, Together** — Direct APIs
- **GLM, Kimi** — Direct APIs
- **Ollama** — Local models

## Contributing

1. Check [docs/ROADMAP.md](docs/ROADMAP.md) for current phase
2. Read [CLAUDE.md](CLAUDE.md) for coding conventions
3. Run `npm run lint && npx tsc --noEmit` before committing
4. Commits use [Conventional Commits](https://conventionalcommits.org)

## License

MIT
