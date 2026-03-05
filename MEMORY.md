# AVA Memory Snapshot

## Product State (v2)

- Architecture: hybrid (`core-v2` + extensions + Rust hotpaths)
- Built-in extensions: 20
- Tool surface: ~39
- Legacy `packages/core/`: compatibility re-export shim

## Backend Pattern

Use `dispatchCompute<T>(rustCommand, rustArgs, tsFallback)` for:

- compute-heavy routines
- safety-sensitive checks
- desktop-native acceleration with CLI/Node parity

Behavior:

- Tauri: Rust command path
- Non-Tauri: TS fallback path

## Important Operating Constraints

- SolidJS only in desktop UI (`src/`)
- Middleware priority ordering is contract-critical
- Keep docs aligned with current hybrid architecture (no migration-era dual-stack language)
