# Estela

> Multi-Agent AI Coding Assistant

A Tauri 2.0 + SolidJS desktop application implementing a hierarchical **Commander + Operators + Validator** architecture for complex software engineering tasks.

## Features

- **Commander Agent**: Lead planner and orchestrator (never writes code)
- **Parallel Operators**: Multiple task executors working on different files simultaneously
- **Validator Gate**: QA checkpoint before task completion
- **Streaming UI**: Real-time LLM response rendering with SolidJS
- **Local Database**: SQLite for session and message persistence
- **LSP Integration**: Rust-based language server client for code intelligence

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

| Layer | Technology | Why |
|-------|------------|-----|
| **Desktop** | Tauri 2.0 (Rust) | 3-10MB app size, 30-40MB RAM, sandboxed security |
| **Frontend** | SolidJS + TypeScript | Fine-grained reactivity for streaming (~7KB runtime) |
| **Styling** | Tailwind CSS | Rapid UI development |
| **Database** | SQLite | Local persistence via tauri-plugin-sql |
| **LSP** | Rust client | Multi-language code intelligence |

## Development

```bash
# Prerequisites
# - Node.js 20+
# - Rust 1.70+
# - Tauri CLI

# Create the project
npm create tauri-app@latest estela -- --template solid-ts

# Install dependencies
cd estela
npm install

# Run development server
npm run tauri dev

# Build for production
npm run tauri build
```

## Documentation

| Document | Description |
|----------|-------------|
| [docs/VISION.md](docs/VISION.md) | Project vision and roadmap |
| [docs/BACKLOG.md](docs/BACKLOG.md) | Current development tasks |
| [docs/architecture/](docs/architecture/) | System design |
| [docs/agents/](docs/agents/) | Agent specifications |
| [CLAUDE.md](CLAUDE.md) | AI assistant instructions |

## Project Status

- **Phase 0**: Planning - Complete
- **Phase 1**: Foundation - Ready to start
- **Phase 2-7**: Planned

See [docs/BACKLOG.md](docs/BACKLOG.md) for detailed progress tracking.

## Previous Work

This project evolved from an OpenCode plugin called "Delta9" implementing a similar multi-agent architecture. The plugin-era documentation is preserved in `docs/archive/opencode-plugin-era/` for reference.

## License

MIT
