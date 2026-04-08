# Contributing to AVA

This repository is currently organized around AVA 3.3 as the baseline core.

Read these first:

1. `AGENTS.md` — source of truth for architecture, workflow, and doc-update requirements
2. `docs/README.md` — docs entrypoint
3. `docs/project/roadmap.md` — current product direction
4. `docs/project/backlog.md` — active work
5. `docs/contributing/releasing.md` — release workflow

## Development Checks

```bash
just check              # fmt + clippy + nextest
just test               # cargo nextest run --workspace
just run                # interactive TUI

pnpm install
pnpm run tauri dev
```

## Pull Requests

1. Keep changes aligned with `AGENTS.md` and the AVA 3.3 docs.
2. Run the relevant verification before opening the PR.
3. Update `CHANGELOG.md` and `docs/project/backlog.md` when the change materially affects shipped behavior or project status.
4. Follow the release-specific instructions in `docs/contributing/releasing.md` instead of duplicating them here.
