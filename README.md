# Estela

> Multi-Agent AI Coding Assistant

A Tauri 2.0 + SolidJS desktop application for AI-assisted software development. Currently implements streaming chat with tool use; working toward a hierarchical **Commander + Operators + Validator** architecture.

## Quick Start

```bash
# Prerequisites: Node.js 20+, Rust 1.70+

# Clone and install
git clone https://github.com/g0dxn4/Estela.git
cd Estela
npm install

# Set up environment (optional - can also configure in Settings UI)
cp .env.example .env
# Edit .env with your API keys

# Run development
npm run tauri dev
```

## Current Status

| Epic | Goal | Status |
|------|------|--------|
| 1. Chat | Streaming chat with multi-provider LLM | ✅ Complete |
| 2. File Tools | Read, write, edit, glob, grep, bash | ✅ Complete |
| 3. Tool Use | LLM function calling loop | 🟡 In Progress |
| 4+ | Single Agent → Commander → Parallel → Validator | ⬜ Planned |

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

## Development Commands

```bash
# Run app
npm run tauri dev

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

## Project Structure

```
src/
├── components/       # UI components (chat, layout, sessions, settings)
├── config/          # Constants, environment
├── hooks/           # SolidJS hooks (useChat)
├── services/        # Core services
│   ├── auth/        # Credential management
│   ├── llm/         # LLM clients (Anthropic, OpenRouter)
│   └── tools/       # File tools, bash, registry
├── stores/          # State management (sessions)
└── types/           # TypeScript types

docs/
├── ROADMAP.md       # Epic overview
├── VISION.md        # Project vision
├── development/     # Sprint planning
│   ├── epics/       # Epic details
│   └── completed/   # Done sprints
├── architecture/    # System design
└── reference-code/  # SOTA examples (OpenCode, Gemini CLI)
```

## Configuration

API keys can be set via:
1. **Settings UI** in the app (stored in localStorage)
2. **Environment variables** (see `.env.example`)

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
