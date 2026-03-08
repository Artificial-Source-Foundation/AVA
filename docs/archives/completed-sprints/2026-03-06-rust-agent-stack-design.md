# Sprint 16a Rust Agent Stack Design

## Goal

Complete the missing Rust runtime pieces so Sprint 16b TUI can run fully in Rust without TypeScript fallbacks: core tools, real provider-backed commander, concurrency controls, sandbox execution wiring, a unified agent entrypoint, and end-to-end validation.

## Key Decisions

1. Keep current `ava-agent` API shape and adapt new work to it.
   - Current `AgentLoop` owns LLM/tools/context in the constructor.
   - Refactoring to a new runtime API now would increase blast radius and risk.

2. Add missing capabilities in additive modules with minimal breaking changes.
   - New `ava-tools::core` module with tool implementations and registration helper.
   - New commander events module for progress streaming.
   - New sandbox executor module for plan execution.
   - New `AgentStack` entrypoint module in `ava-agent`.

3. Keep security and safety defaults strict.
   - `bash` tool rejects explicitly dangerous patterns.
   - Sandbox is used for install-class commands.
   - Worker failures are isolated and do not crash global coordination.

## Architecture

### Tools

`ToolRegistry` remains the central dispatch system. New core tools implement `Tool` and are registered through `register_core_tools(registry, platform)`.

### Commander

`CommanderConfig` injects real `Arc<dyn LLMProvider>` instances with optional domain overrides. Worker creation passes selected provider into each `AgentLoop`. `coordinate` adds cancellation, timeout, event emission, and failure isolation.

### Sandbox

Existing plan-builders are extended to use `working_dir` and `env`. A new async executor runs `SandboxPlan` via `tokio::process::Command`. `BashTool` routes install-class commands through this sandbox path.

### Unified Entrypoint

`AgentStack` wires config, provider router, registry, session, memory, and platform into a single instantiation and run API for TUI integration.

## Testing Strategy

- Crate-level tests for each new capability:
  - `ava-tools` core tool behavior and schemas.
  - `ava-commander` provider wiring and coordination semantics.
  - `ava-sandbox` plan and executor behavior.
  - `ava-agent` stack init and runtime behavior.
- End-to-end tests in `ava-agent/tests/e2e_test.rs` using `MockProvider` to prove tool invocation and completion flow.
- Mandatory verification sequence after each feature:
  - `cargo test --workspace`
  - `cargo clippy --workspace -- -D warnings`
