<!-- Last verified: 2026-03-16. Run 'cargo test --workspace' to revalidate. -->

# AVA Backend Architecture Guide (v3)

> The backend is pure Rust. The SolidJS desktop frontend communicates with Rust via Tauri IPC. The CLI (`crates/ava-tui/`) calls Rust crates directly.

## 1) System Boundaries

AVA backend is organized into two layers:

1. `crates/` — Rust crate ecosystem
   - agent loop, tool registry, LLM providers
   - session/memory persistence, context management
   - permissions, sandboxing, safety
   - multi-agent orchestration (Praxis)

2. `src-tauri/` — Tauri IPC commands
   - bridges SolidJS frontend to Rust crates
   - desktop-native operations

## 2) Execution Flow

High-level turn flow:

1. history prepared and compacted
2. model response parsed
3. tool calls executed through registry
4. middleware `before` and `after` applied in priority order
5. events emitted (`tool:finish`, `turn:end`, usage telemetry)
6. termination/completion decision

## 3) Middleware Priority Model

Lower numeric priority runs earlier.

Current architecture depends on deterministic ordering for:

- sandbox policy decisions
- permission gating
- reliability and recovery handling
- formatter and post-processing

When adding middleware, document the chosen priority and expected interactions. Register via `ToolRegistry::add_middleware()`.

## 4) Current Topology Summary

- Rust crates: ~22 under `crates/`
- Built-in tools by default: 6, with 8 additional extended tools available when enabled
- Dynamic MCP tools and TOML custom tools supported at runtime

## 5) Docs Contract

Primary references:

- `CLAUDE.md`
- `docs/architecture/backend.md`
- `docs/troubleshooting/`
