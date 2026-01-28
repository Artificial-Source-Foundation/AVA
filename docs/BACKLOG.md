# Delta9 Backlog

> Active development and bug tracking

---

## Current Status

| Sprint | Status |
|--------|--------|
| Sprint 1-9 | ✅ Complete (see COMPLETED.md) |
| Sprint 10 | ✅ Complete |
| Test 5 | ✅ Complete |
| Test 6 | 🟡 Ready (Sprint 10 bugs fixed) |

---

## Sprint 11: Critical Bug Fixes 🟡 IN PROGRESS

> Bugs discovered during Test 6 validation - some fixed, others pending

### BUG-39: Council Config Mismatch ✅ RESOLVED

**Status**: Fixed in Sprint 10 implementation.

**Verification**:
- `src/types/config.ts`: 6 members in DEFAULT_CONFIG.council.members
- `src/agents/council/`: 6 oracle-*.ts files present
- Advisors: CIPHER, VECTOR, APEX, AEGIS, RAZOR, ORACLE
- PRISM removed (replaced by ORACLE)

### BUG-40: Council Advisors Return Empty Recommendations 🔴 CRITICAL

**Symptom**: All advisors return `recommendation: ""` (empty string) with `confidence: 0.5`.

**Evidence**:
```json
{"oracle":"Cipher","confidence":0.5,"recommendation":"","hasCaveats":true}
{"oracle":"Vector","confidence":0.5,"recommendation":"","hasCaveats":true}
{"oracle":"Prism","confidence":0.5,"recommendation":"","hasCaveats":true}
```

**Expected**: Actual recommendations with reasoning.

**Likely Cause**: Council prompts not being sent correctly, or response parsing failing silently.

### BUG-41: DeepSeek Model Not Found ✅ RESOLVED

**Status**: Fixed in Sprint 10 implementation.

**Verification**:
- `src/types/config.ts`: VECTOR uses `openrouter/deepseek/deepseek-r1`
- Temperature: 0.6 (official R1 recommendation)
- Thinking: `triggerThinking: true` (R1 thinking mode enabled)

### BUG-42: Background Task False "Stale" Error 🟠 HIGH

**Symptom**: Background task completes successfully but returns `error: "Task stale (no activity for 3 minutes)"`.

**Evidence**:
```json
{
  "status": "completed",
  "output": { "success": true, "result": "..." },
  "error": "Task stale (no activity for 3 minutes)"
}
```

**Expected**: No error when task completes successfully.

**Root Cause Found**: In `src/lib/background-manager.ts`:
- `pruneStaleAndExpiredTasks()` (line 462-480) sets `task.error` when stale
- `extractTaskResult()` (line 1105) sets `task.status = 'completed'` but does NOT clear `task.error`
- If staleness is detected before completion, error persists even after successful completion

**Fix**: In `extractTaskResult()`, add `task.error = undefined` before setting status to completed (around line 1105).

### BUG-43: Test 6 Scenario Outdated ✅ RESOLVED

**Status**: Sprint 10 implementation complete with 6 Strategic Advisors.

**Note**: Test 6 scenarios should now verify 6 advisors respond (CIPHER, VECTOR, APEX, AEGIS, RAZOR, ORACLE).

### BUG-44: Intel Agent Returns Empty Output 🟠 HIGH

**Symptom**: Intel agent completes but returns `"(No text output)"` - no actual research.

**Evidence**:
```json
{
  "status": "completed",
  "output": {"result": "(No text output)"},
  "error": "Task stale (no activity for 3 minutes)"
}
```

**Expected**: Intel should return research findings.

**Root Cause Found**: In `src/lib/background-manager.ts` line 1086:
```typescript
const textParts = lastMessage.parts?.filter((p) => p.type === 'text' || p.type === 'reasoning') ?? []
```

Only `text` and `reasoning` part types are captured. If agent returns output in different part type (e.g., `tool_result`, `code`, etc.), it's ignored.

**Affected Agents**: Intel, Scribe, UI-Ops (all return empty)
**Working Agents**: Scout, Patcher, Strategist, QA, Operator (return content)

**Hypothesis**: Working agents produce `text` parts, broken agents might:
1. Use tool calls instead of text responses
2. Return output in a different part type
3. Have model/prompt issues causing no text generation

**Fix**: Debug by logging all part types from completed sessions, then expand the filter.

### Test 6 Agent Validation Results

| Agent | Status | Notes |
|-------|--------|-------|
| Scout | ✅ Works | Completes but shows false "stale" error (BUG-42) |
| Intel | ⚠️ Partial | Completes but returns empty output (BUG-44) |
| Patcher | ✅ Works | Add/remove operations successful |
| Strategist | ✅ Works | Returns good tactical advice |
| Council | ❌ Broken | Empty recommendations, wrong advisors, model errors (BUG-39,40,41) |
| QA | ✅ Works | Excellent - returns full test suite with edge cases |
| Scribe | ⚠️ Partial | Completes but returns empty output like Intel (BUG-45) |
| UI-Ops | ⚠️ Partial | Completes but returns empty output (BUG-50) |
| Operator | ✅ Works | Full report with acceptance criteria - excellent! |

### BUG-45: Scribe Agent Fails 🟠 HIGH

**Symptom**: Scribe agent fails with stale error.

**Likely Cause**: Model misconfiguration (Scribe uses `z-ai/glm-4.7` per Sprint 10).

### BUG-46: Errors Logged to Console Instead of Handled 🟡 MEDIUM

**Symptom**: `ProviderModelNotFoundError` for `openrouter/deepseek/deepseek-v3.2` spams console during council operations.

**Expected**: Errors should be caught and returned cleanly in tool output, not logged to console.

**Fix**: Add try/catch around council model calls, return structured error in response.

### BUG-47: Background Task Queue Stuck 🔴 CRITICAL

**Symptom**: Tasks remain in "pending" status even when pool shows 0% utilization and 0 active tasks.

**Evidence**:
```json
{
  "pool": {"active": 0, "pending": 0, "maxConcurrency": 3, "utilization": "0%"},
  // But these tasks are still pending:
  {"id": "bg_tNa3z7_N", "status": "⏳ pending", "agent": "uiOps"},
  {"id": "bg_Qe8j-nlW", "status": "⏳ pending", "agent": "operator"}
}
```

**Expected**: Pending tasks should be picked up when pool has capacity.

**Likely Cause**: Queue processor not running, or tasks not being moved from pending to running.

### BUG-48: Scout Output Truncated 🟠 HIGH

**Symptom**: Scout completes but output is truncated mid-sentence.

**Evidence**:
```json
{"result": "Now I understand the issue! Let me create a comprehensive report of my findings:"}
```

**Expected**: Full investigation report.

**Likely Cause**: Output capture stops prematurely, or agent session ends before full response.

### BUG-49: EditBuffer Destroyed Error 🟡 MEDIUM

**Symptom**: Console shows `Error: EditBuffer is destroyed` from OpenTUI core during subagent sessions.

**Evidence** (from screenshot):
```
Error: EditBuffer is destroyed
  at guard (node_modules/.bun/@opentui+core/index.js:332:17)
  at getTexts (node_modules/.bun/@opentui+core/index.js:371:10)
  at focus (node_modules/.bun/@opentui+core/index.js:6744:32)
```

**Context**: Appears during/after operator subagent completes task.

**Likely Cause**: OpenTUI buffer lifecycle issue - buffer destroyed before focus attempt.

### BUG-50: UI-Ops Returns Empty Output 🟠 HIGH

**Symptom**: UI-Ops agent completes but returns `"(No text output)"` like Intel/Scribe.

**Pattern**: Intel, Scribe, UI-Ops all return empty. Scout, Patcher, Strategist, QA, Operator return content.

**Hypothesis**: Agents with certain model configurations or prompt structures don't capture output correctly.

---

## Sprint 10: Model Configuration Optimization 🟡 IN PROGRESS

> Optimizing model assignments based on rate limits, capabilities, and provider distribution

### Progress Summary

| Category | Status | Notes |
|----------|--------|-------|
| Commander | ✅ Done | Opus 4.5 |
| Council (6 advisors) | ✅ Done | CIPHER, VECTOR, APEX, AEGIS, RAZOR, ORACLE |
| Scout, Validator | ✅ Done | GLM-4.7, Haiku 4.5 |
| Operators (3-tier) | ✅ Done | Private (Sonnet), Sergeant (Codex), Delta Force (Opus) |
| Support Agents | ✅ Done | 6/6 complete (Optics removed as redundant) |

**Next Step:** Verify implementation and mark complete

### Decisions Made

| Agent | Model | Temperature | Reasoning |
|-------|-------|-------------|-----------|
| **Commander** | `claude-opus-4-5` | 0.7 | Best reasoning, most important agent |
| **Scout (RECON)** | `z-ai/glm-4.7` | - | Fast, good ZAI Max limits |
| **Validator** | `claude-haiku-4-5` | - | Fast, separate rate limit bucket |

#### Council (Strategic Advisors) - Heterogeneous Models

| Advisor | Model | Provider | Temp | Reasoning |
|---------|-------|----------|------|-----------|
| **CIPHER** | `gpt-5.2-codex` (xhigh reasoning) | OpenAI | 0.2 | Architecture needs deep thinking, low temp for precision |
| **VECTOR** | `deepseek-r1` | OpenRouter | 0.6 | SOTA reasoning (97.3% MATH), R1 needs 0.6 temp officially |
| **APEX** | `claude-opus-4-5` | Anthropic | 0.3 | Best for performance (beats humans), precise analysis |
| **AEGIS** | `claude-opus-4-5` | Anthropic | 0.3 | Security critical - 4.7% injection rate (best), can't compromise |
| **RAZOR** | `gemini-3-pro-preview` | Google | 0.4 | Naturally KISS (2.1 CCN), lowest verbosity, avoids over-engineering |
| **ORACLE** | `moonshot/kimi-k2.5` | Moonshot | 0.7 | Agent Swarm explores alternatives in parallel, creative, no vendor lock-in |

#### Research Findings Applied

- **Heterogeneous > Homogeneous**: X-MAS research shows 47% boost with diverse models (COUNCIL ONLY - support agents can repeat)
- **Temperature by role**: Low (0.2-0.3) for analytical, 0.6 for R1, 0.4 for balanced
- **DeepSeek R1 quirks**: No system prompts, temp 0.6, top-p 0.95, `<think>` trigger
- **Claude for security**: 4.7% injection ASR vs Gemini's 12.5% - Claude wins
- **Gemini for KISS**: Lowest cyclomatic complexity, Opus over-engineers (2x code)

### Architecture Decisions

#### ARCH-1: Heterogeneous Council (Research-Backed)

Each council member MUST use a different model provider to leverage collective intelligence:
- X-MAS research shows **47% performance boost** with heterogeneous models
- Different models = different strengths + different blind spots
- Avoids "single LLM blind spot" problem

#### ARCH-2: Restructure Council (6 Strategic Advisors)

Replace current 4-oracle structure with 6 strategic advisors. Remove Prism (UI/UX) as it's redundant with UI Ops support agent.

**New Council Composition:**

| Codename | Focus | Perspective |
|----------|-------|-------------|
| **CIPHER** | Architecture | System design, patterns, structure |
| **VECTOR** | Logic & Analysis | Reasoning, problem decomposition |
| **APEX** | Performance | Optimization, scalability, efficiency |
| **AEGIS** | Security & Risk | Threats, vulnerabilities, edge cases |
| **RAZOR** | Simplification | KISS, maintainability, avoid over-engineering |
| **ORACLE** | Future & Alternatives | Innovation, future-proofing, different approaches |

**Key Changes:**
- Removed: Prism (UI/UX) - redundant with FACADE support agent
- Added: AEGIS (Security + Risk combined)
- Added: RAZOR (Simplification + Maintainability combined)
- Added: ORACLE (Future-proofing + Innovation combined)

#### ARCH-3: Commander-Driven Council Invocation

Advisors are **on-call** rather than mandatory. Commander decides which to invoke:

```
Simple task → Skip council or minimal (RAZOR only)
Auth task → CIPHER + AEGIS + VECTOR
Critical refactor → ALL 6 (full council)
Performance issue → APEX + VECTOR + RAZOR
```

**Implementation:**
- Pool of 6 advisors available
- Commander analyzes task and picks relevant advisors
- Council modes map to presets:
  - `none`: 0 advisors
  - `quick`: 2 advisors (Commander picks)
  - `standard`: 3-4 advisors (Commander picks)
  - `xhigh`: All 6 (full council)

#### ARCH-4: Rename "Oracles" to Military Term

Change terminology from "Oracles" to military-themed term:
- **Strategic Advisors** (recommended)
- Intelligence Officers
- Joint Chiefs
- War Council
- Staff Officers

#### ARCH-5: Reasoning Mode Configuration

Research and implement configurable reasoning/thinking modes for models:
- GPT-5.2: `high`, `xhigh` reasoning modes
- Claude: Extended thinking with budget tokens
- Gemini: Deep Think mode
- DeepSeek R1: Reasoning traces

**Goal**: Allow per-advisor configuration of reasoning depth:
```typescript
{
  model: 'openai/gpt-5.2-codex',
  reasoningMode: 'xhigh',  // or 'high', 'standard'
  thinkingBudget: 32000,   // for Claude
}
```

#### ARCH-6: DeepSeek R1 Special Handling

DeepSeek R1 has unique requirements that differ from other models:
- **No system prompts** - Put all instructions in user prompt
- **Temperature 0.6** - Official recommendation (0.5-0.7 range)
- **Top-p 0.95** - For balanced diversity
- **Thinking trigger** - Start response with `<think>\n` to force reasoning

**Implementation**: Create R1-specific adapter that:
1. Moves system prompt content to user prompt
2. Enforces temperature 0.6
3. Optionally triggers thinking mode

### Operators: 3-Tier Marine System ✅

| Tier | Codename | Model | Provider | Temp | Triggers |
|------|----------|-------|----------|------|----------|
| **Tier 1** | Marine Private | `claude-sonnet-4-5` | Anthropic | 0.3 | Default, simple, single-file |
| **Tier 2** | Marine Sergeant | `gpt-5.2-codex` (high) | OpenAI | 0.3 | Moderate, multi-file, `update`, `enhance` |
| **Tier 3** | Delta Force | `claude-opus-4-5` | Anthropic | 0.2 | Critical, `refactor`, `migrate`, `rewrite`, `architecture` |

**Rationale:**
- Private (Sonnet): Separate rate limit from Opus, fast, capable for most work
- Sergeant (Codex): Spreads load to OpenAI, high reasoning for tougher tasks
- Delta Force (Opus): Reserved for truly hard problems, best of the best

### Support Agents Configuration

| Agent | Codename | Model | Provider | Status | Reasoning |
|-------|----------|-------|----------|--------|-----------|
| **Intel** | SIGINT | `gemini-3-pro` | Google | ✅ Done | #1 on Search Arena, 72% SimpleQA (vs GPT 38%), best grounding |
| **Strategist** | TACCOM | `gpt-5.2-codex` | OpenAI | ✅ Done | Strong reasoning for mid-execution decisions |
| **UI Ops** | FACADE | `gemini-3-pro` | Google | ✅ Done | #1 WebDev Arena, best visual quality for cost |
| **Scribe** | - | `z-ai/glm-4.7` | ZAI | ✅ Done | Good ZAI Max limits, spreads provider load |
| ~~Optics~~ | ~~SPECTRE~~ | - | - | ❌ Removed | Redundant - most models are multimodal now |
| **QA** | SENTINEL | `claude-sonnet-4-5` | Anthropic | ✅ Done | 77-82% SWE-Bench, highest first-attempt completion, reliable |
| **Patcher** | SURGEON | `claude-haiku-4-5` | Anthropic | ✅ Done | Fast, cheap, perfect for small fixes (max 50 lines) |

### Provider Distribution (Final)

| Provider | Agents | Rate Limit Strategy |
|----------|--------|---------------------|
| **Anthropic** | Commander (Opus), APEX (Opus), AEGIS (Opus), Validator (Haiku) | 3x Opus, 1x Haiku |
| **OpenAI** | CIPHER (Codex xhigh) | 1x Codex |
| **Google** | RAZOR (Gemini 3 Pro) | 1x Pro |
| **ZAI** | Scout (GLM-4.7) | 1x GLM |
| **OpenRouter** | VECTOR (DeepSeek-R1) | 1x R1 |
| **Moonshot** | ORACLE (Kimi K2.5) | 1x K2.5 |

---

## Test 6: Full Workflow Validation

> Validate the complete config-driven model system with a real-world task

### Pre-Test Checklist

- [x] All oracle agents use factory functions with `cwd` parameter
- [x] All models come from `DEFAULT_CONFIG` (no hardcoded strings in agent files)
- [x] Fallbacks reference `DEFAULT_CONFIG`, not hardcoded values
- [x] `src/index.ts` uses `createCouncilAgents(cwd)` for council registration
- [x] Typecheck passes
- [x] Build succeeds

### Test Scenarios

1. **Mission Creation** - Create a mission with multiple objectives
2. **Council Consultation** - Verify all 4 oracles respond with correct models
3. **Task Delegation** - Delegate to support agents (RECON, SIGINT, etc.)
4. **Background Execution** - Spawn background tasks and retrieve outputs
5. **Dependency Resolution** - Complete task and verify blocked tasks unblock
6. **Mission Completion** - Verify status transitions (planning → in_progress → completed)

### What to Watch For

- Model IDs in logs should match `DEFAULT_CONFIG` values
- Council responses should come from configured models
- Background tasks should spawn with correct agent prompts
- No "agent not found" or model resolution errors

---

## Documentation Needed

### DOC-1: Model Configuration System [LOW]

Document in `docs/delta9/`:
- How models are configured
- Fallback chain behavior
- How to override defaults via delta9.json

### DOC-2: Scout vs Intel Usage Patterns [LOW]

Document when to use:
- **Scout (RECON)**: Fast codebase search, file discovery
- **Intel (SIGINT)**: External research, docs, web search

---

## Previous Test Summary

**Sprint 9 (Model Configuration Audit):** ✅ Complete
- All oracle agents converted to factory functions
- Zero hardcoded model strings in agent files
- All models come from `DEFAULT_CONFIG`
- Verified pattern matches OMO/swarm reference implementations

**Test 5 (Performance Optimization Workflow):** ✅ Complete
- All 8 bugs fixed (BUG-31 through BUG-38)
- Model ID parsing for OpenRouter 3-segment format
- dispatch_task now spawns agents
- Auto-dependency resolution on task completion
- Mission status state machine (planning → in_progress → completed)

**Test 4 (Unfollow Module Enhancement):** ✅ Complete
- All 7 bugs fixed (BUG-24 through BUG-30, except BUG-26 external)
- Task dependency resolution working
- Background output persistence added
- Mission sync improved

**Current Stats:**
- Tests: 1266 passing
- Tools: 68+
- Agents: 19

---

## Future Enhancements

### HIGH Priority

#### ENH-23: 3-Tier Marine System (Operators) ✅ DONE

Implemented in Sprint 10. See `src/tools/delegation.ts`:
- `operator_tier1` / `marine_private` - Simple tasks (Sonnet)
- `operator_tier2` / `marine_sergeant` - Moderate tasks (Codex)
- `operator_tier3` / `delta_force` - Critical tasks (Opus)

#### ENH-24: Commander-Driven Complexity Routing ✅ DONE

Implemented in `src/lib/models.ts`:
- Complexity-based tier selection
- Commander can explicitly choose tier via `delegate_task`

#### ENH-25: Agent Sub-Agent Invocation ✅ DONE

Implemented via:
- `spawn_subagent` tool for agents to spawn other agents
- Config `invokeThreshold` in `src/types/config.ts`
- Compliance hooks push Commander to delegate to scout/intel

#### ENH-4: Better Error Messages 🟠

Improve error messages with actionable context:
- "JSON Parse error: Unexpected EOF" → "Gemini returned incomplete response. Consider using operator instead."
- "Agent not found" → "Agent 'ui_ops' not registered. Available: commander, operator, validator"

---

### MEDIUM Priority

#### ENH-5: Unified run_task Tool 🔥

Single tool that replaces dispatch_task + delegate_task confusion:

```typescript
run_task({
  taskId: 'task_xyz',  // Optional - syncs with mission if provided
  prompt: '...',
  agent: 'auto',       // Auto-routes to best agent
  background: true,    // Auto-tracks in background
  wait: true           // Block until complete (optional)
})
```

**Note**: BUG-34 fixed - dispatch_task now spawns agents. Consider consolidating tools.

#### ENH-6: execute_objective Tool

Run all tasks in an objective automatically with dependency handling.

#### ENH-7: Mission Progress Sync Command

Tool `mission_sync` that scans git changes and updates task statuses.

#### ENH-22: mission_complete Tool

Explicit tool to mark mission done and trigger cleanup/summary:
- Transition status to `completed`
- Generate mission summary
- Archive to history
- Clean up background tasks

---

### LOW Priority

| ID | Request |
|----|---------|
| ENH-8 | Live Mission Dashboard TUI |
| ENH-9 | Agent Performance Metrics |
| ENH-10 | Background Task Health Monitoring |
| ENH-11 | Task Replay/Debug |
| ENH-12 | Dry Run Mode |
| ENH-13 | Resumable Missions |
| ENH-14 | Learned Patterns |
| ENH-15 | Smart Checkpointing |
| ENH-16 | Squadron Templates |
| ENH-17 | Human Checkpoints |

---

### ARCHITECTURE

#### ENH-18: Event-Driven Architecture

Replace polling with event bus:
- Tasks emit: started, progress, completed, failed
- Commander subscribes and reacts
- UI can also subscribe for live updates

---

## External Issues

### BUG-11: Background Task Visibility (OpenCode)

CTRL+X navigation doesn't show plugin-created sessions. Filed as OpenCode platform issue.

### BUG-26: Extract Tool (OpenCode)

`extract` tool is an OpenCode platform internal tool. Workaround: use `discard` instead.

---

## References

- [COMPLETED.md](COMPLETED.md) - Completed work archive
- [spec.md](spec.md) - Full specification
- [CLAUDE.md](../CLAUDE.md) - Project overview
