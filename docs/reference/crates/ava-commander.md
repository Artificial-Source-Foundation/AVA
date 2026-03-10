# ava-commander

> Multi-agent orchestration with domain-specific leads, workflow pipelines, and code review.

## Overview

`ava-commander` implements multi-agent coordination for AVA. It provides two orchestration modes:

1. **Commander pattern** -- Domain-specific leads (Frontend, Backend, QA, etc.) that spawn workers and execute tasks in parallel, merging results into a combined session.
2. **Workflow pipelines** -- Sequential multi-phase execution (Planner -> Coder -> Reviewer) with output chaining and feedback loops.

The crate also includes a full-featured **code review agent** that collects git diffs, runs an LLM-powered review, and produces structured output with severity-tagged issues, verdicts, and multiple output formats.

All orchestration is built on top of `ava-agent::AgentLoop` -- each worker or phase runs its own `AgentLoop` instance with a role-appropriate tool set and system prompt.

## Architecture

```
Commander (lib.rs)                    WorkflowExecutor (workflow.rs)
    |                                     |
    v                                     v
Lead[7 domains]                     Phase[N phases]
    |                                     |
    v                                     v
Worker (AgentLoop + Task)           run_phase_worker (AgentLoop)
    |                                     |
    v                                     v
run_worker() -> Session             Session -> extract_phase_output()
    |                                     |
    v                                     v
coordinate() -> merged Session      execute() -> merged Session


ReviewAgent (review.rs)
    |
    v
collect_diff() -> ReviewContext
    |
    v
run_review_agent() -> raw text
    |
    v
parse_review_output() -> ReviewResult
    |
    v
format_text/json/markdown()
```

## Key Types

### Commander
`crates/ava-commander/src/lib.rs:38`

```rust
pub struct Commander {
    leads: Vec<Lead>,
    budget: Budget,
}
```

The top-level orchestrator. Created with a `CommanderConfig` that specifies a default provider, optional per-domain providers, platform, and budget. On construction, it creates 7 domain leads (Frontend, Backend, QA, Research, Debug, Fullstack, DevOps).

### CommanderConfig
`crates/ava-commander/src/lib.rs:43`

```rust
pub struct CommanderConfig {
    pub budget: Budget,
    pub default_provider: Arc<dyn LLMProvider>,
    pub domain_providers: HashMap<Domain, Arc<dyn LLMProvider>>,
    pub platform: Option<Arc<StandardPlatform>>,
}
```

- `provider_for(domain)` returns the domain-specific provider or falls back to the default.
- `apply_cli_tier_routes()` (feature-gated behind `cli-providers`) integrates CLI-discovered providers with `cli:` prefix routing.

### Domain
`crates/ava-commander/src/lib.rs:87`

```rust
pub enum Domain {
    Frontend,
    Backend,
    QA,
    Research,
    Debug,
    Fullstack,
    DevOps,
}
```

Used for task routing. The `pick_domain()` method (line 323) maps `TaskType` to `Domain`:

| TaskType | Routed to Domain |
|----------|-----------------|
| Planning | Fullstack |
| CodeGeneration | Backend |
| Testing / Review | QA |
| Research | Research |
| Debug | Debug |
| Simple | Fullstack |

### Lead
`crates/ava-commander/src/lib.rs:78`

```rust
pub struct Lead {
    name: String,
    domain: Domain,
    workers: Vec<Worker>,
    provider: Arc<dyn LLMProvider>,
    platform: Option<Arc<StandardPlatform>>,
}
```

A domain-specific team lead that owns a provider and can spawn workers. `spawn_worker()` (line 363) creates a `Worker` with:
- **Half the budget** of the parent (tokens, turns, cost) -- `max(1)` floor for turns/tokens.
- A fresh `ToolRegistry` with core tools registered via the platform.
- Its own `AgentLoop` wrapped in `Arc<Mutex<AgentLoop>>`.

### Worker
`crates/ava-commander/src/lib.rs:97`

```rust
pub struct Worker {
    id: Uuid,
    lead: String,
    agent: Arc<Mutex<AgentLoop>>,
    budget: Budget,
    task: Task,
    provider: Arc<dyn LLMProvider>,
}
```

Workers are `Clone` (via `Arc::clone` on the agent). Each worker runs its task description through its `AgentLoop` in streaming mode.

### Budget
`crates/ava-commander/src/lib.rs:106`

```rust
pub struct Budget {
    pub max_tokens: usize,
    pub max_turns: usize,
    pub max_cost_usd: f64,
}
```

Shared budget type used by Commander, Leads, Workers, and Workflows.

### Task
`crates/ava-commander/src/lib.rs:113`

```rust
pub struct Task {
    pub description: String,
    pub task_type: TaskType,
    pub files: Vec<String>,
}
```

### CommanderEvent
`crates/ava-commander/src/events.rs:4`

Events emitted during multi-agent execution for UI consumption:

| Variant | Purpose |
|---------|---------|
| `WorkerStarted { worker_id, lead, task_description }` | Worker begins execution |
| `WorkerProgress { worker_id, turn, max_turns }` | Turn progress update |
| `WorkerToken { worker_id, token }` | Streamed text token from a worker |
| `WorkerCompleted { worker_id, success, turns }` | Worker finished successfully |
| `WorkerFailed { worker_id, error }` | Worker encountered a fatal error |
| `AllComplete { total_workers, succeeded, failed }` | All workers done |
| `Summary { total_workers, succeeded, failed, total_turns }` | Final summary |
| `PhaseStarted { phase_index, phase_count, phase_name, role }` | Workflow phase begins |
| `PhaseCompleted { phase_index, phase_name, turns, output_preview }` | Phase finished |
| `IterationStarted { iteration, max_iterations }` | Feedback loop iteration begins |
| `WorkflowComplete { phases_completed, total_phases, iterations, total_turns }` | Workflow done |

### Workflow Types

#### Workflow
`crates/ava-commander/src/workflow.rs:52`

```rust
pub struct Workflow {
    pub name: String,
    pub phases: Vec<Phase>,
    pub max_iterations: usize,
}
```

#### Phase
`crates/ava-commander/src/workflow.rs:43`

```rust
pub struct Phase {
    pub name: String,
    pub role: PhaseRole,
    pub system_prompt_override: Option<String>,
    pub max_turns: Option<usize>,
    pub receives_prior_output: bool,
}
```

#### PhaseRole
`crates/ava-commander/src/workflow.rs:21`

```rust
pub enum PhaseRole {
    Planner,   // Read-only tools (read, glob, grep)
    Coder,     // All core tools
    Reviewer,  // Read-only tools
    Tester,    // All core tools
    Custom(String), // All core tools
}
```

### Review Types

#### ReviewResult
`crates/ava-commander/src/review.rs:17`

```rust
pub struct ReviewResult {
    pub summary: String,
    pub issues: Vec<ReviewIssue>,
    pub positives: Vec<String>,
    pub verdict: ReviewVerdict,
    pub raw_output: String,
}
```

#### ReviewIssue
`crates/ava-commander/src/review.rs:25`

```rust
pub struct ReviewIssue {
    pub severity: Severity,     // Nitpick < Suggestion < Warning < Critical
    pub file: Option<String>,
    pub line: Option<usize>,
    pub description: String,
}
```

#### ReviewVerdict
`crates/ava-commander/src/review.rs:67`

```rust
pub enum ReviewVerdict {
    Approve,         // No critical or warning issues
    RequestChanges,  // Critical issues or multiple warnings
    Comment,         // Suggestions/nitpicks only
}
```

#### DiffMode
`crates/ava-commander/src/review.rs:98`

```rust
pub enum DiffMode {
    Staged,          // git diff --staged
    Working,         // git diff
    Commit(String),  // git show <sha>
    Range(String),   // git diff <range>
}
```

## Flows

### Commander: Delegate and Coordinate

1. **Delegate** (`commander.delegate(task)`, line 193):
   - `pick_domain()` selects the appropriate domain based on `task.task_type`.
   - Finds the matching `Lead` and calls `spawn_worker()`.
   - Worker gets half the commander's budget, a fresh tool registry with core tools, and its own `AgentLoop`.

2. **Coordinate** (`commander.coordinate(workers, cancel, event_tx)`, line 204):
   - All workers run **in parallel** via `join_all()`.
   - Each worker runs inside `tokio::select!` with cancellation and timeout support (timeout = `max_turns * 60` seconds).
   - Events are emitted as workers start, progress, complete, or fail.
   - Results are **merged** into a combined `Session`:
     - Each worker's messages are prefixed with `[worker-{id}: {lead}]` system message.
     - Error workers get an error system message.
     - Final summary message with success/failure counts.

### Workflow Execution

`WorkflowExecutor::execute()` at `crates/ava-commander/src/workflow.rs:306`:

1. **Budget division** -- Total turns and cost divided equally across phases.
2. **Phase iteration** -- For each phase in order:
   - Build the phase goal: if `receives_prior_output` is true and there is prior output, prepend "Original goal: ... Output from previous phase: ..."
   - `run_phase_worker()` creates an `AgentLoop` with role-appropriate tools and system prompt, runs it in streaming mode with cancellation.
   - `extract_phase_output()` (line 246) takes the last 2 assistant messages (capped at 4000 chars) as the phase output.
   - Phase messages are added to the combined session with `[phase-{idx}: {name} ({role})]` markers.
3. **Feedback loop** -- If the workflow has a `Reviewer` phase and `iteration < max_iterations`:
   - `needs_revision()` (line 266) checks the reviewer output for revision signals ("fix", "bug", "missing", etc.) vs. approval signals ("lgtm", "approved").
   - If revision needed, the loop continues with the reviewer feedback as prior output for the next Coder phase.
4. **Completion** -- `WorkflowComplete` event emitted with phase/iteration/turn counts.

### Preset Workflows

| Name | Phases | Max Iterations |
|------|--------|---------------|
| `plan-code-review` | Planner -> Coder -> Reviewer | 2 |
| `code-review` | Coder -> Reviewer | 2 |
| `plan-code` | Planner -> Coder | 1 |

Resolved via `Workflow::from_name()` (line 141).

### Tool Registration by Role

`register_tools_for_role()` at `crates/ava-commander/src/workflow.rs:223`:

| Role | Tools |
|------|-------|
| Planner | read, glob, grep (read-only) |
| Reviewer | read, glob, grep (read-only) |
| Coder | All core tools |
| Tester | All core tools |
| Custom | All core tools |

### Code Review Flow

`crates/ava-commander/src/review.rs`

1. **Collect diff** (`collect_diff()`, line 130):
   - Runs `git diff` (or `git show` for commits) via `tokio::process::Command`.
   - Truncates diffs larger than 50KB.
   - Parses `git diff --stat` output into `DiffStats` (file, insertions, deletions).

2. **Build review prompt** (`build_review_system_prompt()`, line 187):
   - Takes a focus area: `"security"`, `"performance"`, `"bugs"`, `"style"`, or general.
   - Instructs the model to output structured feedback with `## Summary`, `## Issues`, `## Positives`, `## Verdict` sections.
   - Issues use `### [severity] file:line - description` format.

3. **Run review agent** (`run_review_agent()`, line 440):
   - Creates an `AgentLoop` with core tools and the review system prompt.
   - Runs in streaming mode, printing tokens to stderr for live output.
   - Returns the raw text output.

4. **Parse output** (`parse_review_output()`, line 233):
   - Extracts `Summary`, `Issues`, `Positives`, `Verdict` sections via regex and string matching.
   - Issues parsed with regex: `### [severity] file:line - description`
   - Severity classified via `from_str_loose()`: "critical"/"error"/"bug" -> Critical, "warn" -> Warning, "suggest"/"improvement" -> Suggestion, else Nitpick.
   - Verdict: looks for "REQUEST_CHANGES", "APPROVE", defaults to "COMMENT".
   - Graceful fallback: unstructured text becomes the summary with Comment verdict.

5. **Format output** -- Three formatters:
   - `format_text()` -- ANSI-colored terminal output
   - `format_json()` -- Serialized `ReviewResult`
   - `format_markdown()` -- GitHub-compatible markdown

6. **Exit code** (`determine_exit_code()`, line 425):
   - Returns 1 if any issue meets or exceeds the severity threshold, 0 otherwise.
   - Useful for CI integration.

## Configuration

### Commander Budget
Set via `CommanderConfig.budget`. Workers receive half the budget (floor of 1 for turns/tokens).

### Workflow Budget
Set via `WorkflowExecutor::new()`. Divided equally across phases (per-phase turns = total / phase_count, per-phase cost = total / phase_count).

### Domain Provider Routing
`CommanderConfig.domain_providers` maps `Domain` variants to specific LLM providers. Falls back to `default_provider` for unmapped domains.

### CLI Provider Integration (feature: `cli-providers`)
When the `cli-providers` feature is enabled, `apply_cli_tier_routes()` discovers external CLI providers and routes domains with `cli:` prefixed names to them.

## Dependencies

### Depends on
| Crate | Purpose |
|-------|---------|
| `ava-agent` | `AgentLoop`, `AgentConfig`, `AgentEvent` -- core loop for each worker/phase |
| `ava-llm` | `LLMProvider`, `SharedProvider` -- LLM access |
| `ava-tools` | `ToolRegistry`, `register_core_tools`, individual tool types |
| `ava-context` | `ContextManager` -- per-worker context windows |
| `ava-platform` | `StandardPlatform` -- file system abstraction |
| `ava-types` | Shared types (Message, Session, Tool, AvaError) |
| `ava-cli-providers` | Optional: external CLI provider discovery |
| `regex` | Review output parsing |
| `uuid` | Worker and phase IDs |

### Depended on by
| Crate | Purpose |
|-------|---------|
| `ava-tui` | TUI binary (multi-agent and workflow commands) |
| `ava-agent` | Dev dependency for integration tests |

## Examples

### Running a workflow

```rust
use ava_commander::{Workflow, WorkflowExecutor, Budget};

let workflow = Workflow::from_name("plan-code-review").unwrap();
let budget = Budget {
    max_tokens: 128_000,
    max_turns: 30,
    max_cost_usd: 9.0,
};

let executor = WorkflowExecutor::new(
    workflow,
    budget,
    provider.clone(),
    platform.clone(),
);

let session = executor.execute(
    "Add pagination to the API",
    cancel_token,
    event_tx,
).await?;
```

### Using the Commander for parallel tasks

```rust
use ava_commander::{Commander, CommanderConfig, Budget, Task, TaskType};

let config = CommanderConfig {
    budget: Budget { max_tokens: 128_000, max_turns: 20, max_cost_usd: 5.0 },
    default_provider: provider.clone(),
    domain_providers: HashMap::new(),
    platform: Some(platform.clone()),
};

let mut commander = Commander::new(config);
let worker = commander.delegate(Task {
    description: "Add unit tests for the auth module".to_string(),
    task_type: TaskType::Testing,
    files: vec!["src/auth.rs".to_string()],
})?;

let session = commander.coordinate(vec![worker], cancel, event_tx).await?;
```

### Running a code review

```rust
use ava_commander::review::*;

let ctx = collect_diff(&DiffMode::Staged).await?;
let system_prompt = build_review_system_prompt("security");
let raw_output = run_review_agent(
    provider, platform, &ctx, &system_prompt, 10,
).await?;

let result = parse_review_output(&raw_output);
println!("{}", format_text(&result));

let exit_code = determine_exit_code(&result, Severity::Warning);
std::process::exit(exit_code);
```
