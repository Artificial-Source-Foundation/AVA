<!-- Compatibility entrypoint for tools that still probe CLAUDE.md -->

# CLAUDE.md

This file is a compatibility entrypoint.

Use these files instead:

1. `AGENTS.md` — source of truth for repo workflow, architecture, and agent instructions
2. `docs/README.md` — docs entrypoint
3. `docs/project/roadmap.md` — AVA 3.3 baseline and product direction
4. `docs/project/backlog.md` — active work
5. `docs/architecture/README.md` — architecture entrypoint and audits
6. `docs/architecture/agent-backend-capability-audit-m1.md` — current coding-agent backend capability inventory
7. `docs/architecture/agent-backend-capability-comparison-m2.md` — external comparison matrix for backend correction planning
8. `docs/architecture/crate-map.md` — current crate map
9. `docs/architecture/plugin-boundary.md` — current core-vs-plugin boundary work

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

## AVA 3.3 Summary

1. AVA is a practical solo-first coding agent.
2. Core AVA stays small and opinionated.
3. Plugins, MCPs, commands, and skills are the extension path.
4. HQ is no longer part of the default core product surface.
