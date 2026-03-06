# Sprint 16a: Complete Rust Agent Stack — Implementation Prompt

> For AI coding agent. Estimated: 6 features, mix M/L effort.
> Run `cargo test --workspace && cargo clippy --workspace -- -D warnings` after each feature.
> This sprint MUST complete before Sprint 16b (Ratatui TUI).

---

## Role

You are implementing Sprint 16a (Complete Rust Agent Stack) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, architecture)
- `crates/ava-agent/src/lib.rs` (AgentLoop — production-ready, 874 LOC)
- `crates/ava-llm/src/lib.rs` (LLM providers — production-ready, 6 providers, streaming)
- `crates/ava-tools/src/lib.rs` + `src/registry.rs` (Tool trait, ToolRegistry, Middleware)
- `crates/ava-tools/src/edit/mod.rs` (EditEngine — 9 strategies)
- `crates/ava-platform/src/lib.rs` (Platform trait — read_file, write_file, execute, execute_streaming)
- `crates/ava-commander/src/lib.rs` (Commander — routing works, NullProvider stub)
- `crates/ava-sandbox/src/lib.rs` (sandbox plan generation — bwrap/sandbox-exec)
- `crates/ava-context/src/lib.rs` (context management — fully functional)
- `crates/ava-codebase/src/search.rs` (SearchIndex — tantivy BM25)
- `crates/ava-codebase/src/lib.rs` (repomap, pagerank)

**CONTEXT**: AVA has 19 Rust crates. 12 are production-ready, 7 were labeled "partial." Deep audits revealed that `ava-context` is actually complete, and the critical gaps are:
1. **No core tool implementations** — Tool trait infra exists but read/write/edit/bash/glob/grep aren't wired
2. **ava-commander** uses NullProvider — subagent orchestration can't actually run agents
3. **ava-sandbox** generates plans but never executes them — BashTool is unsandboxed

This sprint fills those gaps so the pure Rust TUI (Sprint 16b) has a complete agent stack to call into.

---

## Feature 1: Core Tool Implementations

### What to Read
- `crates/ava-tools/src/registry.rs` — `Tool` trait (`name`, `description`, `parameters`, `execute`)
- `crates/ava-platform/src/lib.rs` — `Platform` trait, `StandardPlatform` impl
- `crates/ava-tools/src/edit/mod.rs` — `EditEngine::apply()`, `EditRequest`, 9 strategies
- `crates/ava-codebase/src/search.rs` — `SearchIndex` (tantivy-based)
- `packages/extensions/tools-extended/src/` — TypeScript implementations (port the parameter schemas and behavior)

### What to Build
6 concrete `Tool` trait implementations in `crates/ava-tools/src/core/`.

**Files:**
- `crates/ava-tools/src/core/mod.rs` — Module + `register_core_tools(registry, platform)` helper
- `crates/ava-tools/src/core/read.rs` — ReadTool
- `crates/ava-tools/src/core/write.rs` — WriteTool
- `crates/ava-tools/src/core/edit.rs` — EditTool (wraps EditEngine)
- `crates/ava-tools/src/core/bash.rs` — BashTool (wraps Platform::execute)
- `crates/ava-tools/src/core/glob.rs` — GlobTool
- `crates/ava-tools/src/core/grep.rs` — GrepTool
- Update `crates/ava-tools/src/lib.rs` to export `pub mod core;`
- Update `crates/ava-tools/Cargo.toml` with new dependencies

**Dependencies to add to `crates/ava-tools/Cargo.toml`:**
```toml
ava-platform = { path = "../ava-platform" }
glob = "0.3"
grep-regex = "0.1"
grep-searcher = "0.1"
grep-matcher = "0.1"
```

**Implementation per tool:**

#### ReadTool
- Params: `path` (required string), `offset` (optional u64, 1-based line), `limit` (optional u64)
- Uses `Platform::read_file(path)`
- Adds line numbers in `cat -n` format: `{line_num:>6}\t{content}`
- Applies offset/limit to lines
- Error: file not found, permission denied

#### WriteTool
- Params: `path` (required string), `content` (required string)
- Creates parent directories with `tokio::fs::create_dir_all`
- Uses `Platform::write_file(path, content)`
- Returns: `"Wrote {bytes} bytes to {path}"`

#### EditTool
- Params: `path` (required string), `old_text` (required string), `new_text` (required string), `replace_all` (optional bool)
- Reads file via Platform, applies `EditEngine::apply()` with `EditRequest`, writes back
- If `replace_all`: use `str::replace()` instead of EditEngine
- Returns: strategy name used + line count of changes

#### BashTool
- Params: `command` (required string), `timeout_ms` (optional u64, default 120000), `cwd` (optional string)
- Uses `Platform::execute(command)` wrapped in `tokio::time::timeout`
- If cwd provided, prepend `cd "{cwd}" && ` to command
- Returns: stdout + stderr + exit_code
- Truncate output to 100KB max (with "[truncated]" notice)
- **Security**: reject commands matching dangerous patterns (`rm -rf /`, `dd if=`, `mkfs`, `:(){ :|:& };:`)

#### GlobTool
- Params: `pattern` (required string), `path` (optional string, default ".")
- Uses `glob::glob()` with pattern joined to path
- Returns: matching paths sorted by modification time (newest first)
- Limit: 1000 results max

#### GrepTool
- Params: `pattern` (required string), `path` (optional string, default "."), `include` (optional string, file glob filter)
- Uses `grep-regex` + `grep-searcher` for fast regex search
- Returns: `{file}:{line_num}:{content}` format
- Limit: 500 matches max
- Respects `.gitignore` if present

#### Registration helper:
```rust
use crate::registry::ToolRegistry;
use ava_platform::Platform;
use std::sync::Arc;

pub fn register_core_tools(registry: &mut ToolRegistry, platform: Arc<dyn Platform>) {
    registry.register(read::ReadTool::new(platform.clone()));
    registry.register(write::WriteTool::new(platform.clone()));
    registry.register(edit::EditTool::new(platform.clone()));
    registry.register(bash::BashTool::new(platform.clone()));
    registry.register(glob::GlobTool::new());
    registry.register(grep::GrepTool::new());
}
```

### Tests
`crates/ava-tools/tests/core_tools_test.rs` using `tempfile` crate:
- **ReadTool**: read existing file, read with offset/limit, read nonexistent → error
- **WriteTool**: write new file, creates parent dirs, overwrite existing
- **EditTool**: exact match replacement, multi-strategy fallback, no-match → error
- **BashTool**: echo command, exit code propagation, timeout enforcement, dangerous command rejection
- **GlobTool**: match patterns, empty result, respects path
- **GrepTool**: regex match, include filter, empty result

### Acceptance Criteria
- `cargo test -p ava-tools` — all new + existing tests pass
- 6 tools registered via `register_core_tools()`
- Each tool's `parameters()` returns valid JSON Schema

---

## Feature 2: Wire Real LLM Providers into Commander

### What to Read
- `crates/ava-commander/src/lib.rs` — Commander, Lead, Worker, NullProvider (lines 198-235)
- `crates/ava-llm/src/lib.rs` — LLMProvider trait, ModelRouter, providers
- `crates/ava-llm/src/provider.rs` — LLMProvider trait definition (5 methods)
- `crates/ava-llm/src/router.rs` — ModelRouter with tier routing
- `crates/ava-agent/src/lib.rs` — AgentLoop (uses LLMProvider)

### What to Build
Replace NullProvider with real LLM provider injection via constructor. Add a builder pattern for Commander configuration.

**Files to modify:**
- `crates/ava-commander/src/lib.rs` — Major refactor
- `crates/ava-commander/Cargo.toml` — Add ava-llm dependency
- `crates/ava-commander/tests/commander.rs` — Update tests

**Implementation:**

Remove `NullProvider` entirely. Add provider injection:

```rust
use ava_llm::provider::LLMProvider;
use std::sync::Arc;

pub struct CommanderConfig {
    pub budget: Budget,
    pub default_provider: Arc<dyn LLMProvider>,
    /// Optional per-domain provider overrides
    pub domain_providers: HashMap<Domain, Arc<dyn LLMProvider>>,
}

impl Commander {
    pub fn new(config: CommanderConfig) -> Self {
        let leads = vec![
            Lead::new("frontend-lead", Domain::Frontend, config.provider_for(Domain::Frontend)),
            Lead::new("backend-lead", Domain::Backend, config.provider_for(Domain::Backend)),
            Lead::new("qa-lead", Domain::QA, config.provider_for(Domain::QA)),
            Lead::new("research-lead", Domain::Research, config.provider_for(Domain::Research)),
            Lead::new("debug-lead", Domain::Debug, config.provider_for(Domain::Debug)),
            Lead::new("fullstack-lead", Domain::Fullstack, config.provider_for(Domain::Fullstack)),
            Lead::new("devops-lead", Domain::DevOps, config.provider_for(Domain::DevOps)),
        ];
        Self { leads, budget: config.budget, config }
    }
}

impl CommanderConfig {
    fn provider_for(&self, domain: Domain) -> Arc<dyn LLMProvider> {
        self.domain_providers.get(&domain)
            .cloned()
            .unwrap_or_else(|| self.default_provider.clone())
    }
}
```

Update `Lead` to accept provider:
```rust
pub struct Lead {
    name: String,
    domain: Domain,
    workers: Vec<Worker>,
    provider: Arc<dyn LLMProvider>,
}

impl Lead {
    pub fn new(name: impl Into<String>, domain: Domain, provider: Arc<dyn LLMProvider>) -> Self {
        Self {
            name: name.into(),
            domain,
            workers: Vec::new(),
            provider,
        }
    }

    pub fn spawn_worker(&self, task: Task, budget: &Budget, tools: Arc<ToolRegistry>) -> Result<Worker> {
        let worker_budget = Budget {
            max_tokens: (budget.max_tokens / 2).max(1),
            max_turns: (budget.max_turns / 2).max(1),
            max_cost_usd: budget.max_cost_usd / 2.0,
        };

        let config = ava_agent::AgentConfig {
            max_turns: worker_budget.max_turns,
            token_limit: worker_budget.max_tokens,
            model: self.provider.model_name().to_string(),
        };

        let agent = ava_agent::AgentLoop::new(config);

        Ok(Worker {
            id: Uuid::new_v4(),
            lead: self.name.clone(),
            task,
            budget: worker_budget,
            agent: Arc::new(Mutex::new(agent)),
            provider: self.provider.clone(),
            tools,
        })
    }
}
```

Update `Worker` to carry provider + tools:
```rust
pub struct Worker {
    id: Uuid,
    lead: String,
    task: Task,
    budget: Budget,
    agent: Arc<Mutex<AgentLoop>>,
    provider: Arc<dyn LLMProvider>,
    tools: Arc<ToolRegistry>,
}
```

Update `coordinate()` to pass provider and tools to agent run:
```rust
pub async fn coordinate(&self, workers: Vec<Worker>) -> Result<Session> {
    let futures = workers.into_iter().map(|worker| async move {
        let mut agent = worker.agent.lock().await;
        agent.run(
            &worker.task.description,
            worker.provider.as_ref(),
            &worker.tools,
        ).await
    });
    let results = futures::future::join_all(futures).await;
    // ... merge sessions
}
```

### Tests
Update `crates/ava-commander/tests/commander.rs`:
- Use `ava_llm::MockProvider` instead of NullProvider
- Test: delegation routes correctly with mock provider
- Test: worker spawn creates agent with correct model name
- Test: coordinate runs workers and merges sessions (mock returns completion)

### Acceptance Criteria
- NullProvider is completely removed
- Commander accepts `Arc<dyn LLMProvider>` via `CommanderConfig`
- Per-domain provider overrides work
- Tests pass with `MockProvider` from ava-llm
- `cargo test -p ava-commander` passes

---

## Feature 3: Commander Concurrency — Cancellation & Budget Enforcement

### What to Read
- `crates/ava-commander/src/lib.rs` — `coordinate()` with `join_all`
- `crates/ava-agent/src/lib.rs` — `AgentLoop::run()` and `run_streaming()`
- Reference: `docs/reference-code/codex-cli/codex-rs/tui/src/app.rs` — cancellation token pattern

### What to Build
Add cancellation tokens, timeout enforcement, streaming progress events, and failure isolation to the Commander.

**Files:**
- `crates/ava-commander/src/lib.rs` — Update coordinate()
- `crates/ava-commander/src/events.rs` — New: progress event types

**Implementation:**

Progress events:
```rust
#[derive(Debug, Clone, Serialize)]
pub enum CommanderEvent {
    WorkerStarted { worker_id: Uuid, lead: String, task_description: String },
    WorkerProgress { worker_id: Uuid, turn: usize, max_turns: usize },
    WorkerToken { worker_id: Uuid, token: String },
    WorkerCompleted { worker_id: Uuid, success: bool, turns: usize },
    WorkerFailed { worker_id: Uuid, error: String },
    AllComplete { total_workers: usize, succeeded: usize, failed: usize },
}
```

Cancellation + timeout in coordinate():
```rust
use tokio_util::sync::CancellationToken;

pub async fn coordinate(
    &self,
    workers: Vec<Worker>,
    cancel: CancellationToken,
    event_tx: mpsc::UnboundedSender<CommanderEvent>,
) -> Result<Session> {
    let futures = workers.into_iter().map(|worker| {
        let cancel = cancel.clone();
        let tx = event_tx.clone();
        let timeout = Duration::from_secs((worker.budget.max_turns * 60) as u64);

        async move {
            tx.send(CommanderEvent::WorkerStarted {
                worker_id: worker.id,
                lead: worker.lead.clone(),
                task_description: worker.task.description.clone(),
            }).ok();

            let result = tokio::select! {
                r = tokio::time::timeout(timeout, run_worker(&worker, tx.clone())) => {
                    match r {
                        Ok(r) => r,
                        Err(_) => Err(AvaError::Timeout("Worker timed out".into())),
                    }
                }
                _ = cancel.cancelled() => {
                    Err(AvaError::Cancelled("Operation cancelled".into()))
                }
            };

            match &result {
                Ok(session) => {
                    tx.send(CommanderEvent::WorkerCompleted {
                        worker_id: worker.id, success: true, turns: session.messages.len(),
                    }).ok();
                }
                Err(e) => {
                    tx.send(CommanderEvent::WorkerFailed {
                        worker_id: worker.id, error: e.to_string(),
                    }).ok();
                }
            }

            (worker.id, result)
        }
    });

    let results = futures::future::join_all(futures).await;

    // Failure isolation: collect successes, report failures
    let mut combined = Session::new();
    let mut succeeded = 0;
    let mut failed = 0;

    for (id, result) in &results {
        match result {
            Ok(session) => {
                for msg in &session.messages {
                    combined.add_message(msg.clone());
                }
                succeeded += 1;
            }
            Err(_) => { failed += 1; }
        }
    }

    event_tx.send(CommanderEvent::AllComplete {
        total_workers: results.len(),
        succeeded,
        failed,
    }).ok();

    Ok(combined)
}
```

Add `tokio-util` dependency to `crates/ava-commander/Cargo.toml`:
```toml
tokio-util = { version = "0.7", features = ["rt"] }
```

### Tests
- Test: cancellation token stops workers mid-execution
- Test: timeout fires for slow workers, others continue
- Test: one worker failure doesn't cancel others (failure isolation)
- Test: progress events emitted in correct order

### Acceptance Criteria
- Workers can be cancelled via `CancellationToken`
- Timeouts enforce budget limits
- Failed workers don't crash the entire coordination
- Progress events stream to caller via channel
- `cargo test -p ava-commander` passes

---

## Feature 4: Sandbox Execution — Wire Plan to Process Spawn

### What to Read
- `crates/ava-sandbox/src/lib.rs` — `SandboxBackend` trait, `select_backend()`
- `crates/ava-sandbox/src/linux.rs` — bwrap plan builder
- `crates/ava-sandbox/src/macos.rs` — sandbox-exec plan builder
- `crates/ava-sandbox/src/types.rs` — `SandboxRequest`, `SandboxPolicy`, `SandboxPlan`
- `crates/ava-platform/src/shell.rs` — Shell execution

### What to Build
1. Fix unused fields (`working_dir`, `env`) in plan builders
2. Add actual process execution from sandbox plans
3. Wire into BashTool as sandboxed execution path

**Files:**
- `crates/ava-sandbox/src/linux.rs` — Use working_dir + env in bwrap plan
- `crates/ava-sandbox/src/macos.rs` — Use working_dir + env in sandbox-exec plan
- `crates/ava-sandbox/src/executor.rs` — New: execute a SandboxPlan as subprocess
- `crates/ava-sandbox/src/lib.rs` — Export executor, add `execute_sandboxed()` convenience fn
- `crates/ava-sandbox/Cargo.toml` — Add tokio dependency
- `crates/ava-tools/src/core/bash.rs` — Update BashTool to use sandbox for dangerous commands

**Implementation:**

Fix bwrap plan builder to use `working_dir`:
```rust
pub fn build_bwrap_plan(request: &SandboxRequest, policy: &SandboxPolicy) -> Result<SandboxPlan> {
    let mut args = vec![
        "--unshare-user".to_string(),
        "--unshare-pid".to_string(),
        "--die-with-parent".to_string(),
    ];

    if !policy.allow_network {
        args.push("--unshare-net".to_string());
    }

    for path in &policy.read_only_paths {
        args.extend(["--ro-bind".to_string(), path.clone(), path.clone()]);
    }
    for path in &policy.writable_paths {
        args.extend(["--bind".to_string(), path.clone(), path.clone()]);
    }

    // NEW: working directory
    if let Some(cwd) = &request.working_dir {
        args.extend(["--chdir".to_string(), cwd.clone()]);
    }

    // NEW: environment variables
    for (key, value) in &request.env {
        args.extend(["--setenv".to_string(), key.clone(), value.clone()]);
    }

    args.push("--".to_string());
    args.push(request.command.clone());
    args.extend(request.args.clone());

    Ok(SandboxPlan { program: "bwrap".to_string(), args })
}
```

Executor (`executor.rs`):
```rust
use crate::types::SandboxPlan;
use std::process::Stdio;
use tokio::process::Command;

pub struct SandboxOutput {
    pub stdout: String,
    pub stderr: String,
    pub exit_code: i32,
}

pub async fn execute_plan(
    plan: &SandboxPlan,
    timeout: std::time::Duration,
) -> Result<SandboxOutput, crate::error::SandboxError> {
    let result = tokio::time::timeout(timeout, async {
        let output = Command::new(&plan.program)
            .args(&plan.args)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()?
            .wait_with_output()
            .await?;

        Ok::<_, std::io::Error>(SandboxOutput {
            stdout: String::from_utf8_lossy(&output.stdout).to_string(),
            stderr: String::from_utf8_lossy(&output.stderr).to_string(),
            exit_code: output.status.code().unwrap_or(-1),
        })
    }).await;

    match result {
        Ok(Ok(output)) => Ok(output),
        Ok(Err(e)) => Err(SandboxError::ExecutionFailed(e.to_string())),
        Err(_) => Err(SandboxError::Timeout),
    }
}
```

Update BashTool to use sandbox for install commands:
```rust
// In bash.rs execute():
let is_install_command = is_install_class(&command);

if is_install_command {
    let backend = ava_sandbox::select_backend()?;
    let policy = SandboxPolicy {
        read_only_paths: vec!["/usr".into(), "/bin".into(), "/lib".into()],
        writable_paths: vec![cwd.clone(), "/tmp".into()],
        allow_network: true, // installs need network
        allow_process_spawn: true,
    };
    let request = SandboxRequest {
        command: "sh".into(),
        args: vec!["-c".into(), command],
        working_dir: Some(cwd),
        env: filtered_env(),
    };
    let plan = backend.build_plan(&request, &policy)?;
    let output = ava_sandbox::execute_plan(&plan, timeout).await?;
    return Ok(ToolResult { output: format_output(output), .. });
}
```

Add `is_install_class()` helper:
```rust
fn is_install_class(cmd: &str) -> bool {
    let patterns = [
        "npm install", "npm i ", "yarn add", "pnpm add",
        "pip install", "pip3 install",
        "cargo install", "cargo add",
        "apt install", "apt-get install",
        "brew install",
    ];
    patterns.iter().any(|p| cmd.contains(p))
}
```

Update `SandboxError` to include new variants:
```rust
pub enum SandboxError {
    InvalidPolicy(String),
    UnsupportedPlatform(String),
    ExecutionFailed(String),  // NEW
    Timeout,                  // NEW
}
```

### Tests
- Test: bwrap plan includes `--chdir` when working_dir set
- Test: bwrap plan includes `--setenv` for each env var
- Test: sandbox-exec plan includes working_dir in profile
- Test: execute_plan runs simple echo command (may need bwrap installed)
- Test: BashTool detects install commands and routes to sandbox
- Test: non-install commands bypass sandbox

### Acceptance Criteria
- `working_dir` and `env` fields used in both Linux and macOS plan builders
- `execute_plan()` spawns real subprocess from SandboxPlan
- BashTool routes install-class commands through sandbox
- `cargo test -p ava-sandbox` passes
- `cargo test -p ava-tools` passes (bash tool sandbox integration)

---

## Feature 5: Unified Agent Entrypoint

### What to Read
- `crates/ava-agent/src/lib.rs` — AgentLoop API
- `crates/ava-llm/src/lib.rs` — ModelRouter, providers
- `crates/ava-tools/src/core/mod.rs` — register_core_tools() (from Feature 1)
- `crates/ava-session/src/lib.rs` — SessionManager
- `crates/ava-memory/src/lib.rs` — MemorySystem
- `crates/ava-config/src/lib.rs` — ConfigManager
- `crates/ava-permissions/src/lib.rs` — permission rules

### What to Build
A high-level `AgentStack` struct that composes all crates into a ready-to-use agent. This is what the TUI (Sprint 16b) will instantiate.

**Files:**
- `crates/ava-agent/src/stack.rs` — New: AgentStack builder + runner
- `crates/ava-agent/src/lib.rs` — Export stack module

**Implementation:**

```rust
use ava_llm::{ModelRouter, provider::LLMProvider};
use ava_tools::{registry::ToolRegistry, core::register_core_tools};
use ava_session::SessionManager;
use ava_memory::MemorySystem;
use ava_config::ConfigManager;
use ava_platform::StandardPlatform;
use ava_context::ContextManager;
use std::sync::Arc;
use std::path::PathBuf;

pub struct AgentStack {
    pub router: ModelRouter,
    pub tools: Arc<ToolRegistry>,
    pub session_manager: SessionManager,
    pub memory: MemorySystem,
    pub config: ConfigManager,
    pub platform: Arc<StandardPlatform>,
}

pub struct AgentStackConfig {
    pub data_dir: PathBuf,        // ~/.ava/
    pub provider: Option<String>, // override provider
    pub model: Option<String>,    // override model
    pub max_turns: usize,
    pub yolo: bool,
}

impl Default for AgentStackConfig {
    fn default() -> Self {
        Self {
            data_dir: dirs::home_dir().unwrap_or_default().join(".ava"),
            provider: None,
            model: None,
            max_turns: 20,
            yolo: false,
        }
    }
}

impl AgentStack {
    pub fn new(config: AgentStackConfig) -> color_eyre::Result<Self> {
        let db_path = config.data_dir.join("data.db");

        // Platform
        let platform = Arc::new(StandardPlatform);

        // Tools
        let mut tools = ToolRegistry::new();
        register_core_tools(&mut tools, platform.clone());

        // LLM Router — load API keys from config/env
        let config_mgr = ConfigManager::load_or_default(&config.data_dir)?;
        let mut router = ModelRouter::new("default");
        // Register providers based on available API keys
        // (Anthropic, OpenAI, OpenRouter, Ollama, etc.)

        // Session + Memory
        let session_manager = SessionManager::new(&db_path)?;
        let memory = MemorySystem::new(&db_path)?;

        Ok(Self {
            router,
            tools: Arc::new(tools),
            session_manager,
            memory,
            config: config_mgr,
            platform,
        })
    }

    /// Run a single agent task (headless mode)
    pub async fn run(
        &self,
        goal: &str,
        max_turns: usize,
        event_tx: Option<mpsc::UnboundedSender<AgentEvent>>,
        cancel: CancellationToken,
    ) -> color_eyre::Result<AgentResult> {
        let provider = self.router.route(RoutingTaskType::CodeGeneration)?;
        let config = AgentConfig {
            max_turns,
            token_limit: 128_000,
            model: provider.model_name().to_string(),
        };
        let agent = AgentLoop::new(config);

        if let Some(tx) = event_tx {
            agent.run_streaming(goal, provider, &self.tools, move |event| {
                let _ = tx.send(event);
            }).await
        } else {
            agent.run(goal, provider, &self.tools).await
        }
    }
}
```

### Tests
`crates/ava-agent/tests/stack_test.rs`:
- Test: AgentStack::new() with default config initializes all components
- Test: tools registry has 6 core tools registered
- Test: run() with MockProvider returns completion
- Test: run() with cancellation stops early

### Acceptance Criteria
- `AgentStack::new()` creates a fully wired agent in one call
- All 6 core tools registered
- `run()` executes agent with real tool calls (via MockProvider)
- CancellationToken support works
- `cargo test -p ava-agent` passes

---

## Feature 6: Integration Test — End-to-End Agent Run

### What to Build
A comprehensive integration test that proves the entire Rust agent stack works end-to-end: AgentStack → AgentLoop → LLM (mock) → Tool execution → Session persistence.

**Files:**
- `crates/ava-agent/tests/e2e_test.rs` — Full integration test

**Implementation:**

```rust
#[tokio::test]
async fn full_agent_run_with_tool_calls() {
    // 1. Create AgentStack with MockProvider that returns tool calls
    let mock = Arc::new(MockProvider::new("test-model", vec![
        // Turn 1: Agent wants to read a file
        r#"{"tool_calls":[{"name":"read","arguments":{"path":"/tmp/test_e2e/hello.txt"}}]}"#.to_string(),
        // Turn 2: Agent wants to write a file
        r#"{"tool_calls":[{"name":"write","arguments":{"path":"/tmp/test_e2e/output.txt","content":"Hello from AVA!"}}]}"#.to_string(),
        // Turn 3: Agent completes
        r#"{"tool_calls":[{"name":"attempt_completion","arguments":{"result":"Done"}}]}"#.to_string(),
    ]));

    // 2. Set up temp directory with test file
    let dir = tempfile::tempdir().unwrap();
    let test_file = dir.path().join("hello.txt");
    std::fs::write(&test_file, "Hello World").unwrap();

    // 3. Build stack and run
    let stack = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        ..Default::default()
    }).unwrap();

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();

    let result = stack.run("Read hello.txt and write output", 10, Some(tx), cancel).await.unwrap();

    // 4. Verify
    assert!(result.success);
    assert!(result.turns >= 3);

    // 5. Verify tool actually executed
    let output_file = dir.path().join("output.txt");
    assert!(output_file.exists());
    assert_eq!(std::fs::read_to_string(output_file).unwrap(), "Hello from AVA!");

    // 6. Verify events were emitted
    let mut events = Vec::new();
    while let Ok(event) = rx.try_recv() {
        events.push(event);
    }
    assert!(!events.is_empty());
}

#[tokio::test]
async fn agent_run_with_bash_tool() {
    // Test bash tool execution through agent
    // MockProvider returns: bash("echo hello") → attempt_completion
}

#[tokio::test]
async fn agent_run_cancellation() {
    // Test that cancelling mid-run stops the agent
}

#[tokio::test]
async fn commander_multi_agent_coordination() {
    // Test Commander with 2 workers running in parallel
    // Both use MockProvider, verify results merge correctly
}
```

### Acceptance Criteria
- All e2e tests pass
- Agent can read, write, edit files through tools
- Agent can execute bash commands
- Cancellation works mid-run
- Commander coordinates multiple workers
- `cargo test --workspace` passes with zero failures

---

## Post-Implementation Verification

After ALL 6 features:

1. `cargo test --workspace` — all tests pass
2. `cargo clippy --workspace -- -D warnings` — no warnings
3. Verify core tools work: `cargo test -p ava-tools -- core`
4. Verify commander works: `cargo test -p ava-commander`
5. Verify e2e: `cargo test -p ava-agent -- e2e`
6. Commit: `git commit -m "feat(sprint-16a): complete Rust agent stack"`

---

## File Change Summary

| Action | File |
|--------|------|
| CREATE | `crates/ava-tools/src/core/mod.rs` |
| CREATE | `crates/ava-tools/src/core/read.rs` |
| CREATE | `crates/ava-tools/src/core/write.rs` |
| CREATE | `crates/ava-tools/src/core/edit.rs` |
| CREATE | `crates/ava-tools/src/core/bash.rs` |
| CREATE | `crates/ava-tools/src/core/glob.rs` |
| CREATE | `crates/ava-tools/src/core/grep.rs` |
| CREATE | `crates/ava-tools/tests/core_tools_test.rs` |
| CREATE | `crates/ava-commander/src/events.rs` |
| CREATE | `crates/ava-sandbox/src/executor.rs` |
| CREATE | `crates/ava-agent/src/stack.rs` |
| CREATE | `crates/ava-agent/tests/stack_test.rs` |
| CREATE | `crates/ava-agent/tests/e2e_test.rs` |
| MODIFY | `crates/ava-tools/src/lib.rs` (add `pub mod core;`) |
| MODIFY | `crates/ava-tools/Cargo.toml` (add glob, grep-*, ava-platform deps) |
| MODIFY | `crates/ava-commander/src/lib.rs` (remove NullProvider, add provider injection) |
| MODIFY | `crates/ava-commander/Cargo.toml` (add ava-llm, tokio-util deps) |
| MODIFY | `crates/ava-commander/tests/commander.rs` (update for new API) |
| MODIFY | `crates/ava-sandbox/src/lib.rs` (export executor) |
| MODIFY | `crates/ava-sandbox/src/linux.rs` (use working_dir, env) |
| MODIFY | `crates/ava-sandbox/src/macos.rs` (use working_dir, env) |
| MODIFY | `crates/ava-sandbox/src/error.rs` (add ExecutionFailed, Timeout variants) |
| MODIFY | `crates/ava-sandbox/Cargo.toml` (add tokio dep) |
| MODIFY | `crates/ava-agent/src/lib.rs` (export stack module) |
| MODIFY | `Cargo.toml` (workspace dep updates if needed) |
