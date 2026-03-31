# F14: Session Backgrounding — Gap Analysis

Sprint 61 | 2026-03-31

## Summary

Compare AVA's current backgrounding capabilities (`--later`, sub-agents, background tasks) with Claude Code's Ctrl+B session backgrounding. Identify gaps and design recommendations for Sprint 62.

---

## Claude Code's Approach

### Ctrl+B Session Backgrounding

Claude Code uses Ctrl+B as a dual-purpose background key:

1. **Foreground task backgrounding**: If shell commands or agent tasks are running in the foreground, Ctrl+B backgrounds them immediately (process continues, UI detaches).
2. **Session backgrounding**: If no foreground tasks but a query is in progress, Ctrl+B uses a double-press pattern (first press shows hint, second press within 800ms backgrounds the entire session).

**Key implementation details:**
- `useSessionBackgrounding.ts` hook manages Ctrl+B logic
- `SessionBackgroundHint.tsx` displays eligibility hint
- Tasks have an `isBackgrounded` boolean property
- Backgrounded tasks continue executing via the underlying process/agent
- Output stored to disk via `TaskOutputPath`
- Stall watchdog monitors commands waiting on user input
- Tasks can be re-foregrounded (Ctrl+B again syncs messages back to main view)

### Task System

Claude Code's task system tracks 6 task types:
- `LocalShellTask` — background shell commands
- `LocalAgentTask` — sub-agents running locally
- `RemoteAgentTask` — agents on remote machines
- `InProcessTeammateTask` — parallel teammate agents
- `DreamTask` — background "dreaming" processes
- `RemoteSession` — remote session tasks

Management via `/tasks` command and task tools (`TaskCreate`, `TaskList`, `TaskGet`, `TaskOutput`, `TaskStop`).

---

## AVA's Current Capabilities

### Three-Tier Mid-Stream Messaging (B34)

AVA has a more structured approach to deferred work:

| Tier | Trigger | Behavior |
|------|---------|----------|
| **Steering** | Enter (TUI), stdin (headless) | Interrupts after current tool, sends immediately |
| **Follow-up** | Alt+Enter, `--follow-up` | Runs after agent finishes current task |
| **Post-complete** | Alt+Ctrl+Enter, `--later`, `--later-group` | Grouped pipeline after all work done |

Post-complete groups (G1, G2, G3...) execute sequentially with budget awareness — no competitor has this grouped pipeline concept.

### Sub-Agent Backgrounding

The `TaskTool` supports `background: true`:
- Spawns sub-agent in parallel via `spawn_background()`
- Main agent continues working immediately
- Emits `SubAgentComplete` event when done
- Returns session ID for tracking

### Background Task Infrastructure

`BackgroundState` in `crates/ava-tui/src/state/background.rs`:
- Tracks task ID, goal, status (Running/Completed/Failed), timing, token/cost metrics
- `CancellationToken` for stopping tasks
- Worktree path/branch for isolation
- Ctrl+B keybinding already registered as `BackgroundAgent`

---

## Gap Analysis

### What AVA Has That Claude Code Doesn't

| Feature | AVA | Claude Code |
|---------|-----|-------------|
| Grouped post-complete pipeline | G1→G2→G3 sequential execution | No equivalent |
| Budget-aware message tiers | Skips remaining if budget exhausted | No budget tracking |
| Three distinct priority tiers | Steering/Follow-up/Post-complete | Single background queue |
| CLI flags for deferred work | `--later`, `--later-group`, `--follow-up` | No CLI backgrounding flags |

### What Claude Code Has That AVA Doesn't

| Feature | Claude Code | AVA Status |
|---------|-------------|------------|
| Session detach/reattach | Ctrl+B detaches, re-Ctrl+B reattaches | Keybind registered, no detach/reattach |
| Foreground task backgrounding | Background running shell/agent mid-execution | Sub-agent only (not main session) |
| Task output persistence | Writes to disk, survives restarts | In-memory only |
| Stall watchdog | Detects commands waiting on input | Not implemented |
| Remote agent tasks | Tracks agents on remote machines | Local only |
| Dream tasks | Background "dreaming" processes | No equivalent |
| Double-press confirmation UX | First press=hint, second=action | Not implemented |

### Key Gaps (Priority Order)

1. **Session detach/reattach** (HIGH): The main gap. AVA can background sub-agents but cannot background the main session. The Ctrl+B keybind is already registered (`BackgroundAgent` action) but not wired to session-level detach.

2. **Task output persistence** (MEDIUM): Background task output is in-memory. If the TUI crashes, background task results are lost. Should persist to `~/.ava/tasks/{session_id}/`.

3. **Foreground task backgrounding** (MEDIUM): Currently you can only background sub-agents at spawn time. Claude Code lets you background a running foreground task mid-execution.

4. **Double-press confirmation UX** (LOW): Nice UX pattern for destructive/mode-changing actions. Could apply to other actions too (e.g., cancel confirmation).

---

## Design Recommendations for Sprint 62

### Phase 1: Session Detach/Reattach

1. **Wire Ctrl+B to session detach**:
   - Save full agent state (messages, tools, pending queue) to `~/.ava/sessions/{id}/state.json`
   - Agent loop continues in background thread
   - TUI exits cleanly with "Session backgrounded" message
   - Output buffered to disk

2. **Session resume**:
   - `ava --resume {session_id}` or `ava --resume latest`
   - `/sessions` shows backgrounded sessions with status badge
   - Reattach syncs buffered messages to TUI

3. **Completion notification**:
   - Desktop: system notification via `notify-rust`
   - CLI: write completion marker to `~/.ava/sessions/{id}/done`

### Phase 2: Task Persistence

1. Persist `BackgroundTask` to SQLite (extend `ava-session`)
2. Persist task output to `~/.ava/tasks/{session_id}/{task_id}.jsonl`
3. Survive TUI restart — reload on next launch

### Phase 3: Mid-Execution Backgrounding

1. Allow backgrounding a running foreground tool (not just at spawn)
2. Requires moving tool execution to a separate tokio task with channel-based result delivery
3. Already partially done with `CancellationToken` infrastructure

---

## Conclusion

AVA's three-tier messaging system is architecturally superior to Claude Code's simpler background queue — the grouped pipeline concept is novel and useful. The main gap is **session-level detach/reattach**, which Claude Code handles well with Ctrl+B. The infrastructure is already partially in place (Ctrl+B keybind, BackgroundState, CancellationToken), making this a natural Sprint 62 feature.
