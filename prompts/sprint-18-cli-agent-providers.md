# Sprint 18: CLI Agent Providers (Rust)

> For AI coding agent. Estimated: 6 features, mix M/L effort.
> Run `cargo test --workspace` after each feature.
> Depends on: Sprint 16a (Rust agent stack), Sprint 16c (credential store)

---

## Role

You are implementing Sprint 18 (CLI Agent Providers) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, Rust-first architecture)
- `AGENTS.md` (code standards, common workflows)
- `crates/ava-llm/src/provider.rs` (LLM provider trait)
- `crates/ava-llm/src/providers/mod.rs` (provider factory)
- `crates/ava-commander/src/lib.rs` (multi-agent orchestration)

**Context**: AVA currently calls LLM APIs directly. This sprint adds a new class of "provider" — one that spawns an external coding agent CLI (Claude Code, Gemini CLI, Codex CLI, OpenCode, Aider) as a subprocess. The user's existing subscriptions power the agents — no API keys needed for subscription-based tools.

**Why**: Anthropic, Google, and OpenAI don't allow third-party OAuth for their consumer subscriptions. But their CLIs work with the user's own login. By spawning their CLIs as subprocesses, AVA can leverage any subscription the user already has.

**Architecture**: This is a new Rust crate `crates/ava-cli-providers/` that implements the `LLMProvider` trait by spawning external CLIs.

---

## Pre-Implementation: Read Existing Code

Before writing any code, read:
- `crates/ava-llm/src/provider.rs` — LLMProvider trait (generate, generate_stream, estimate_tokens, estimate_cost, model_name)
- `crates/ava-llm/src/providers/mod.rs` — Provider registry / factory
- `crates/ava-llm/src/router.rs` — ModelRouter (needs CLI agent path)
- `crates/ava-commander/src/lib.rs` — Commander orchestration
- `crates/ava-tools/src/registry.rs` — Tool trait (for understanding tool execution)
- `crates/ava-platform/src/lib.rs` — Platform trait (shell execution reference)

---

## Feature 1: CLI Agent Config Types

### What to Build
Type definitions for CLI agent provider configuration.

**Create crate:** `crates/ava-cli-providers/`
- `Cargo.toml` with deps: `ava-types`, `ava-llm`, `serde`, `serde_json`, `tokio`, `async-trait`, `futures`
- Register in workspace `Cargo.toml`

**File:** `crates/ava-cli-providers/src/config.rs` (new)

```rust
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Configuration for wrapping a coding agent CLI as an AVA provider
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CLIAgentConfig {
    /// Unique provider name (e.g., "claude-code", "gemini-cli", "codex")
    pub name: String,
    /// CLI binary name or path (e.g., "claude", "gemini", "codex")
    pub binary: String,
    /// How to pass the prompt (e.g., "-p" for Claude, "exec" for Codex)
    pub prompt_flag: PromptMode,
    /// Flags for non-interactive mode
    pub non_interactive_flags: Vec<String>,
    /// Flags to skip permission prompts
    pub yolo_flags: Vec<String>,
    /// Flag for structured JSON output (if supported)
    pub output_format_flag: Option<String>,
    /// Flag to scope allowed tools (if supported)
    pub allowed_tools_flag: Option<String>,
    /// Flag to set working directory
    pub cwd_flag: Option<String>,
    /// Flag to set model
    pub model_flag: Option<String>,
    /// Flag to continue a session
    pub session_flag: Option<String>,
    /// Whether this CLI supports structured JSON output
    pub supports_stream_json: bool,
    /// Whether this CLI supports scoped tool permissions
    pub supports_tool_scoping: bool,
    /// Default tool scoping per Praxis tier
    pub tier_tool_scopes: Option<HashMap<String, Vec<String>>>,
    /// Command to detect if binary is installed
    pub version_command: Vec<String>,
}

/// How the CLI accepts prompts
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum PromptMode {
    /// Flag-based: binary <flag> "prompt" (e.g., claude -p "prompt")
    Flag(String),
    /// Subcommand-based: binary <subcmd> "prompt" (e.g., codex exec "prompt")
    Subcommand(String),
}

/// Result from a CLI agent execution
#[derive(Debug, Clone)]
pub struct CLIAgentResult {
    pub success: bool,
    pub output: String,
    pub exit_code: i32,
    pub events: Vec<CLIAgentEvent>,
    pub tokens_used: Option<TokenUsage>,
    pub duration_ms: u64,
}

#[derive(Debug, Clone)]
pub struct TokenUsage {
    pub input: u64,
    pub output: u64,
}

/// Parsed event from CLI agent stream-json output
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum CLIAgentEvent {
    #[serde(rename = "text")]
    Text { content: String },
    #[serde(rename = "tool_use")]
    ToolUse {
        tool_name: String,
        #[serde(default)]
        tool_args: serde_json::Value,
    },
    #[serde(rename = "tool_result")]
    ToolResult {
        tool_name: String,
        result: String,
    },
    #[serde(rename = "error")]
    Error { message: String },
    #[serde(rename = "usage")]
    Usage { input_tokens: u64, output_tokens: u64 },
    #[serde(other)]
    Unknown,
}
```

### Tests
- `crates/ava-cli-providers/src/config.rs` (inline tests)
- Test: CLIAgentConfig serializes/deserializes
- Test: PromptMode variants
- Test: CLIAgentEvent parses from JSON

---

## Feature 2: CLI Agent Runner (Core Engine)

### What to Build
The subprocess spawner that runs any CLI agent and parses its output.

**File:** `crates/ava-cli-providers/src/runner.rs` (new)

```rust
use crate::config::{CLIAgentConfig, CLIAgentEvent, CLIAgentResult, PromptMode, TokenUsage};
use ava_types::Result;
use std::process::Stdio;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::Command;
use tokio_util::sync::CancellationToken;

pub struct CLIAgentRunner {
    config: CLIAgentConfig,
    cancel: CancellationToken,
}

pub struct RunOptions {
    pub prompt: String,
    pub cwd: String,
    pub model: Option<String>,
    pub yolo: bool,
    pub allowed_tools: Option<Vec<String>>,
    pub session_id: Option<String>,
    pub timeout_ms: Option<u64>,
    pub env: Option<Vec<(String, String)>>,
}

impl CLIAgentRunner {
    pub fn new(config: CLIAgentConfig) -> Self;

    /// Check if the CLI binary is installed and accessible
    pub async fn is_available(&self) -> bool;

    /// Get the CLI version string
    pub async fn version(&self) -> Option<String>;

    /// Run the CLI agent with a prompt, return structured result
    pub async fn run(&self, options: RunOptions) -> Result<CLIAgentResult>;

    /// Run with streaming — send events through a channel
    pub async fn stream(
        &self,
        options: RunOptions,
        tx: tokio::sync::mpsc::Sender<CLIAgentEvent>,
    ) -> Result<CLIAgentResult>;

    /// Cancel a running agent
    pub fn cancel(&self);

    /// Build the command args from config + options
    fn build_args(&self, options: &RunOptions) -> Vec<String>;

    /// Parse a line of stream-json output into an event
    fn parse_event(line: &str) -> Option<CLIAgentEvent>;
}
```

**Implementation:**
- Spawn CLI as child process via `tokio::process::Command`
- Build args array from config + options using `build_args()`
- If `supports_stream_json`: parse stdout line-by-line as JSON -> `CLIAgentEvent`
- If not: collect all stdout as plain text -> single output
- Stream stderr for progress/logging
- Handle timeout with `tokio::time::timeout`
- Handle cancellation with `CancellationToken`
- Handle exit codes: 0 = success, non-zero = failure
- Track duration via `std::time::Instant`
- Extract token usage from Usage events

**Arg building:**
```rust
fn build_args(&self, options: &RunOptions) -> Vec<String> {
    let mut args = Vec::new();

    // Prompt mode
    match &self.config.prompt_flag {
        PromptMode::Flag(flag) => {
            args.push(flag.clone());
            args.push(options.prompt.clone());
        }
        PromptMode::Subcommand(cmd) => {
            args.push(cmd.clone());
            args.push(options.prompt.clone());
        }
    }

    // Non-interactive
    args.extend(self.config.non_interactive_flags.clone());

    // YOLO mode
    if options.yolo {
        args.extend(self.config.yolo_flags.clone());
    }

    // Output format
    if self.config.supports_stream_json {
        if let Some(ref flag) = self.config.output_format_flag {
            args.push(flag.clone());
            args.push("stream-json".to_string());
        }
    }

    // Tool scoping
    if let (Some(tools), Some(ref flag)) = (&options.allowed_tools, &self.config.allowed_tools_flag) {
        args.push(flag.clone());
        args.push(tools.join(","));
    }

    // CWD
    if let Some(ref flag) = self.config.cwd_flag {
        args.push(flag.clone());
        args.push(options.cwd.clone());
    }

    // Model
    if let (Some(ref model), Some(ref flag)) = (&options.model, &self.config.model_flag) {
        args.push(flag.clone());
        args.push(model.clone());
    }

    // Session
    if let (Some(ref session), Some(ref flag)) = (&options.session_id, &self.config.session_flag) {
        args.push(flag.clone());
        args.push(session.clone());
    }

    args
}
```

### Tests
- Test: Builds correct args for Claude Code (flag-based prompt)
- Test: Builds correct args for Codex (subcommand-based prompt)
- Test: Yolo flags included when yolo=true
- Test: Tool scoping args when supported
- Test: Parse stream-json line into CLIAgentEvent::Text
- Test: Parse stream-json line into CLIAgentEvent::ToolUse
- Test: Parse stream-json line into CLIAgentEvent::Usage
- Test: Invalid JSON line returns None
- Test: `is_available()` returns false for nonexistent binary

---

## Feature 3: Built-in CLI Agent Configs

### What to Build
Pre-configured `CLIAgentConfig` for each major coding agent CLI.

**File:** `crates/ava-cli-providers/src/configs.rs` (new)

```rust
use crate::config::{CLIAgentConfig, PromptMode};
use std::collections::HashMap;

/// Get all built-in CLI agent configs
pub fn builtin_configs() -> HashMap<String, CLIAgentConfig> {
    let mut configs = HashMap::new();
    configs.insert("claude-code".into(), claude_code_config());
    configs.insert("gemini-cli".into(), gemini_cli_config());
    configs.insert("codex".into(), codex_config());
    configs.insert("opencode".into(), opencode_config());
    configs.insert("aider".into(), aider_config());
    configs
}

fn claude_code_config() -> CLIAgentConfig { /* ... */ }
fn gemini_cli_config() -> CLIAgentConfig { /* ... */ }
fn codex_config() -> CLIAgentConfig { /* ... */ }
fn opencode_config() -> CLIAgentConfig { /* ... */ }
fn aider_config() -> CLIAgentConfig { /* ... */ }
```

**Config details:**

| CLI | Binary | Prompt | YOLO | Stream JSON | Tool Scoping |
|-----|--------|--------|------|-------------|--------------|
| Claude Code | `claude` | `-p` (flag) | `--dangerously-skip-permissions` | Yes (`--output-format stream-json`) | Yes (`--allowedTools`) |
| Gemini CLI | `gemini` | `-p` (flag) | `--yolo` | No | No |
| Codex CLI | `codex` | `exec` (subcommand) | `--full-auto` | Yes (`--json`) | No |
| OpenCode | `opencode` | `run` (subcommand) | auto in headless | No | No |
| Aider | `aider` | `--message` (flag) | `--yes-always` | No | No |

**Claude Code config (reference):**
```rust
CLIAgentConfig {
    name: "claude-code".into(),
    binary: "claude".into(),
    prompt_flag: PromptMode::Flag("-p".into()),
    non_interactive_flags: vec!["--no-user-prompt".into()],
    yolo_flags: vec!["--dangerously-skip-permissions".into()],
    output_format_flag: Some("--output-format".into()),
    allowed_tools_flag: Some("--allowedTools".into()),
    cwd_flag: Some("--cwd".into()),
    model_flag: Some("--model".into()),
    session_flag: Some("--session-id".into()),
    supports_stream_json: true,
    supports_tool_scoping: true,
    tier_tool_scopes: Some(HashMap::from([
        ("engineer".into(), vec!["Edit", "Write", "Bash", "Read", "Glob", "Grep"]
            .into_iter().map(String::from).collect()),
        ("reviewer".into(), vec!["Read", "Bash", "Glob", "Grep"]
            .into_iter().map(String::from).collect()),
        ("subagent".into(), vec!["Read", "Glob", "Grep"]
            .into_iter().map(String::from).collect()),
    ])),
    version_command: vec!["claude".into(), "--version".into()],
}
```

### Tests
- Test: All 5 configs have required fields
- Test: Binary names are correct
- Test: Claude Code supports stream-json + tool scoping
- Test: Codex uses subcommand prompt mode
- Test: Aider uses flag prompt mode with `--message`

---

## Feature 4: LLMProvider Trait Implementation

### What to Build
Implement the `LLMProvider` trait so CLI agents can be used anywhere a regular LLM provider can.

**File:** `crates/ava-cli-providers/src/provider.rs` (new)

```rust
use async_trait::async_trait;
use ava_llm::LLMProvider;
use ava_types::{Message, Result};
use futures::Stream;
use std::pin::Pin;

use crate::config::CLIAgentConfig;
use crate::runner::{CLIAgentRunner, RunOptions};

/// Wraps a CLI agent as an LLMProvider
pub struct CLIAgentLLMProvider {
    runner: CLIAgentRunner,
    model_name: String,
    yolo: bool,
}

impl CLIAgentLLMProvider {
    pub fn new(config: CLIAgentConfig, model: Option<String>, yolo: bool) -> Self;
}

#[async_trait]
impl LLMProvider for CLIAgentLLMProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        // Convert messages to a single prompt string
        // The last user message is the primary prompt
        // System messages become context prefix
        let prompt = messages_to_prompt(messages);

        let result = self.runner.run(RunOptions {
            prompt,
            cwd: std::env::current_dir()?.to_string_lossy().to_string(),
            model: Some(self.model_name.clone()),
            yolo: self.yolo,
            ..Default::default()
        }).await?;

        if result.success {
            Ok(result.output)
        } else {
            Err(AvaError::ProviderError(format!(
                "CLI agent exited with code {}: {}", result.exit_code, result.output
            )))
        }
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        // Create channel, spawn runner in background, yield text events
        let (tx, rx) = tokio::sync::mpsc::channel(256);
        let prompt = messages_to_prompt(messages);
        let runner = self.runner.clone(); // Runner needs to be Clone

        tokio::spawn(async move {
            let _ = runner.stream(RunOptions {
                prompt,
                cwd: std::env::current_dir().unwrap().to_string_lossy().to_string(),
                ..Default::default()
            }, tx).await;
        });

        // Convert mpsc receiver to Stream, filtering for text events
        Ok(Box::pin(futures::stream::unfold(rx, |mut rx| async {
            loop {
                match rx.recv().await {
                    Some(CLIAgentEvent::Text { content }) => return Some((content, rx)),
                    Some(_) => continue, // skip non-text events
                    None => return None,
                }
            }
        })))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        // Rough estimate: ~4 chars per token
        input.len() / 4
    }

    fn estimate_cost(&self, _input_tokens: usize, _output_tokens: usize) -> f64 {
        // CLI agents use the user's subscription — no API cost
        0.0
    }

    fn model_name(&self) -> &str {
        &self.model_name
    }
}

/// Convert AVA messages to a single prompt string for CLI agents
fn messages_to_prompt(messages: &[Message]) -> String {
    // Build a prompt that the CLI agent can understand
    // System messages -> "Context: ..."
    // User messages -> the actual request
    // Assistant messages -> prior conversation
    // Focus on the last user message as the primary prompt
    todo!()
}
```

### Tests
- Test: `messages_to_prompt` with single user message
- Test: `messages_to_prompt` with system + user messages
- Test: `estimate_cost` returns 0.0 (subscription-based)
- Test: `model_name` returns configured name
- Test: Provider creation from config

---

## Feature 5: CLI Agent Discovery & Registration

### What to Build
Auto-discover installed CLI agents and register them as providers in the ModelRouter.

**File:** `crates/ava-cli-providers/src/discovery.rs` (new)

```rust
use crate::config::CLIAgentConfig;
use crate::configs::builtin_configs;
use crate::provider::CLIAgentLLMProvider;
use crate::runner::CLIAgentRunner;
use std::collections::HashMap;

/// Discovered CLI agent with version info
#[derive(Debug, Clone)]
pub struct DiscoveredAgent {
    pub name: String,
    pub binary: String,
    pub version: String,
    pub config: CLIAgentConfig,
}

/// Discover which CLI agents are installed on this system
pub async fn discover_agents() -> Vec<DiscoveredAgent> {
    let configs = builtin_configs();
    let mut discovered = Vec::new();

    // Check all in parallel
    let mut handles = Vec::new();
    for (name, config) in configs {
        let name = name.clone();
        handles.push(tokio::spawn(async move {
            let runner = CLIAgentRunner::new(config.clone());
            if runner.is_available().await {
                let version = runner.version().await.unwrap_or_else(|| "unknown".into());
                Some(DiscoveredAgent {
                    name,
                    binary: config.binary.clone(),
                    version,
                    config,
                })
            } else {
                None
            }
        }));
    }

    for handle in handles {
        if let Ok(Some(agent)) = handle.await {
            discovered.push(agent);
        }
    }

    discovered
}

/// Create LLMProvider instances from discovered agents
pub fn create_providers(
    agents: &[DiscoveredAgent],
    yolo: bool,
) -> HashMap<String, Box<dyn ava_llm::LLMProvider>> {
    let mut providers = HashMap::new();
    for agent in agents {
        let provider_name = format!("cli:{}", agent.name);
        let provider = CLIAgentLLMProvider::new(
            agent.config.clone(),
            None, // Use default model
            yolo,
        );
        providers.insert(provider_name, Box::new(provider) as Box<dyn ava_llm::LLMProvider>);
    }
    providers
}
```

### Tests
- Test: `discover_agents()` returns empty for system with no agents installed (mock)
- Test: `create_providers` prefixes names with `cli:`
- Test: Discovery runs checks in parallel
- Test: Unavailable agents excluded from results

---

## Feature 6: Praxis Integration — CLI Agents as Tier Backends

### What to Build
Wire CLI agent providers into the Commander so any Praxis tier can use a CLI agent.

**File:** `crates/ava-cli-providers/src/bridge.rs` (new)

```rust
use crate::config::{CLIAgentResult, CLIAgentEvent};
use crate::runner::{CLIAgentRunner, RunOptions};
use ava_types::Result;

/// Agent role for tier-specific configuration
#[derive(Debug, Clone, Copy)]
pub enum AgentRole {
    Engineer,
    Reviewer,
    Subagent,
}

/// Execute a task using a CLI agent with tier-appropriate settings
pub async fn execute_with_cli_agent(
    runner: &CLIAgentRunner,
    task: &str,
    role: AgentRole,
    cwd: &str,
    files: Option<&[String]>,
    event_tx: Option<tokio::sync::mpsc::Sender<CLIAgentEvent>>,
) -> Result<CLIAgentResult> {
    // Build tier-appropriate prompt
    let prompt = build_tier_prompt(task, role, files);

    // Build tier-appropriate options
    let options = RunOptions {
        prompt,
        cwd: cwd.to_string(),
        yolo: matches!(role, AgentRole::Engineer),
        allowed_tools: get_tier_tools(runner, role),
        timeout_ms: get_tier_timeout(role),
        ..Default::default()
    };

    // Stream or run based on whether we have an event channel
    if let Some(tx) = event_tx {
        runner.stream(options, tx).await
    } else {
        runner.run(options).await
    }
}

fn build_tier_prompt(task: &str, role: AgentRole, files: Option<&[String]>) -> String {
    let files_ctx = files
        .map(|f| format!("\nRelevant files: {}", f.join(", ")))
        .unwrap_or_default();

    match role {
        AgentRole::Engineer => format!(
            "You are an engineer. Implement the following task:\n\n{task}{files_ctx}\n\n\
             Write clean, tested code. Commit when done."
        ),
        AgentRole::Reviewer => format!(
            "Review these changes for correctness, style, and potential bugs. \
             Run lint and tests to verify.{files_ctx}\n\nTask context: {task}"
        ),
        AgentRole::Subagent => format!(
            "Research the following and report your findings:\n\n{task}{files_ctx}"
        ),
    }
}

fn get_tier_tools(runner: &CLIAgentRunner, role: AgentRole) -> Option<Vec<String>> {
    // Use tier_tool_scopes from config if the CLI supports tool scoping
    let role_key = match role {
        AgentRole::Engineer => "engineer",
        AgentRole::Reviewer => "reviewer",
        AgentRole::Subagent => "subagent",
    };
    runner.config()
        .tier_tool_scopes
        .as_ref()
        .and_then(|scopes| scopes.get(role_key).cloned())
}

fn get_tier_timeout(role: AgentRole) -> Option<u64> {
    match role {
        AgentRole::Engineer => Some(600_000),  // 10 minutes
        AgentRole::Reviewer => Some(300_000),  // 5 minutes
        AgentRole::Subagent => Some(120_000),  // 2 minutes
    }
}
```

**Also modify:** `crates/ava-commander/` to add CLI agent routing:
- When a tier's provider starts with `cli:`, route through `ava-cli-providers` bridge
- Add `ava-cli-providers` as optional dependency of `ava-commander`

### Tests
- Test: Engineer prompt includes task and files
- Test: Reviewer prompt focuses on review
- Test: Subagent prompt focuses on research
- Test: Engineer gets yolo=true, reviewer/subagent get yolo=false
- Test: Tool scoping applied per tier when supported
- Test: Timeouts differ by tier

---

## Crate Structure

```text
crates/ava-cli-providers/
├── Cargo.toml
└── src/
    ├── lib.rs
    ├── config.rs       # Types: CLIAgentConfig, CLIAgentResult, CLIAgentEvent
    ├── runner.rs        # CLIAgentRunner: subprocess spawner
    ├── configs.rs       # Built-in configs for 5 CLI agents
    ├── provider.rs      # LLMProvider trait implementation
    ├── discovery.rs     # Auto-discover installed CLIs
    └── bridge.rs        # Praxis tier integration
```

---

## Post-Implementation Verification

After ALL 6 features:

1. `cargo test -p ava-cli-providers` — crate tests
2. `cargo test --workspace` — full workspace
3. `cargo clippy --workspace` — no warnings
4. Verify: `cargo build -p ava-cli-providers` compiles cleanly
5. Manual test: If `claude` is installed, verify `is_available()` returns true
6. Commit: `git commit -m "feat(sprint-18): CLI agent providers as Rust crate"`

---

## File Change Summary

| Action | File |
|--------|------|
| CREATE | `crates/ava-cli-providers/Cargo.toml` |
| CREATE | `crates/ava-cli-providers/src/lib.rs` |
| CREATE | `crates/ava-cli-providers/src/config.rs` |
| CREATE | `crates/ava-cli-providers/src/runner.rs` |
| CREATE | `crates/ava-cli-providers/src/configs.rs` |
| CREATE | `crates/ava-cli-providers/src/provider.rs` |
| CREATE | `crates/ava-cli-providers/src/discovery.rs` |
| CREATE | `crates/ava-cli-providers/src/bridge.rs` |
| MODIFY | `Cargo.toml` (add ava-cli-providers to workspace members) |
| MODIFY | `crates/ava-commander/Cargo.toml` (add optional ava-cli-providers dep) |
| MODIFY | `crates/ava-commander/src/lib.rs` (add CLI agent routing) |
