# Project Context

## Environment
- Language: Rust-first workspace + TypeScript desktop frontend
- Runtime: Rust CLI/TUI + Node.js tooling for the desktop app
- Build: `cargo build --bin ava` and `pnpm build` for the frontend bundle
- Test: `just check` for the fast project loop, `pnpm test:run` for frontend tests
- Typecheck: `pnpm typecheck`
- Package Manager: pnpm

## Project Type
- Application: Desktop app (Tauri + SolidJS)
- Workspace: `crates/*` + `src` + `src-tauri`

## Infrastructure
- Container: none detected in root
- Orchestration: none detected in root
- CI/CD: GitHub Actions (`.github/workflows/`)
- Cloud/IaC: none detected in root

## Structure
- Frontend app: `src/`
- Tauri backend: `src-tauri/`
- Core logic: `crates/`
- CLI/TUI: `crates/ava-tui/`
- Docs: `docs/`

## Conventions Observed
- Naming: kebab-case files, camelCase functions, PascalCase types/components
- Testing: Vitest (`*.test.ts`)
- Styling: Tailwind + SolidJS component patterns
- Architecture: desktop-first, plugin ecosystem planned in frontend and partially implemented in core backend

## Notes
- Use `CLAUDE.md` and `AGENTS.md` at the repo root as the current source of truth for architecture and workflow details.
- Plugin SDK examples live under `plugins/`, but the main product backend is the Rust workspace under `crates/`.
