# Epic 4 Sprint 34 Design (Extensions & Validation)

**Goal:** Deliver production-grade extension loading, validation pipeline, and reflection-loop error recovery in Rust with crate-level boundaries and Tauri adapters.

## Scope

Sprint 34 stories:

1. Extension system (native + WASM plugins, hook registration, reload)
2. Validation pipeline (syntax + compilation checks, retry orchestration)
3. Reflection loop (error analysis + one-step fix attempt)

## Architecture

Create three focused backend crates and integrate them in `src-tauri`:

- `crates/ava-extensions`
- `crates/ava-validator`
- `crates/ava-agent`

Each crate owns one concern and exposes typed APIs. `src-tauri` provides thin command adapters and no business logic.

## Components

### 1) `ava-extensions`

- `Extension` trait for native extension contracts.
- `ExtensionManager` for native dynamic library loading (`libloading`) and WASM loading (`wasmtime`).
- Registry primitives for tools/hooks/validators.
- Hook lifecycle support and hot-reload API.

### 2) `ava-validator`

- `Validator` async trait and `ValidationPipeline` orchestrator.
- Built-in validators:
  - syntax validator (`tree-sitter` parser check)
  - compilation validator (language-aware compile/check command)
- `validate_with_retry` flow invoking a fixer trait up to bounded attempts.

### 3) `ava-agent`

- `reflection.rs` with `ReflectionLoop`.
- Error classifier for syntax/import/type/tool command failures.
- One-shot fix attempt through pluggable `ReflectionAgent` and `ToolExecutor` traits.

## Data Flow

1. Tool or edit produces candidate output/error.
2. Validation pipeline runs configured validators.
3. On failure, retry orchestration asks reflection agent for a fix.
4. Reflection loop re-executes once and returns improved result or original error.
5. Extension hooks run around tool/LLM boundaries where registered.

## Reliability and Safety

- Fail-closed behavior for extension load errors and validator crashes.
- WASM runtime sandboxed with explicit host imports only.
- Retry count capped; no unbounded loops.
- Reflection limited to one fix execution per error to prevent runaway behavior.

## Testing Strategy

- Unit tests per crate for happy/error paths.
- Integration tests for:
  - native + WASM loading contracts
  - validation fail/pass transitions
  - reflection classification and one-shot recovery
- Tauri adapter tests for JSON mapping and serializable responses.

## Out of Scope (Sprint 34)

- CPU/memory profiling and micro-optimizations (Sprint 35)
- full production performance targets and >80% total coverage target (Sprint 35 completion)
