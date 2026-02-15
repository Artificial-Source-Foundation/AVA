# Project Context

## Environment
- Language: TypeScript (monorepo) + Rust (Tauri backend)
- Runtime: Node.js + Tauri desktop app
- Build: `npm run build:packages` then `npm run build:cli`
- Test: `npm run test:run` (or `npx vitest run <path>`)
- Typecheck: `npx tsc --noEmit`
- Package Manager: npm (lockfile present) and pnpm workspace metadata

## Project Type
- Application: Desktop app (Tauri + SolidJS)
- Monorepo: `packages/*` + `cli` + `src`

## Infrastructure
- Container: none detected in root
- Orchestration: none detected in root
- CI/CD: GitHub Actions (`.github/workflows/`)
- Cloud/IaC: none detected in root

## Structure
- Frontend app: `src/`
- Tauri backend: `src-tauri/`
- Core logic: `packages/core/src/`
- CLI: `cli/src/`
- Docs: `docs/`

## Conventions Observed
- Naming: kebab-case files, camelCase functions, PascalCase types/components
- Testing: Vitest (`*.test.ts`)
- Styling: Tailwind + SolidJS component patterns
- Architecture: desktop-first, plugin ecosystem planned in frontend and partially implemented in core backend

## Notes
- `docs/ROADMAP.md` still marks Phase 2 plugin ecosystem as next.
- Core extension lifecycle code already exists in `packages/core/src/extensions/`.
- Frontend plugin UX remains placeholder in settings (no marketplace UI yet).
- Sprint 1.6 testing/hardening exists as a plan doc and appears partially implemented.
