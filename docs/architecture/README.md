# Architecture

> AVA system design — Rust-first AI coding assistant with desktop and CLI interfaces
>
> **Last verified**: 2026-03-08

---

## Overview

AVA uses a **pure Rust backend** for both CLI and desktop:

```
AVA/
├── crates/                   # Rust crates — ALL backend logic (~22 crates)
│   ├── ava-tui/              # CLI/TUI binary (Ratatui + Crossterm)
│   ├── ava-agent/            # Agent execution loop
│   ├── ava-llm/              # LLM providers (7 built-in + mock)
│   ├── ava-tools/            # Tool trait + registry (6 built-in + 8 extended tools)
│   ├── ava-praxis/           # Multi-agent orchestration (Praxis)
│   └── ...                   # 17 more crates (session, memory, config, etc.)
├── src/                      # Desktop frontend (SolidJS)
├── src-tauri/                # Desktop native host (Tauri 2.0) — IPC to Rust crates
└── docs/
    ├── architecture/         # System design docs (this directory)
    ├── development/          # Roadmap, sprints, epics
    └── research/             # Competitive analysis
```

### Data Flow

```
SolidJS (src/) → Tauri IPC → Rust commands (src-tauri/) → Rust crates (crates/)
```

The CLI (`crates/ava-tui/`) calls Rust crates directly with no IPC layer.

---

## Documentation Index

### Core Architecture

| Document | Description |
|----------|-------------|
| [architecture-guide.md](./architecture-guide.md) | Backend architecture: system boundaries, execution flow, middleware |
| [backend.md](./backend.md) | Backend overview: Rust crates |
| [modules.md](./modules.md) | Module organization: Rust crates + SolidJS frontend |
| [data-flow.md](./data-flow.md) | Desktop data flow (turn lifecycle, hooks) |
| [praxis.md](./praxis.md) | Desktop agent hierarchy (Director → Leads → Workers) |
| [database-schema.md](./database-schema.md) | SQLite schema for session storage |

### Architecture Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| CLI framework | Rust (Ratatui) | Native performance, single binary, no Node.js |
| Desktop framework | Tauri 2.0 | ~5MB binary, 30MB RAM, Rust security |
| Frontend | SolidJS | Fine-grained reactivity for streaming |
| Agents | Workers as tools | Simple, unified delegation pattern |
| Tools | Platform-agnostic | Single impl for Tauri + CLI |
| Edits | Fuzzy strategies | Handles LLM whitespace/indent errors |

---

## The Dev Team Flow (Praxis)

```
User types request
    │
    ▼
Director (AgentExecutor)
    │ Analyzes task, creates plan
    │
    ├─→ Frontend Lead (Lead agent)
    │   ├─→ Coder Worker → writes components
    │   └─→ Tester Worker → writes tests
    │
    ├─→ Backend Lead (Lead agent)
    │   ├─→ Coder Worker → writes API routes
    │   └─→ Debugger Worker → fixes issues
    │
    └─→ QA Lead (Lead agent)
        └─→ Reviewer Worker → code review

Director aggregates results and presents to user
```

The Rust CLI uses Director with workflow pipelines for multi-agent orchestration.

---

## Tool Surface

| Group | Count | Tools |
|-------|------:|-------|
| Built-in | 6 | read, write, edit, bash, glob, grep |
| Extended | 7 | apply_patch, web_fetch, multiedit, test_runner, lint, diagnostics, git |

Additional runtime helpers such as `task`, `question`, and todo tools are registered separately. Dynamic MCP tools and TOML custom tools remain supported.

---

## Related Documentation

- `CLAUDE.md` — Primary architecture reference (repo root)
- `docs/development/roadmap.md` — Current roadmap
- `docs/development/sprints/` — Sprint prompts
- `docs/troubleshooting/` — Debugging guides

---

## Archives

Historical documents moved to `docs/archives/architecture/`:
- `backlog.md` — TypeScript-era backlog (merged case conflict)
- `changelog.md` — Development history (packages/core/)
- `gap-analysis.md` — Competitive analysis (TypeScript era)
- `test-coverage.md` — Test metrics (TypeScript era)
- `components.md` — Old component breakdown (packages/core/)
- `backlog-providers.md` — Provider implementation backlog
- `backlog-skills-rules.md` — Skills/rules backlog
