# AVA Documentation

> Single source of truth for architecture, development, and troubleshooting.

<!-- Last verified: 2026-03-05. Run 'npm run test:run && cargo test --workspace' to revalidate. -->

## Quick Start

| Document | Purpose |
|----------|---------|
| [CLAUDE.md](../CLAUDE.md) | **Primary reference** — architecture, conventions, quick commands |
| [troubleshooting.md](troubleshooting.md) | Common issues and solutions |
| [backend.md](backend.md) | Backend runtime overview |

## Architecture

| Document | Description |
|----------|-------------|
| [architecture/README.md](architecture/README.md) | System components and data flow |
| [backend/architecture-guide.md](backend/architecture-guide.md) | Detailed backend architecture |
| [backend.md](backend.md) | Backend runtime model and extension index |
| [CLAUDE.md](../CLAUDE.md) | Primary architecture reference (single source of truth) |

## Development

| Document | Description |
|----------|-------------|
| [development/status/current-focus.md](development/status/current-focus.md) | Current development priorities |
| [plugins/PLUGIN_SDK.md](plugins/PLUGIN_SDK.md) | Extension/plugin development guide |
| [plugins/PLUGIN_TEMPLATE.md](plugins/PLUGIN_TEMPLATE.md) | Plugin template reference |

## Backend Reference

| Document | Description |
|----------|-------------|
| [backend.md](backend.md) | Backend overview (~20 extensions, ~41 tools) |
| [backend/modules.md](backend/modules.md) | Module organization |
| [backend/test-coverage.md](backend/test-coverage.md) | Test coverage summary |

## Frontend Reference

| Document | Description |
|----------|-------------|
| [frontend/README.md](frontend/README.md) | Frontend architecture |
| [frontend/design-system.md](frontend/design-system.md) | UI/UX design system |

## Project Planning

| Document | Description |
|----------|-------------|
| [ROADMAP.md](ROADMAP.md) | Project roadmap and phase status |
| [VISION.md](VISION.md) | Project vision and goals |
| [plans/](plans/) | Sprint plans and designs |

## Research & Analysis

| Document | Description |
|----------|-------------|
| [research/README.md](research/README.md) | Research index |
| [research/competitive-analysis-2026-03.md](research/competitive-analysis-2026-03.md) | Competitive analysis |

## Troubleshooting

| Document | Description |
|----------|-------------|
| [troubleshooting.md](troubleshooting.md) | Common issues and fixes |

---

## Status

- **Architecture**: AVA v2.0 uses `packages/core-v2/` + `packages/extensions/` (20 built-in)
- **Tool Surface**: ~41 tools across core, extended, git, memory, LSP, recall, and delegate categories
- **Rust Integration**: 19 crates for compute/safety hotpaths
- **Migration**: Complete — `packages/core/` is now a compatibility shim

See [CLAUDE.md](../CLAUDE.md) for the definitive architecture reference.
