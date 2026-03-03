# AVA

> The Obsidian of AI Coding — Desktop AI coding app with a virtual dev team and community plugins

A Tauri 2.0 + SolidJS desktop application for AI-assisted software development. AVA now runs a dual-stack backend during migration: the original `packages/core` monolith and the new extension-first `packages/core-v2` + `packages/extensions` stack. It includes autonomous agent execution, hierarchical **Team Lead + Senior Leads + Junior Devs + Validator** coordination, 55+ tools, and 16 LLM providers.

## Quick Start

```bash
# Prerequisites: Node.js 20+, pnpm 10+, Rust toolchain

# Clone and install
git clone https://github.com/g0dxn4/AVA.git
cd AVA
pnpm install

# Optional: configure providers via .env
cp .env.example .env

# Run desktop app
npm run tauri dev

# Build all packages + CLI
pnpm build:all

# Run CLI
node cli/dist/index.js --help
```

## Current Status

AVA is in active migration from the original core to an extension-first architecture.

- `packages/core` remains the production baseline for desktop and CLI workflows.
- `packages/core-v2` provides the new minimal core runtime.
- `packages/extensions` contains built-in features (providers, tools, permissions, prompts, context, validator, commander, git, memory, and more) implemented as extensions.

See [docs/ROADMAP.md](docs/ROADMAP.md) for the current execution plan.

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
src/                       # Tauri + SolidJS desktop app (primary)
src-tauri/                 # Rust backend
cli/                       # CLI interface (secondary)
packages/
├── core/                  # Original backend (current baseline)
├── core-v2/               # New minimal core (agent loop, tools, session, extension API)
├── extensions/            # Built-in extensions (providers, tools, modes, validator, etc.)
├── platform-node/         # Node.js platform implementations
└── platform-tauri/        # Tauri platform implementations
docs/                      # Product, architecture, and implementation docs
```

### Backend Architecture (Dual Stack)

- `packages/core`: legacy monolith currently powering most production behavior.
- `packages/core-v2`: minimal runtime core (agent, tools, extensions, session, config).
- `packages/extensions`: extension modules that provide most features previously embedded in core.

## Development Commands

```bash
# Desktop app
npm run tauri dev

# Build packages only
pnpm build:packages

# Build + run CLI
pnpm build:cli
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
3. **~/.ava/credentials.json** for CLI mode

Supported providers (16):
- **Anthropic** — Direct API or OAuth
- **OpenAI** — Direct API or OAuth
- **Google** — Direct API or OAuth
- **OpenRouter** — Gateway to multiple models
- **Azure OpenAI** — Direct API
- **Mistral, Groq, DeepSeek, xAI, Cohere, Together, LiteLLM** — Direct APIs
- **GLM, Kimi, Vertex** — Direct APIs
- **Ollama** — Local models

## Tooling Snapshot

- Core-v2 ships the foundational tool set (`read`, `write`, `edit`, `bash`, `glob`, `grep`, `pty`).
- Extensions add 40+ more capabilities (create/delete/apply_patch/multiedit, web tools, task/subagents, git/PR, memory, LSP, planning, and more).
- Combined tool surface is 55+ tools across desktop + CLI agent workflows.

## Contributing

1. Check [docs/ROADMAP.md](docs/ROADMAP.md) for current phase
2. Read [CLAUDE.md](CLAUDE.md) for coding conventions
3. Run `npm run lint && npx tsc --noEmit` before committing
4. Commits use [Conventional Commits](https://conventionalcommits.org)

## License

MIT
