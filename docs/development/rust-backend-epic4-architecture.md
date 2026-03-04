# Rust Backend Epic 4 Architecture

## Goal

Epic 4 establishes a minimal Rust backend foundation for extension loading, validation, and reflection-driven retry loops. The design keeps each crate focused on a single concern and composes them through explicit data contracts.

## Crate Responsibilities

### `ava-extensions`

- Owns extension registration and metadata.
- Separates native descriptors (runtime hooks) from WASM descriptors (declarative hook points).
- Maintains a `HookRegistry` keyed by `HookPoint` to support deterministic hook invocation.

### `ava-validator`

- Defines the validator contract (`Validator`) and result model (`ValidationResult`).
- Provides built-in syntax and compilation signal validators.
- Exposes `ValidationPipeline` and `validate_with_retry` for bounded retries with optional fix generation.

### `ava-agent`

- Defines reflection primitives for classifying failures (`ErrorKind`) and generating fixes (`ReflectionAgent`).
- Coordinates one repair cycle in `ReflectionLoop` by:
  1. classifying tool errors,
  2. requesting a fix,
  3. re-executing through `ToolExecutor`.

## Integration Flow

1. Extensions are registered through `ExtensionManager`.
2. Hooks execute around tool operations via `HookRegistry`.
3. Tool output can be validated through `ValidationPipeline`.
4. Failed executions can enter `ReflectionLoop` for a single fix-and-retry pass.

This split keeps extension wiring, validation policy, and reflective recovery independently testable while preserving simple interfaces between crates.

## Contracts and Boundaries

- `ava-extensions` does not run validators or reflection logic.
- `ava-validator` is transport-agnostic and only consumes text content.
- `ava-agent` depends on trait-based abstractions (`ReflectionAgent`, `ToolExecutor`) to avoid coupling to specific runtimes.

The boundary strategy allows runtime implementations to evolve without changing core crate APIs.
