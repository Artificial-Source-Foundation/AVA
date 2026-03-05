<!-- Last verified: 2026-03-05. Run 'npm run test:run && cargo test --workspace' to revalidate. -->

# AVA Backend Architecture Guide (v2)

## 1) System Boundaries

AVA backend is organized into three layers:

1. `packages/core-v2/`
   - agent loop
   - tool registry
   - extension API + middleware pipeline
   - session/event infrastructure

2. `packages/extensions/`
   - built-in capabilities packaged as extensions
   - middleware-based cross-cutting behavior
   - provider, context, safety, reliability, plugin modules

3. `src-tauri/` Rust commands
   - accelerated compute
   - safety/validation primitives
   - desktop-native operations

## 2) Execution Flow

High-level turn flow:

1. history prepared and compacted
2. model response parsed
3. tool calls executed through registry
4. middleware `before` and `after` applied in priority order
5. events emitted (`tool:finish`, `turn:end`, usage telemetry)
6. termination/completion decision

## 3) Hotpath Dispatch Contract

Use:

```ts
dispatchCompute<T>(rustCommand, rustArgs, tsFallback)
```

Rules:

- Rust command path in Tauri runtime
- TypeScript fallback in non-Tauri runtimes
- caller owns fallback semantics and error strategy

This pattern keeps CLI/Node parity while enabling Rust acceleration for desktop builds.

## 4) Middleware Priority Model

Lower numeric priority runs earlier.

Current architecture depends on deterministic ordering for:

- sandbox policy decisions
- permission gating
- reliability and recovery handling
- formatter and post-processing

When adding middleware, document the chosen priority and expected interactions.

## 5) Current Topology Summary

- Built-in extensions: ~20
- Tool surface: ~41
- `packages/core/`: compatibility re-export layer (not primary implementation target)
- Typical runtime extension count: ~31 (20 feature + 16 providers - 4 disabled: lsp, mcp, server, litellm)

## 6) Docs Contract

Canonical architecture docs should reflect the hybrid v2 model and avoid migration-era dual-stack framing.

Primary references:

- `CLAUDE.md`
- `docs/backend.md`
- `docs/troubleshooting.md`
