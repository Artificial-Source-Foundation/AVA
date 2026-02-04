# Estela

> Multi-Agent AI Coding Assistant (~19,100 lines)

A Tauri 2.0 + SolidJS desktop application for AI-assisted software development with **ACP (Agent Client Protocol)** support for editor integration. Features autonomous agent loop, hierarchical **Commander + Workers + Validator** architecture, 15 tools, and long-term memory.

## Quick Start

```bash
# Prerequisites: Node.js 20+, pnpm 10+, Rust 1.70+

# Clone and install
git clone https://github.com/g0dxn4/Estela.git
cd Estela
pnpm install

# Set up environment (optional - can also configure in Settings UI)
cp .env.example .env
# Edit .env with your API keys

# Run desktop app
npm run tauri dev

# Or build the CLI for ACP mode
pnpm build:all
node cli/dist/index.js --help
```

## Running Modes

### Desktop App (Tauri)
Full-featured desktop application with UI:
```bash
npm run tauri dev
```

### CLI with ACP (for Toad/Zed)
Run as an ACP-compatible agent for editor integration:
```bash
pnpm build:all
node cli/dist/index.js --acp
```

## Current Status

**All core epics (1-17) complete!** Ready for frontend integration (Epic 18).

| Phase | Epics | Status |
|-------|-------|--------|
| Foundation | 1-3: Chat, File Tools, ACP Monorepo | ✅ Complete |
| Infrastructure | 4-7: Safety, Context, DX, MCP | ✅ Complete |
| Agent System | 8-12: Agent Loop, Commander, Parallel, Validator, Codebase | ✅ Complete |
| Settings & Memory | 13-14: Config, Long-term Memory | ✅ Complete |
| Enhancement | 15-17: Comparison, OpenCode Features, Missing Tools | ✅ Complete |
| **Next** | 18: Tauri Desktop GUI | ⬜ Planned |

See [docs/ROADMAP.md](docs/ROADMAP.md) for full roadmap.

## Architecture

```
                    ┌─────────────┐
                    │  Commander  │  Strategic Planning
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
        ┌──────────┐ ┌──────────┐ ┌──────────┐
        │ Operator │ │ Operator │ │ Operator │  Parallel Execution
        │ (file A) │ │ (file B) │ │ (file C) │
        └────┬─────┘ └────┬─────┘ └────┬─────┘
              └────────────┼────────────┘
                           ▼
                    ┌─────────────┐
                    │  Validator  │  Quality Gate
                    └─────────────┘
```

## Tech Stack

| Layer | Technology |
|-------|------------|
| Desktop | Tauri 2.0 (Rust) - 3-10MB app, 30-40MB RAM |
| Frontend | SolidJS + TypeScript (~7KB runtime) |
| Styling | Tailwind CSS v4 |
| Database | SQLite via tauri-plugin-sql |
| Tools | File ops via tauri-plugin-fs, shell via tauri-plugin-shell |
| CLI | Node.js with ACP SDK |

## Project Structure

```
packages/
├── core/              # Business logic (~15,400 lines)
│   └── src/
│       ├── agent/     # Autonomous loop, subagents, recovery
│       ├── commander/ # Hierarchical delegation, workers
│       │   └── parallel/  # Concurrent execution, DAG scheduler
│       ├── tools/     # 15 tools (file, web, task)
│       ├── context/   # Token tracking, compaction
│       ├── memory/    # Episodic, semantic, procedural
│       ├── validator/ # QA pipeline (syntax, types, lint)
│       ├── codebase/  # Indexer, PageRank, repo map
│       ├── config/    # Settings, credentials
│       ├── permissions/   # Safety, risk assessment
│       ├── session/   # State, checkpoints, forking
│       ├── mcp/       # MCP protocol client
│       └── ...        # diff, git, question, instructions, scheduler
├── platform-tauri/    # Tauri implementations
└── platform-node/     # Node.js implementations

cli/                   # CLI with ACP agent
src/                   # Tauri SolidJS frontend (~3,700 lines)
docs/                  # Documentation & memory bank
```

## Development Commands

```bash
# Desktop app
npm run tauri dev

# Build all packages (for CLI)
pnpm build:all

# Run CLI
node cli/dist/index.js --help
node cli/dist/index.js --acp

# Code quality
npm run lint          # Oxlint + ESLint
npm run lint:fix      # Auto-fix
npm run format        # Biome format
npm run typecheck     # TypeScript check

# Testing
npm run test          # Vitest watch
npm run test:run      # Single run

# Maintenance
npm run knip          # Find dead code
npm run knip:fix      # Remove dead code
npm run analyze       # Bundle size
```

## Configuration

API keys can be set via:
1. **Settings UI** in the desktop app (stored in localStorage)
2. **Environment variables** (see `.env.example`)
3. **~/.estela/credentials.json** for CLI mode

Supported providers:
- **Anthropic** - Direct API
- **OpenRouter** - Gateway to multiple models

## Contributing

1. Check [docs/ROADMAP.md](docs/ROADMAP.md) for current epic
2. Read [CLAUDE.md](CLAUDE.md) for coding conventions
3. Run `npm run lint && npm run typecheck` before committing
4. Commits use [Conventional Commits](https://conventionalcommits.org)

## License

MIT
