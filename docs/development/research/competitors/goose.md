# Goose

> AI coding agent by Block (formerly Square) (~10k GitHub stars)
> Analyzed: March 2026

---

## Architecture Summary

Goose is Block's open-source AI coding agent, built in **Rust** for performance and safety. It's designed for developer workflows at scale with a focus on reliability.

**Key architectural decisions:**
- **Rust implementation** — Memory safety, performance
- **Tool-based architecture** — Modular tools with clear interfaces
- **Session management** — Persistent sessions with state
- **Extension system** — Go-based extensions (plugins)

### Project Structure

```
goose/
├── crates/
│   ├── goose/               # Core library
│   ├── goose-cli/           # CLI binary
│   └── goose-server/        # Server mode
├── extensions/              # Go extension SDK
└── ...
```

---

## Key Patterns

### 1. Rust Core

Memory-safe, high-performance core:
- No garbage collection pauses
- Type safety
- Easy parallelism

### 2. Tool Registry

Clean tool abstraction:
```rust
trait Tool {
    fn name(&self) -> &str;
    fn description(&self) -> &str;
    async fn execute(&self, params: Value) -> Result<Value>;
}
```

### 3. Go Extensions

Extensions written in Go:
- Separate process for isolation
- gRPC communication
- Hot reloading

### 4. Session Persistence

SQLite-backed session storage:
- Resume sessions
- Fork conversations
- Search history

---

## What AVA Can Learn

### High Priority

1. **Rust Core** — AVA is already moving to Rust (crates/ava-*); this is the right direction.

2. **Tool Registry** — Clean trait-based tool system.

3. **Extension Isolation** — Separate process for extensions improves stability.

### Medium Priority

4. **Session Forking** — Ability to branch conversations.

---

## Comparison: Goose vs AVA

| Capability | Goose | AVA |
|------------|-------|-----|
| **Language** | Rust + Go extensions | Rust + TS (transitioning) |
| **Extensions** | Go (separate process) | TypeScript (in-process) |
| **Session** | SQLite-backed | SQLite-backed |
| **Platform** | CLI + Server | Desktop + CLI |

---

*Consolidated from: audits/goose-audit.md, backend-analysis/goose.md*
