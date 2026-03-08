# Sprint 46: Multi-Agent Workflows

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Transform AVA's Commander from a simple task delegator into a true multi-agent workflow engine. After this sprint, AVA can run a **planner→coder→reviewer** pipeline where one agent plans the work, another writes the code, and a third reviews it — with feedback loops and automatic iteration.

## Key Files to Read

```
crates/ava-commander/src/lib.rs          # Commander, Lead, Worker, Domain, Task, TaskType, Budget
crates/ava-commander/src/events.rs       # CommanderEvent (7 variants)
crates/ava-commander/tests/commander.rs  # Existing tests (domain routing, coordination, cancellation)
crates/ava-commander/Cargo.toml

crates/ava-agent/src/loop.rs             # AgentLoop, AgentEvent, AgentConfig
crates/ava-agent/src/stack.rs            # AgentStack, AgentRunResult
crates/ava-agent/src/system_prompt.rs    # build_system_prompt()

crates/ava-tui/src/headless.rs           # run_multi_agent() — current --multi-agent CLI
crates/ava-tui/src/config/cli.rs         # CliArgs (--multi-agent flag)

crates/ava-tools/src/registry.rs         # ToolRegistry, Tool trait
crates/ava-tools/src/core/mod.rs         # register_core_tools()
```

## What Already Exists

- **Commander**: Domain-based leads (Frontend, Backend, QA, Research, Debug, Fullstack, DevOps)
- **Task delegation**: `pick_domain()` routes by TaskType
- **Worker coordination**: `coordinate()` runs workers concurrently via `join_all`, merges sessions
- **Events**: WorkerStarted, WorkerProgress, WorkerToken, WorkerCompleted, WorkerFailed, AllComplete, Summary
- **Budget**: max_tokens, max_turns, max_cost_usd — halved per worker
- **CLI**: `--multi-agent` flag triggers `run_multi_agent()` in headless mode
- **Agent loop**: Streaming, tool execution, stuck detection, self-correction, context compaction

## Theme 1: Workflow Pipeline Engine

### Story 1.1: Workflow Definition

Define multi-step workflows as a sequence of phases that feed output into the next phase.

**Implementation:**
- File: `crates/ava-commander/src/workflow.rs` (NEW)

```rust
pub struct Workflow {
    pub name: String,
    pub phases: Vec<Phase>,
}

pub struct Phase {
    pub name: String,
    pub role: PhaseRole,
    pub system_prompt_override: Option<String>,
    pub max_turns: usize,
    pub receives_prior_output: bool,
}

pub enum PhaseRole {
    Planner,    // Analyzes the goal, produces a structured plan
    Coder,      // Implements the plan, writes/edits code
    Reviewer,   // Reviews changes, provides feedback
    Tester,     // Runs tests, validates changes
    Custom(String),
}
```

- Add `Workflow::plan_code_review()` factory for the default 3-phase pipeline
- Each phase gets its own system prompt tailored to its role
- The output of phase N is prepended to the goal of phase N+1

**Acceptance criteria:**
- Workflow struct defines ordered phases
- `plan_code_review()` creates a 3-phase pipeline
- Phases have distinct roles and system prompts
- Add tests for workflow construction

### Story 1.2: Phase-Specific System Prompts

Create role-specific system prompts that focus each agent on its phase.

**Implementation:**
- File: `crates/ava-commander/src/prompts.rs` (NEW)

**Prompts:**

| Role | System Prompt Focus |
|------|-------------------|
| Planner | "You are a planning agent. Analyze the request, break it into concrete steps. Output a numbered plan. Do NOT write code — only plan." |
| Coder | "You are a coding agent. You receive a plan and implement it step by step. Read files before editing. Run tests after changes." |
| Reviewer | "You are a code review agent. Review the changes made. Check for bugs, style issues, missing tests. Output specific feedback with file:line references." |
| Tester | "You are a testing agent. Run the test suite, analyze failures, and fix broken tests." |

- Each prompt should include the tools available for that role
- Planner gets read-only tools (read, glob, grep, codebase_search)
- Coder gets all tools
- Reviewer gets read-only tools + diagnostics + lint
- Tester gets all tools

**Acceptance criteria:**
- Each PhaseRole has a dedicated system prompt
- Tool filtering per role works correctly
- Prompts are concise and focused
- Add tests

### Story 1.3: Workflow Executor

Execute a workflow by running phases sequentially, passing output between them.

**Implementation:**
- In `crates/ava-commander/src/workflow.rs`, add:

```rust
impl Commander {
    pub async fn execute_workflow(
        &mut self,
        workflow: Workflow,
        goal: &str,
        cancel: CancellationToken,
        event_tx: mpsc::UnboundedSender<CommanderEvent>,
    ) -> Result<WorkflowResult> {
        let mut phase_outputs: Vec<PhaseOutput> = Vec::new();

        for (i, phase) in workflow.phases.iter().enumerate() {
            // Build goal with prior phase output
            let phase_goal = build_phase_goal(goal, &phase, &phase_outputs);

            // Create worker with role-specific tools and prompt
            let worker = self.spawn_phase_worker(&phase, &phase_goal)?;

            // Run phase
            let session = self.run_phase(worker, cancel.clone(), event_tx.clone()).await?;

            phase_outputs.push(PhaseOutput {
                phase_name: phase.name.clone(),
                role: phase.role.clone(),
                session,
            });

            // Emit phase completion event
            event_tx.send(CommanderEvent::PhaseCompleted { ... });
        }

        Ok(WorkflowResult { phase_outputs })
    }
}
```

**Acceptance criteria:**
- Phases execute sequentially
- Output of phase N feeds into phase N+1
- Phase completion events emitted
- Cancellation stops the current phase and skips remaining
- Budget is split across phases (not per-worker halving)
- Add integration test with MockProvider

### Story 1.4: Feedback Loop (Reviewer → Coder Iteration)

When the reviewer finds issues, automatically send feedback back to the coder for another iteration.

**Implementation:**
- Add `max_iterations: usize` to `Workflow` (default: 2)
- After the Reviewer phase, if the output contains actionable feedback (not just "LGTM"):
  1. Parse reviewer output for issues
  2. Create a new Coder phase with the feedback as input
  3. Run Coder again
  4. Run Reviewer again
  5. Repeat up to `max_iterations` times
- Detection heuristic: if reviewer output contains words like "fix", "bug", "issue", "error", "missing", "should", "needs" — it has actionable feedback. If it says "LGTM", "looks good", "approved" — stop iterating.

**Acceptance criteria:**
- Reviewer feedback triggers coder re-iteration
- Max iterations respected
- "LGTM" stops the loop
- Events track iteration count
- Add test: reviewer gives feedback → coder iterates → reviewer approves

## Theme 2: CLI & TUI Integration

### Story 2.1: Workflow CLI Commands

Expose workflows via CLI.

**Implementation:**
- Add `--workflow` flag to `CliArgs`:
  ```rust
  #[arg(long, value_parser = ["plan-code-review", "code-review", "plan-code"])]
  pub workflow: Option<String>,
  ```
- When `--workflow` is set, use `execute_workflow()` instead of single agent
- `--multi-agent` continues to work as before (simple delegation)

**Workflows:**
| Name | Phases |
|------|--------|
| `plan-code-review` | Planner → Coder → Reviewer (with feedback loop) |
| `code-review` | Coder → Reviewer |
| `plan-code` | Planner → Coder |

**Acceptance criteria:**
- `--workflow plan-code-review` runs the full pipeline
- Events stream to stdout/stderr correctly
- JSON mode outputs phase events
- Add to headless.rs

### Story 2.2: Workflow Events

Add new CommanderEvent variants for workflow progress.

**Implementation:**
- Add to `CommanderEvent`:
```rust
PhaseStarted {
    phase_index: usize,
    phase_name: String,
    role: String,
},
PhaseCompleted {
    phase_index: usize,
    phase_name: String,
    success: bool,
    turns: usize,
},
IterationStarted {
    iteration: usize,
    max_iterations: usize,
    reason: String,
},
WorkflowComplete {
    total_phases: usize,
    total_iterations: usize,
    success: bool,
},
```

**Acceptance criteria:**
- All new events emitted at correct points
- Headless mode displays them
- JSON mode serializes them
- Add tests

### Story 2.3: TUI Workflow Status

Show workflow progress in the TUI when running interactively.

**Implementation:**
- In `AgentState`, add workflow tracking:
  ```rust
  pub workflow_phase: Option<(usize, String, String)>,  // (index, name, role)
  pub workflow_iteration: Option<(usize, usize)>,       // (current, max)
  ```
- Status bar shows: `Phase 2/3: Coder │ Iteration 1/2`
- Message list shows phase transitions as separator messages

**Acceptance criteria:**
- Status bar shows current phase and iteration
- Phase transitions visible in message list
- Works in both TUI and headless mode

## Implementation Order

1. Story 1.1 (workflow definition) — data structures
2. Story 1.2 (phase prompts) — role-specific behavior
3. Story 2.2 (workflow events) — needed by executor
4. Story 1.3 (workflow executor) — core engine
5. Story 1.4 (feedback loop) — iteration logic
6. Story 2.1 (CLI commands) — user-facing
7. Story 2.3 (TUI status) — polish

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Don't break existing `--multi-agent` behavior
- Don't break existing Commander delegation API
- Phase workers should go through the permission system
- Budget should be divided across phases (e.g., 3 phases = 1/3 each), not the existing halving logic
- Keep workflow definitions simple — no YAML/config files this sprint (code-defined only)

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
cargo test -p ava-commander -- --nocapture

# Manual test (requires provider)
cargo run --bin ava -- "Add a health check endpoint to src/main.rs" --headless --workflow plan-code-review --provider openrouter --model anthropic/claude-sonnet-4
```
