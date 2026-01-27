# Delta9 Robustness Recommendations

> Comprehensive improvement plan from live testing (2026-01-26)

---

## Executive Summary

The system is solid for happy-path orchestration. The gaps are mostly around **failure recovery**, **observability**, and **safety guardrails** - the stuff that matters when things go wrong in production.

### Top 10 Priorities

1. Squadron partial success handling
2. Unified event log
3. Mission-squadron task linking
4. Council simulation mode clarity
5. Rollback capability
6. Token/cost tracking
7. Better error messages
8. File scope limits
9. Agent reasoning traces
10. Idempotency guarantees

---

## Category A: Failure Recovery & Resilience

### A1. Agent Death Recovery

**Problem:** If an operator crashes mid-task, there's no automatic recovery.

**Suggestions:**
- Heartbeat mechanism for long-running agents
- Auto-respawn with context recovery (checkpoint what was done)
- `agent_health_check` before dispatching work
- Configurable retry policy per agent type: `{ maxRetries: 2, backoffMs: 5000 }`

### A2. Partial Success Handling

**Problem:** Squadron Wave 2 had 3 operators. 2 succeeded, 1 got stuck. No way to "accept partial" and continue.

**Suggestions:**
- `wave_policy`: `"all" | "majority" | "any" | "n_of_m"`
- Allow manual wave advancement: `squadron_advance_wave(squadronId, force: true)`
- Quarantine failed agents but continue mission
- Rollback capability if partial success corrupts state

### A3. Cascading Failure Prevention

**Problem:** If Objective 1 fails, should Objective 2 even attempt? Currently unclear.

**Suggestions:**
- Dependency graph between objectives (not just tasks)
- `on_failure`: `"abort" | "skip_dependents" | "continue" | "ask"`
- Circuit breaker pattern: after N failures, pause mission for human review

### A4. Idempotency Guarantees

**Problem:** If I retry a failed task, will it duplicate work?

**Suggestions:**
- Task state should include "files touched"
- Re-running should detect "already done" conditions
- Git integration: create branch per task, easy rollback
- Checksum validation: "file changed since last read, abort or force?"

---

## Category B: State Management

### B1. Mission State Persistence

**Problem:** If my Claude session dies, does mission state survive?

**Suggestions:**
- `mission_resume(missionId)` - explicit reconnection
- Show mission state on session start if one exists
- Lock mechanism: prevent two commanders from running same mission
- State sync: if file and memory diverge, which wins?

### B2. Distributed State Consistency

**Problem:** Squadron agents run in parallel. If two touch the same file, who wins?

**Suggestions:**
- File locking is implemented (`lock_file`) but not auto-enforced
- Make locking MANDATORY for operators editing files
- Conflict detection: "Agent B tried to edit file locked by Agent A"
- Merge strategy for non-conflicting edits to same file

### B3. Context Handoff Between Agents

**Problem:** When Operator finishes and Validator starts, how much context transfers?

**Suggestions:**
- Structured handoff object: `{ filesChanged: [], summary: "", keyDecisions: [] }`
- Validator receives operator's final context snapshot
- "Memory" that persists across agent handoffs (scoped to mission)

### B4. State Machine Visualization

**Problem:** Hard to understand current state without calling multiple tools.

**Suggestions:**
- `mission_diagram` - ASCII or mermaid diagram of current state
- Color-coded: green=done, yellow=in-progress, red=failed, gray=pending
- Show blocked tasks and WHY they're blocked

---

## Category C: Observability & Debugging

### C1. Unified Event Log

**Problem:** To understand what happened, I had to piece together multiple tool outputs.

**Suggestions:**
- `mission_log(missionId, since?, level?)` - unified event stream
- Events: task_started, task_completed, agent_spawned, validation_failed, etc.
- Structured JSON for programmatic parsing
- Optional: stream to external logging (webhook)

### C2. Agent Reasoning Traces

**Problem:** Operators are black boxes. I dispatch, they return result. What did they TRY?

**Suggestions:**
- Capture agent's internal reasoning/attempts
- `get_agent_trace(taskId)` - see what the agent considered
- Store failed attempts too (tried X, failed because Y)

### C3. Token/Cost Tracking

**Problem:** I have no idea how much this mission cost in tokens.

**Suggestions:**
- Per-agent token tracking
- Per-mission cost rollup
- Budget alerts: "Mission at 80% of budget"
- Cost optimization hints

### C4. Timing Metrics

**Problem:** Some agents took 3+ minutes. Was that normal?

**Suggestions:**
- Breakdown: queue_time, execution_time, network_time
- Percentile tracking
- Anomaly detection: "Agent unusually slow, possible issue"

---

## Category D: Safety & Guardrails

### D1. Blast Radius Limits

**Problem:** An operator could theoretically edit ANY file. No sandbox.

**Suggestions:**
- Per-task file allowlist: `allowedFiles: ["src/audio.py"]`
- Per-mission directory scope: `scope: "src/"`
- Dangerous operation confirmation
- "Dry run" mode

### D2. Rollback Capability

**Problem:** If squadron makes bad changes, how do I undo?

**Suggestions:**
- Auto-checkpoint before each task
- `mission_rollback(to: "before_task_X")`
- Git branch per mission
- Keep "before" snapshots of edited files

### D3. Human-in-the-Loop Gates

**Problem:** Some tasks might need human approval before proceeding.

**Suggestions:**
- Task flag: `requiresApproval: true`
- Mission pauses at gate, notifies human
- Approval with comments
- Timeout: auto-abort if no approval in N hours

### D4. Secrets Detection

**Problem:** Agent might accidentally log or commit secrets.

**Suggestions:**
- Scan agent outputs for secret patterns
- Block commits with detected secrets
- Redact secrets from logs/traces

---

## Category E: Developer Experience

### E1. Better Error Messages

**Problem:** "Task is pending, not in progress" - okay but WHY?

**Suggestions:**
- Include current state in error
- Suggest fix: "Did you mean to call X instead?"
- Link to docs for complex errors

### E2. Interactive Mode

**Problem:** Everything is async fire-and-forget.

**Suggestions:**
- `agent_interactive(prompt, stream: true)`
- Ability to interrupt/redirect mid-task
- "Copilot mode"

### E3. Templates & Presets

**Problem:** Setting up a mission is verbose.

**Suggestions:**
- Mission templates: `mission_from_template("bug-fix", { file: "x.py" })`
- Common patterns: "add-feature", "refactor", "fix-bug", "add-tests"
- Save successful missions as templates

### E4. CLI/TUI Dashboard

**Problem:** Flying blind without visual feedback.

**Suggestions:**
- Real-time TUI showing mission progress
- Agent status indicators
- Log tail in separate pane

---

## Category F: Architecture & Extensibility

### F1. Plugin System for Agents

**Problem:** Adding a new specialist requires code changes.

**Suggestions:**
- Agent definition files (YAML/JSON)
- Custom agent loading: `agents/my-specialist.yaml`
- Composable agents

### F2. Webhook/Event System

**Problem:** Can't integrate with external systems easily.

**Suggestions:**
- Webhook config: `onTaskComplete: "https://slack.com/..."`
- Bidirectional: receive commands via webhook too

### F3. Multi-Mission Orchestration

**Problem:** Can only run one mission at a time.

**Suggestions:**
- Mission queue with priorities
- Parallel missions (with resource limits)
- Mission dependencies

### F4. Model Fallback Chains

**Problem:** If Claude is rate-limited, mission stops.

**Suggestions:**
- Fallback config: `[claude-opus, gpt-4, gemini-pro]`
- Auto-fallback on rate limit or error
- Track which model actually handled each task

---

## Category G: Testing & Validation

### G1. Mission Simulation Mode

**Problem:** Can't test mission structure without executing.

**Suggestions:**
- `mission_simulate()` - run through without calling agents
- Validate: dependencies, file existence, criteria clarity
- Estimate: token cost, time, risk level

### G2. Acceptance Criteria Validation

**Problem:** Criteria are free-text. No way to auto-verify.

**Suggestions:**
- Structured criteria: `{ type: "file_exists", path: "x.py" }`
- Auto-verification where possible
- Criteria templates

### G3. Regression Prevention

**Problem:** After fixing one thing, might break another.

**Suggestions:**
- Auto-run tests after each task
- Lint check on modified files
- Type check on modified files

---

## Category H: Quick Wins

| Suggestion | Effort | Impact |
|------------|--------|--------|
| Add `mode: "simulation"` prominently in council response | 1 hour | HIGH |
| Better error messages with suggestions | 2 hours | HIGH |
| `mission_status` one-liner summary | 1 hour | MED |
| Auto-link squadron tasks to mission tasks | 4 hours | HIGH |
| Token tracking per agent | 2 hours | MED |
| Wave timeout with force-advance option | 3 hours | HIGH |
| `mission_log` unified event stream | 4 hours | HIGH |
| File allowlist per task | 3 hours | MED |

---

## Implementation Status

| Category | Items | Implemented | Priority |
|----------|-------|-------------|----------|
| A. Failure Recovery | 4 | 0 | HIGH |
| B. State Management | 4 | 1 (partial) | HIGH |
| C. Observability | 4 | 1 (partial) | HIGH |
| D. Safety | 4 | 1 (partial) | MEDIUM |
| E. Developer Experience | 4 | 2 (partial) | MEDIUM |
| F. Architecture | 4 | 1 (partial) | LOW |
| G. Testing | 3 | 0 | MEDIUM |
| H. Quick Wins | 8 | 0 | HIGH |

---

*Generated from Commander's comprehensive brain dump after live testing.*
