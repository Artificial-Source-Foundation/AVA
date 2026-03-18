# ava-sandbox

> OS-level sandbox for command execution using bwrap (Linux) or sandbox-exec (macOS).

## Public API

| Type/Function | Description |
|--------------|-------------|
| `SandboxBackend` | Trait: `name()`, `build_plan()` |
| `LinuxSandbox` | bwrap backend implementation |
| `MacOsSandbox` | sandbox-exec backend implementation |
| `select_backend()` | Returns platform-appropriate backend |
| `SandboxPolicy` | Read-only paths, writable paths, network, process spawn flags |
| `SandboxRequest` | Command, args, working dir, env vars |
| `SandboxPlan` | Program and args to execute |
| `SandboxOutput` | stdout, stderr, exit_code |
| `SandboxError` | InvalidPolicy, UnsupportedPlatform, ExecutionFailed, Timeout |
| `execute_plan()` | Async execution with timeout |
| `validate_policy()` | No-op (read-only sandboxes valid) |
| `validate_request()` | Rejects empty commands |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | SandboxBackend trait, LinuxSandbox, MacOsSandbox, select_backend() |
| `error.rs` | SandboxError enum with thiserror |
| `types.rs` | SandboxPolicy, SandboxRequest, SandboxPlan |
| `policy.rs` | Policy/request validation |
| `executor.rs` | execute_plan() with tokio process and timeout |
| `linux.rs` | build_bwrap_plan() generating bubblewrap args |
| `macos.rs` | build_sandbox_exec_plan() generating sandbox profile |

## Dependencies

Uses: None (external only: serde, thiserror, tokio, tracing)

Used by:
- `ava-tools` - For sandboxed command execution

## Key Patterns

- **Platform abstraction**: `SandboxBackend` trait with compile-time platform selection
- **Policy-based**: Sandboxes configured declaratively with paths and permissions
- **No network/process by default**: `allow_network=false`, `allow_process_spawn=false` defaults
- **Environment scrubbing**: bwrap uses `--clearenv`, macOS uses `/usr/bin/env` wrapper
- **Minimal filesystem**: Read-only bind of `/usr`, `/bin`; writable `/tmp`
- **Timeout enforcement**: All executions wrapped in `tokio::time::timeout`
- **UTF-8 output**: stdout/stderr converted with `String::from_utf8_lossy`
- **Process cleanup**: `kill_on_drop(true)` ensures cleanup on drop
