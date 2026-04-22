<!-- Compatibility entrypoint for tools that still probe CLAUDE.md -->

# CLAUDE.md

This file is a compatibility entrypoint.

Use these files instead:

1. `AGENTS.md` — source of truth for repo workflow, architecture, and agent instructions
2. `docs/README.md` — docs entrypoint
3. `docs/project/roadmap.md` — AVA 0.6 roadmap and product direction
4. `docs/project/backlog.md` — active work
5. `docs/architecture/README.md` — architecture entrypoint, canonical docs, and historical archive links
6. `docs/architecture/entrypoints.md` — runtime composition roots and adapter wiring
7. `docs/architecture/crate-map.md` — current crate map
8. `docs/architecture/shared-backend-contract-m6.md` — canonical shared-backend contract
9. `docs/archive/architecture/README.md` — historical architecture milestone artifacts

## Quick Commands

```bash
just check
just test
just lint
just fmt
just run
just headless "goal"

cargo run --bin ava
cargo run --bin ava -- --help
cargo run --bin ava -- serve --port 8080

pnpm tauri dev
pnpm lint && pnpm typecheck
```

## AVA 0.6 Summary

1. AVA is a practical solo-first coding agent.
2. Core AVA stays small and opinionated.
3. Plugins, MCPs, commands, and skills are the extension path.
4. HQ is no longer part of the default core product surface.
