# ava-sandbox

> OS-level command sandboxing -- bwrap (Linux) and sandbox-exec (macOS) backends.

**Crate path:** `crates/ava-sandbox/`
**Primary modules:** `types`, `policy`, `executor`, `linux`, `macos`, `error`

---

## Overview

The `ava-sandbox` crate provides OS-level sandboxing for shell commands executed by the `bash` tool. It isolates install-class commands (e.g., `npm install`, `pip install`, `cargo add`) using platform-specific sandboxing backends:

- **Linux**: `bwrap` (bubblewrap) -- namespace-based isolation
- **macOS**: `sandbox-exec` -- Seatbelt sandbox profiles

The sandboxing flow is:

1. The `bash` tool detects an install-class command
2. `select_backend()` returns the appropriate `SandboxBackend` for the OS
3. A `SandboxPolicy` is constructed with read-only and writable paths
4. The backend's `build_plan()` generates a `SandboxPlan` (program + args)
5. `execute_plan()` runs the plan with a timeout

---

## SandboxBackend Trait

**File:** `crates/ava-sandbox/src/lib.rs`, lines 14-17

```rust
pub trait SandboxBackend: Send + Sync {
    fn name(&self) -> &'static str;
    fn build_plan(&self, request: &SandboxRequest, policy: &SandboxPolicy) -> Result<SandboxPlan>;
}
```

Two implementations:
- `LinuxSandbox` -- name: `"linux-bwrap"`, delegates to `linux::build_bwrap_plan()`
- `MacOsSandbox` -- name: `"macos-sandbox-exec"`, delegates to `macos::build_sandbox_exec_plan()`

### Backend selection

**File:** `crates/ava-sandbox/src/lib.rs`, lines 42-57

```rust
pub fn select_backend() -> Result<Box<dyn SandboxBackend>>
```

Uses `#[cfg(target_os)]` to select:
- Linux -> `LinuxSandbox`
- macOS -> `MacOsSandbox`
- Other -> `Err(SandboxError::UnsupportedPlatform)`

---

## Core Types

**File:** `crates/ava-sandbox/src/types.rs`

### SandboxRequest

The command to execute inside the sandbox:

```rust
pub struct SandboxRequest {
    pub command: String,               // Program to run (e.g., "sh")
    pub args: Vec<String>,             // Arguments (e.g., ["-c", "npm install lodash"])
    pub working_dir: Option<String>,   // Working directory inside sandbox
    pub env: Vec<(String, String)>,    // Environment variables to set
}
```

### SandboxPolicy

Filesystem and capability restrictions:

```rust
pub struct SandboxPolicy {
    pub read_only_paths: Vec<String>,    // Paths mounted read-only
    pub writable_paths: Vec<String>,     // Paths mounted read-write
    pub allow_network: bool,             // Whether to allow network access
    pub allow_process_spawn: bool,       // Whether to allow spawning child processes
}
```

Default policy:
- Read-only: `/usr`, `/bin`
- Writable: `/tmp`
- Network: disabled
- Process spawn: disabled

### SandboxPlan

The resolved command to execute (output of `build_plan()`):

```rust
pub struct SandboxPlan {
    pub program: String,       // "bwrap" or "sandbox-exec"
    pub args: Vec<String>,     // Full argument list including sandbox flags
}
```

---

## Policy Validation

**File:** `crates/ava-sandbox/src/policy.rs`

Two validation functions called by both backends before building plans:

- `validate_policy(policy)` -- requires at least one writable path
- `validate_request(request)` -- rejects empty commands

---

## Linux Backend (bwrap)

**File:** `crates/ava-sandbox/src/linux.rs`

Uses [bubblewrap](https://github.com/containers/bubblewrap) for namespace-based isolation.

```rust
pub fn build_bwrap_plan(request: &SandboxRequest, policy: &SandboxPolicy) -> Result<SandboxPlan>
```

### Generated bwrap arguments

The plan generates a `bwrap` command with these flags:

| Flag | Purpose |
|------|---------|
| `--unshare-user` | Create new user namespace |
| `--unshare-pid` | Create new PID namespace |
| `--die-with-parent` | Kill sandbox if parent dies |
| `--unshare-net` | Create new network namespace (when `allow_network: false`) |
| `--ro-bind {path} {path}` | Mount read-only paths (one per `read_only_paths` entry) |
| `--bind {path} {path}` | Mount writable paths (one per `writable_paths` entry) |
| `--chdir {dir}` | Set working directory (when `working_dir` is set) |
| `--setenv {key} {value}` | Set environment variables (one per `env` entry) |
| `--` | End of bwrap options |
| `{command} {args...}` | The actual command to run |

### Example

For an `npm install lodash` in `/home/user/project`:

```
bwrap --unshare-user --unshare-pid --die-with-parent \
  --ro-bind /usr /usr --ro-bind /bin /bin --ro-bind /lib /lib \
  --bind /home/user/project /home/user/project --bind /tmp /tmp \
  --chdir /home/user/project \
  --setenv PATH /usr/bin:/bin \
  -- sh -c "npm install lodash"
```

---

## macOS Backend (sandbox-exec)

**File:** `crates/ava-sandbox/src/macos.rs`

Uses macOS Seatbelt (sandbox-exec) with a dynamically generated profile.

```rust
pub fn build_sandbox_exec_plan(request: &SandboxRequest, policy: &SandboxPolicy) -> Result<SandboxPlan>
```

### Generated sandbox profile

The profile uses Seatbelt's S-expression syntax:

```lisp
(version 1)
(deny default)
(allow process-exec)
(allow process-fork)
(allow file-read* (subpath "/usr"))
(allow file-read* (subpath "/bin"))
(allow file-read* (subpath "/tmp"))
(allow file-write* (subpath "/tmp"))
(allow network*)                        ;; only when allow_network: true
(allow file-read* (subpath "/cwd"))     ;; working dir if set
(allow file-write* (subpath "/cwd"))
```

### Generated arguments

```
sandbox-exec -p "{profile}" {command} {args...}
```

When environment variables are specified, `/usr/bin/env` is inserted:

```
sandbox-exec -p "{profile}" /usr/bin/env KEY=VALUE {command} {args...}
```

---

## Plan Executor

**File:** `crates/ava-sandbox/src/executor.rs`

```rust
pub async fn execute_plan(plan: &SandboxPlan, timeout: Duration) -> Result<SandboxOutput, SandboxError>
```

### SandboxOutput

```rust
pub struct SandboxOutput {
    pub stdout: String,
    pub stderr: String,
    pub exit_code: i32,
}
```

### Execution

- Spawns the plan as a child process via `tokio::process::Command`
- Captures stdout and stderr via `Stdio::piped()`
- Uses `kill_on_drop(true)` to clean up on cancellation
- Wraps in `tokio::time::timeout()` -- returns `SandboxError::Timeout` on expiry
- Exit code defaults to -1 if the process is killed

---

## Error Types

**File:** `crates/ava-sandbox/src/error.rs`

```rust
pub enum SandboxError {
    InvalidPolicy(String),       // Policy validation failed
    UnsupportedPlatform(String), // No sandbox backend for this OS
    ExecutionFailed(String),     // Command spawn or wait failed
    Timeout,                     // Exceeded time budget
}
```

---

## Integration with bash tool

**File:** `crates/ava-tools/src/core/bash.rs`, lines 74-112

The `bash` tool routes install-class commands through the sandbox:

1. `is_install_class(&command)` checks for patterns: `npm install`, `yarn add`, `pnpm add`, `pip install`, `pip3 install`, `cargo install`, `cargo add`, `apt install`, `apt-get install`, `brew install`, `npm i`
2. `select_backend()` gets the OS-appropriate sandbox
3. A `SandboxPolicy` is created:
   - Read-only: `/usr`, `/bin`, `/lib`
   - Writable: `{cwd}`, `/tmp`
   - Network: enabled (install commands need it)
   - Process spawn: enabled
4. Environment is filtered to only safe variables: `PATH`, `HOME`, `USER`, `SHELL`, `LANG`, `LC_ALL`, `LC_CTYPE`, `TERM`, `CARGO_HOME`, `RUSTUP_HOME`
5. The sandbox plan is built and executed with the bash tool's timeout
