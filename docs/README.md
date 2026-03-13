# AVA Documentation

> Last updated: 2026-03-13

**Official sites:**
[useava.dev](https://useava.dev) | [avacli.dev](https://avacli.dev) | [tryava.dev](https://tryava.dev) | [ava.engineering](https://ava.engineering)

## Quick Start

| Document | Purpose |
|----------|---------|
| [CLAUDE.md](../CLAUDE.md) | **Primary reference** — architecture, conventions, commands |
| [AGENTS.md](../AGENTS.md) | AI agent instructions for working on AVA |

## Directory Index

```
docs/
├── architecture/       # How AVA is built (modules, data flow, backend, praxis)
│   └── audits/         # Structural, coverage, and maintainability audits
├── development/        # Active work (roadmap, sprints, benchmarks, research)
│   ├── roadmap.md      # Sprint roadmap (11-66+), v3 lanes, and status
│   ├── backlog.md      # Active backlog and validation status
│   ├── epics.md        # Completed and planned backend/frontend epics
│   ├── v3-plan.md      # Paired backend + UX delivery plan toward v3
│   ├── test-matrix.md  # E2E matrix for tiered tools, modes, and providers
│   ├── sprints/        # Active sprint prompts
│   │   └── archive/    # Implemented sprint docs
│   ├── benchmarks/     # Performance data and bug reports
│   └── research/       # Competitor analysis and audits
├── plans/              # Execution plans and longer-form implementation plans
├── reference-code/     # Competitor source code notes (12 projects)
├── troubleshooting/    # Common issues and fixes
└── archives/           # Completed sprints, epics, historical snapshots, old docs
```

## Key Documents

### Development (Active)
- [**Roadmap**](development/roadmap.md) — Sprint roadmap with phases, dependencies, milestones
- [**Backlog**](development/backlog.md) — Active backlog, validation queue, and execution order
- [**Dev Tooling Setup**](development/dev-tooling.md) — Local verification workflow, optional Rust tools, and hooks
- [**Epics**](development/epics.md) — Completed epics plus planned v3 backend/frontend lanes
- [**v3 Plan**](development/v3-plan.md) — Paired backend and UX plan toward v3
- [**E2E Test Matrix**](development/test-matrix.md) — Tiered tool, mode, and provider verification notes
- [**Benchmark Report**](development/benchmarks/benchmark-2026-03.md) — AVA vs OpenCode performance
- [**Benchmark Discord Drafts**](development/benchmarks/discord-model-breakdown-2026-03-12.md) — Social copy and stats blocks for new model releases
- [**Bug Backlog**](development/benchmarks/sprint-32-bugs.md) — Known bugs and TUI gaps
- [**Competitive Analysis (Rust)**](development/research/rust-competitive-analysis-2026-03.md) — 12-competitor architecture analysis
- [**Benchmark Hardening Plan**](plans/2026-03-12-benchmark-hardening-and-transparency.md) — Benchmark credibility, isolation, and reporting plan

### Sprints
- See also: [Epics](development/epics.md) for the higher-level grouping of active sprint work.
- [Sprint 63](development/sprints/sprint-63/overview.md) — Execution and ecosystem foundations
- [Sprint 63 Execution Checklist](development/sprints/sprint-63/execution-checklist.md) — Agent-ready implementation order, verification, and risks
- [Sprint 64](development/sprints/sprint-64/overview.md) — Knowledge and context foundations
- [Sprint 64 Execution Checklist](development/sprints/sprint-64/execution-checklist.md) — Agent-ready implementation order, verification, and risks
- [Sprint 65](development/sprints/sprint-65/overview.md) — Agent coordination backend
- [Sprint 65 Execution Checklist](development/sprints/sprint-65/execution-checklist.md) — Agent-ready implementation order, verification, and risks
- [Sprint 66](development/sprints/sprint-66/overview.md) — Optional capability backends
- [Sprint 66 Execution Checklist](development/sprints/sprint-66/execution-checklist.md) — Agent-ready implementation order, verification, and risks
- [Sprint 62 (Archived)](development/sprints/archive/sprint-62/overview.md) — Cost and runtime foundations
- [Sprint 62V (Archived)](development/sprints/archive/sprint-62v/overview.md) — Validation and archive closeout
- [Archived Sprints](development/sprints/archive/) — Implemented sprint docs for Sprint 53 through Sprint 62V

### Architecture
- [**Architecture Guide**](architecture/architecture-guide.md) — System architecture overview
- [**Data Flow**](architecture/data-flow.md) — Request/response flow
- [**Praxis**](architecture/praxis.md) — Multi-agent orchestration design
- [**Modules**](architecture/modules.md) — Crate-level module overview
- [**Backend**](architecture/backend.md) — Backend architecture details
- [**Execution Backend Boundary**](architecture/execution-backend-boundary.md) — Platform/tool execution contract for pluggable backends
- [**Repository Layout**](architecture/repository-layout.md) — Root organization and hygiene rules
- [**Test Coverage Audit**](architecture/audits/test-coverage-audit.md) — Rust crate test coverage snapshot
- [**Documentation Coverage Audit**](architecture/audits/doc-coverage-audit.md) — Public API documentation coverage snapshot

### Reference
- [**Reference Code**](reference-code/) — Notes on 12 competitor codebases
- [**Troubleshooting**](troubleshooting/) — Common issues and fixes
- [**Scripts Guide**](../scripts/README.md) — Script categories and key entrypoints
