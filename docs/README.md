# AVA Documentation

> Last updated: 2026-03-08

## Quick Start

| Document | Purpose |
|----------|---------|
| [CLAUDE.md](../CLAUDE.md) | **Primary reference** — architecture, conventions, commands |
| [AGENTS.md](../AGENTS.md) | AI agent instructions for working on AVA |

## Directory Index

```
docs/
├── architecture/       # How AVA is built (modules, data flow, backend, praxis)
├── development/        # Active work (roadmap, sprints, benchmarks, research)
│   ├── roadmap.md      # Sprint 11-50+ roadmap with phases and milestones
│   ├── test-matrix.md  # E2E test matrix (19 tools, 5 modes, 3 providers)
│   ├── sprints/        # Sprint prompts
│   ├── benchmarks/     # Performance data and bug reports
│   └── research/       # Competitor analysis and audits
├── reference-code/     # Competitor source code notes (12 projects)
├── troubleshooting/    # Common issues and fixes
└── archives/           # Completed sprints, epics, old docs (historical only)
```

## Key Documents

### Development (Active)
- [**Roadmap**](development/roadmap.md) — Sprint roadmap with phases, dependencies, milestones
- [**E2E Test Matrix**](development/test-matrix.md) — 19 tools, 5 modes, 3 providers verified
- [**Benchmark Report**](development/benchmarks/benchmark-2026-03.md) — AVA vs OpenCode performance
- [**Bug Backlog**](development/benchmarks/sprint-32-bugs.md) — Known bugs and TUI gaps
- [**Competitive Analysis (Rust)**](development/research/rust-competitive-analysis-2026-03.md) — 12-competitor architecture analysis

### Sprints
- [Sprint 100](development/sprints/sprint-100-v2.1-release.md) — v2.1 release polish (current)

### Architecture
- [**Architecture Guide**](architecture/architecture-guide.md) — System architecture overview
- [**Data Flow**](architecture/data-flow.md) — Request/response flow
- [**Praxis**](architecture/praxis.md) — Multi-agent orchestration design
- [**Modules**](architecture/modules.md) — Crate-level module overview
- [**Backend**](architecture/backend.md) — Backend architecture details

### Reference
- [**Reference Code**](reference-code/) — Notes on 12 competitor codebases
- [**Troubleshooting**](troubleshooting/) — Common issues and fixes
