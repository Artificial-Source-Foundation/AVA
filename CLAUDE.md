<!-- Compatibility entrypoint for tools that still probe CLAUDE.md -->

# CLAUDE.md

This file is a compatibility entrypoint.

Use these files instead:

1. `AGENTS.md` — source of truth for repo workflow, architecture, and agent instructions
2. `docs/README.md` — docs entrypoint
3. `docs/project/roadmap.md` — AVA 3.3 baseline and product direction
4. `docs/project/backlog.md` — active work
5. `docs/architecture/crate-map.md` — current crate map
6. `docs/architecture/plugin-boundary.md` — current core-vs-plugin boundary work

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
