# Delta9 Backlog

> Live Testing Session 2 - Post Robustness Overhaul

---

## Test 2 Status (2026-01-27)

| Category | Count | Status |
|----------|-------|--------|
| **Critical Bugs** | 4 | ~~BUG-12~~, ~~BUG-13~~, ~~BUG-14~~, BUG-15 |
| **Medium Bugs** | 3 | BUG-10, BUG-11, ~~BUG-16~~ |
| **Enhancements** | 18 | ENH-1 through ENH-18 |
| **Cleanup** | 1 | CLEANUP-1 (remove budget) |
| **Config Refactor** | ✅ | All models now config-driven |
| **Sprint 1-3** | Complete | Committed |
| **Sprint 4** | ✅ | BUG-12, BUG-13, BUG-14, BUG-16 fixed |

**Testing:** Unfollow Manager feature implementation (COMPLETED - code works, tracking didn't)

**Commander's Assessment:** "Delta9 orchestration worked to get the feature built, but mission tracking layer didn't keep up. Code delegation and parallel execution was solid - bookkeeping broke down."

---

## Test 2 Issues

### BUG-12: delegate_task Crashes - agent.name undefined ✅ FIXED

**Sprint 4 Fix:** Added `AGENT_ALIASES` map and `resolveAgentType()` function in `src/tools/delegation.ts`. Agent names like `ui_ops` are now correctly resolved to `uiOps` before dispatch.

---

**Original Issue:**

**Observed:** `delegate_task` tool throws TypeError when dispatching to operator.

**Error:**
```
TypeError: undefined is not an object (evaluating 'agent.name')
    at createUserMessage (src/session/prompt.ts:832:14)
```

**Context:** Happens when Commander tries to delegate a task with `agent=operator` or `agent=ui_ops`.

**Impact:** CRITICAL - Task delegation completely broken, Operators never spawn

**Root Cause:** `agent` object is undefined when passed to `createUserMessage()`.

**Location:** `src/session/prompt.ts:832` (this is in **OpenCode**, not Delta9)

**Analysis:**
- Delta9's `delegate_task` tool passes `agent=operator` or `agent=ui_ops`
- OpenCode receives this and tries to look up the agent
- Agent lookup fails → `agent` is undefined
- Crash when accessing `agent.name`

**Possible Causes:**
1. Delta9 agent names don't match OpenCode's expected format
2. Delta9 agents not properly registered with OpenCode's agent registry
3. Missing agent definition export from Delta9 plugin
4. OpenCode expects different agent parameter format

**ROOT CAUSE FOUND:**

Delta9 only registers 3 agents in `getAgentConfigs()`:
- `commander` ✅
- `operator` ✅
- `validator` ✅

But Commander's `delegate_task` is trying to dispatch to:
- `ui_ops` ❌ NOT REGISTERED
- `qa` ❌ NOT REGISTERED

OpenCode can't find `ui_ops` in the agent registry → `agent` is undefined → crash.

**Fix Options:**

1. **Quick Fix:** Commander should only use `agent=operator` for all implementation tasks
   - Update Commander prompt to only reference "operator" not specialized agents
   - Update `delegate_task` tool validation to only accept registered agents

2. **Proper Fix:** Register all Delta Team agents with OpenCode
   - Add `ui_ops`, `qa`, `recon`, `sigint`, etc. to `getAgentConfigs()`
   - Map Delta Team codenames to OpenCode agent configs

3. **Defensive Fix:** Add null check in OpenCode's `createUserMessage`
   - Graceful error: "Agent 'ui_ops' not found" instead of crash

---

### BUG-10: Commander Not Delegating Codebase Scanning 🟡

**Observed:** Commander scraped the codebase itself (Glob, Read) instead of invoking RECON agent.

**Expected:** Commander should delegate reconnaissance tasks to support agents (RECON/Librarian).

**Impact:** Medium - Violates Commander discipline (should never do tasks agents can do)

**What Happened:**
```
→ Read CLAUDE.md           ← Commander did this itself
✱ Glob "src/bot/**/*.ts"   ← Commander did this itself
✱ Glob "src/actions/**/*.ts"
✱ Glob "src/scraping/**/*.ts"
→ Read src/bot/managers/unfollow-manager.ts  ← Should be RECON
→ Read src/renderer/pages/Unfollow.tsx
→ Read src/scraping/scraper.ts
→ Read src/database/current-followers.ts
→ Read src/actions/unfollow.ts
```

**Comparison:** OpenCode's Sisyphus/Planner reliably delegates to subagents for discovery.

**Root Cause:** TBD - Possible causes:
- Commander prompt doesn't emphasize delegation for initial discovery
- No automatic RECON trigger on "read CLAUDE.md" type requests
- Missing compliance rule for codebase scanning
- Compliance hooks not catching Glob/Read as "reconnaissance" tools

**Proposed Fix:**
1. Add compliance rule: `glob/read → must_delegate_to_recon` (for exploration)
2. Update Commander prompt to explicitly delegate discovery tasks
3. Consider auto-invoking RECON on mission start
4. Add hook that intercepts bulk file reads and suggests RECON

---

### BUG-11: Background Tasks Not Visible (CTRL+X Missing) 🟡

**Observed:** No CTRL+X combo visible during test. Last test had background task indicators.

**Expected:** Background agent tasks should show CTRL+X shortcut for management.

**Impact:** Medium - User can't see/manage background tasks

**Root Cause:** TBD - Possible causes:
- Background manager not being used for dispatched tasks
- UI integration issue with OpenCode
- Tasks running synchronously instead of background

**What Works:**
- Mission creation ✅
- Objective/task creation ✅
- `delegate_task` / `dispatch_task` being called ✅

**What's Missing:**
- Background task spawning visibility
- CTRL+X management interface

---

### BUG-13: dispatch_task Self-Conflict Bug ✅ FIXED

**Sprint 4 Fix:** Added `&& t.id !== newTask.id` filter in `checkTaskConflicts()` in `src/mission/conflict-detector.ts:144-145`. Tasks are now excluded from their own conflict comparison.

---

**Original Issue:**

**Observed:** `dispatch_task` detected conflicts with *itself* - same taskId listed twice in conflict check.

**Error:** Tool blocks dispatch claiming task conflicts with... the same task.

**Impact:** CRITICAL - Cannot use dispatch_task at all

**Root Cause:** Conflict detection logic comparing task against itself in the list.

**Workaround Used:** Bypassed with `delegate_task` instead.

---

### BUG-14: delegate_task Doesn't Sync with Mission ✅ FIXED

**Sprint 4 Fix:** Added `state.startTask(taskId, agentType)` calls in `src/tools/delegation.ts` for both background and synchronous execution paths. Tasks are now marked `in_progress` when delegate_task launches.

---

**Original Issue:**

**Observed:** Work completed via `delegate_task` but mission status shows 0% complete.

**Expected:** When `taskId` is provided to `delegate_task`, it should update mission task status.

| What Should Happen | What Actually Happens |
|-------------------|----------------------|
| Task marked `in_progress` on dispatch | Status unchanged |
| Task marked `completed` when done | Status unchanged |
| Mission shows progress | Mission shows 0% |

**Impact:** CRITICAL - Mission tracking completely disconnected from actual work.

---

### BUG-15: task_complete Fails - "Task not in progress" 🔴 CRITICAL

**Observed:** Cannot mark tasks complete because they were never marked `in_progress`.

**Error:** "Task not in progress" when calling `task_complete`.

**Root Cause:** `dispatch_task` never marked task as `in_progress` due to BUG-13.

**Impact:** CRITICAL - Cannot track task completion even manually.

**Fix:** Either auto-mark `in_progress` on dispatch, or remove this requirement.

---

### BUG-16: ui_ops Agent Repeated Failures ✅ FIXED

**Sprint 4 Fix:** Created `src/lib/agent-fallback.ts` with circuit breaker pattern. `AgentFallbackManager` tracks failures per agent and automatically routes to fallback agents when an agent fails N times (default: 3). Uses CLOSED → OPEN → HALF_OPEN state machine with cooldown.

---

**Original Issue:**

**Observed:** ui_ops (Gemini) failed 3 consecutive times with "JSON Parse error: Unexpected EOF".

**Expected:** After 2 failures, should auto-fallback to operator.

**Impact:** Medium - Wasted 3 attempts before manual intervention.

**Root Cause:** Gemini returning malformed JSON responses.

**Workaround Used:** Manually switched to `operator` agent.

---

## Test 2 Observations

### Working ✅
- [x] Commander uses brainstorming skill for creative work
- [x] Commander creates missions with objectives/tasks
- [x] Commander attempts to delegate to Operators (via delegate_task)
- [x] Context pruning/extraction (DCP) active
- [x] Mission planning structure correct
- [x] Background task IDs generated (bg_* format)
- [x] Parallel task dispatch attempted (5 tasks at once)
- [x] mission_update, task_complete tools exist
- [x] background_list, background_output tools working

### Confirmed Issues ❌
- [x] **BUG-12 CONFIRMED**: `agent=ui_ops` causes crash (agent not registered)
- [x] **BUG-12 CONFIRMED**: `agent=operator` also failing with same error
- [x] Commander does recon itself instead of delegating to RECON
- [x] Background tasks created but UI tasks had JSON Parse errors
- [x] Commander correctly identified failed UI tasks and retried with `agent=operator`
- [x] Retry still failing due to agent lookup issue (root cause is registration)

### Partial ⚠️
- Background tasks ARE being created (bg_* IDs visible)
- But execution failing due to agent lookup errors
- CTRL+X visibility issue may be secondary to the crash

### Test Flow Observed:
1. Commander created mission "Unfollow Manager Implementation"
2. 5 objectives created, 10 tasks total
3. Parallel dispatch: 5 background tasks at once (bg_* IDs)
   - 3 UI tasks → `agent=ui_ops` → FAILED (agent not registered)
   - 2 Backend tasks → `agent=operator` → FAILED (same root cause)
4. Commander detected failures via `background_output` tool
5. Commander attempted retry: combined UI work → `agent=operator`
6. Retry also failed due to agent registration issue

### Observations from UI:
- Token count: 67,815 (34% context)
- Version: ev1.1.36
- Commander model: Claude Opus 4.5 (latest) Anthropic · max
- Footer shows: `esc interrupt`, `ctrl+t variants`, `tab agents`, `ctrl+p commands`

### Still Watching:
- [ ] Does Council get invoked for planning decisions?
- [ ] Do fallback chains activate on rate limits?
- [ ] Do reasoning traces capture decisions?
- [ ] Does Operator actually execute when agent lookup is fixed?

---

## Enhancement Requests

### ENH-1: User-Configurable Fallback Models ✅ DONE

**Status:** IMPLEMENTED - All models and fallbacks now config-driven.

**Changes Made:**
- Added `fallbacks: string[]` to all agent configs
- Removed hardcoded `ORACLE_FALLBACKS` and `AGENT_FALLBACKS`
- `getOracleFallbackChain()` and `getAgentFallbackChain()` now read from config
- All routing code uses `loadConfig()` instead of hardcoded values

---

### ENH-2: Auto-Sync delegate_task with Mission 🔴 HIGH

**Request:** When `taskId` is provided to `delegate_task`, automatically update mission:
- Mark task `in_progress` on dispatch
- Mark task `completed` or `failed` when done
- Update mission progress percentage

**Why:** Currently work completes but mission shows 0%.

---

### ENH-3: Agent Auto-Fallback on Failure 🔴 HIGH

**Request:** If an agent fails N times (default 2), auto-fallback to next agent:
- ui_ops fails 2x → operator
- Use agent's configured fallback chain
- Log the fallback for debugging

**Why:** Don't keep trying a broken model.

---

### ENH-4: Better Error Messages 🟠 MEDIUM

**Request:** Improve error messages with actionable context:
- "JSON Parse error: Unexpected EOF" → "Gemini returned incomplete response. Consider using operator instead."
- "Agent not found" → "Agent 'ui_ops' not registered. Available: commander, operator, validator"

---

### ENH-5: Unified run_task Tool 🟠 MEDIUM

**Request:** Single tool that replaces dispatch_task + delegate_task confusion:

```typescript
run_task({
  taskId: 'task_xyz',  // Optional - syncs with mission if provided
  prompt: '...',
  agent: 'auto',       // Auto-routes to best agent
  background: true     // Auto-tracks in background
})
```

**Why:** Currently confusing which tool to use.

---

### ENH-6: execute_objective Tool 🟠 MEDIUM

**Request:** Run all tasks in an objective automatically:
- Handles task dependencies
- Parallel when no deps, sequential when blocked
- Auto-retries on failure
- Commander only intervenes on failures

**Why:** Commander currently dispatches each task manually.

---

### ENH-7: Mission Progress Sync Command 🟠 MEDIUM

**Request:** Tool `mission_sync` that scans completed work and updates task statuses:
- Looks at git changes
- Checks file modifications
- Updates tasks that match criteria

---

### ENH-8: Live Mission Dashboard 🟢 LOW

**Request:** Real-time TUI showing:
```
┌─────────────────────────────────────────┐
│ Mission: Selective Unfollow    [75%]    │
├─────────────────────────────────────────┤
│ ✅ Obj 1: State Management    [2/2]     │
│ 🔄 Obj 2: UI Components       [2/4]     │
│   ├─ ✅ Checkboxes                      │
│   ├─ 🔄 Unfollow Button (bg_xyz)        │
│ ⏳ Obj 3: Backend             [0/2]     │
└─────────────────────────────────────────┘
```

---

### ENH-9: Agent Performance Metrics 🟢 LOW

**Request:** Track agent performance over time:
```
Agent      | Success | Avg Time | Failures | Cost
-----------|---------|----------|----------|------
operator   | 95%     | 2.3m     | 2        | $0.45
ui_ops     | 40%     | 0.2m     | 3        | $0.05  ⚠️
```

Auto-adjust routing based on success rate.

---

### ENH-10: Background Task Health Monitoring 🟢 LOW

**Request:** Detect "stale" tasks earlier:
- Task running > N minutes without output → alert
- Auto-retry on timeout
- Show health status in background_list

---

### ENH-11: Task Replay/Debug 🟢 LOW

**Request:** Debug tool to replay task execution:
```
$ delta9 replay task_xyz
Shows: prompt sent, files read, edits made, result
```

---

### ENH-12: Dry Run Mode 🟢 LOW

**Request:** Preview what would happen:
```
$ delta9 plan --dry-run
"Would dispatch 5 tasks to 3 agents, est. 8 minutes"
```

---

### ENH-13: Resumable Missions 🟢 LOW

**Request:** On new session, detect incomplete mission:
- "Resume mission? [Y/n]"
- Picks up where left off
- Shows what was completed

---

### ENH-14: Learned Patterns 🟢 LOW

**Request:** Delta9 notices patterns and auto-adjusts:
- "UI tasks with Gemini fail 60% of time"
- Auto-routes UI tasks to operator
- Stores in knowledge base

---

### ENH-15: Smart Checkpointing 🟢 LOW

**Request:** Auto-checkpoint triggers:
- After each objective completes
- Before risky operations (schema changes)
- Every N successful tasks

---

### ENH-16: Squadron Templates 🟢 LOW

**Request:** Pre-built patterns:
- "feature" → scout + intel + operator + validator
- "bugfix" → scout + operator + qa
- "refactor" → explorer + operator_complex + validator_strict

---

### ENH-17: Human Checkpoints 🟢 LOW

**Request:** Mark tasks as `needs_review`:
- Pauses before execution
- Shows plan to user
- Continues on approval

Good for: DB migrations, API changes, security-sensitive code.

---

### ENH-18: Event-Driven Architecture 🔵 ARCHITECTURE

**Request:** Replace polling with event bus:
- Tasks emit: started, progress, completed, failed
- Commander subscribes and reacts
- UI can also subscribe for live updates

---

## Cleanup Items (Post-Test)

### CLEANUP-1: Remove Delta9 Budget Tracking

**Reason:** OpenCode already handles budget/cost tracking natively. Delta9's budget system is redundant.

**Action:** Deprecate or remove:
- `src/lib/budget.ts`
- Budget-related config options
- `HardBudgetLimitError` (just added in Sprint 3)
- Budget tools

**Note:** Users with auth tokens (Anthropic Max, etc.) don't have dollar costs anyway - just usage quotas which OpenCode tracks.

---

## Previous Test Results (Archived)

See [COMPLETED.md](COMPLETED.md) for Test 1 results and Sprint 1-3 completion.

---

## References

- [COMPLETED.md](COMPLETED.md) - Completed phases archive
- [spec.md](spec.md) - Full specification
- [CLAUDE.md](../CLAUDE.md) - Project overview
