# Sprint 13: Agent Intelligence — Implementation Prompt

> For AI coding agent. Estimated: 6 features, mix S/M effort.
> Run `npm run test:run && npx tsc --noEmit` after each feature.

---

## Role

You are implementing Sprint 13 (Agent Intelligence) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, architecture, dispatchCompute pattern)
- `AGENTS.md` (code standards, common workflows)

---

## Pre-Implementation: Competitor Research Phase

**CRITICAL**: Before implementing each feature, you MUST read the relevant competitor reference code and extract best patterns. The reference code is in `docs/reference-code/`. The audit summaries are in `docs/research/audits/`.

For EACH feature below, follow this workflow:

1. **Read** the listed competitor reference files
2. **Extract** the key algorithm, data structures, thresholds, and edge cases
3. **Adapt** the pattern to AVA's TypeScript + event-driven + middleware architecture
4. **Implement** using AVA conventions (strict TS, no `any`, explicit return types, <300 lines/file)
5. **Test** with unit tests
6. **Verify** by running `npm run test:run && npx tsc --noEmit`

---

## Feature 1: Steering Interrupts — Skip Pending Tools

### Competitor Research
Read these files and extract the steering interrupt pattern:
- `docs/reference-code/pi-mono/packages/agent/src/agent.ts` — Look for `steeringQueue`, `followUpQueue`, `steer()`, `dequeueSteeringMessages()`, `steeringMode` ("all" vs "one-at-a-time"), how pending tool calls are skipped
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/` — Look for how user interrupts are surfaced and how the agent loop reacts
- `docs/research/audits/pi-mono-audit.md` — "Worth Stealing" section on steering

**Key patterns to extract from Pi Mono:**
- Dual-queue model (steering vs follow-up) with mode-based delivery
- How pending tool calls are skipped when a steering message arrives
- How `one-at-a-time` mode delivers messages gradually
- Session cache coherence across steers

### Current AVA State
AVA already has `steer()` and `queueFollowUp()` in `packages/core-v2/src/agent/loop.ts` (lines 269-289). The steering controller aborts the current turn. But **pending tool calls in a batch are NOT skipped** — if the LLM returned 5 tool calls, all 5 execute even after a steer.

### What to Build
Enhance steering to skip remaining pending tool calls when a steer message arrives mid-batch.

**File:** MODIFY `packages/core-v2/src/agent/loop.ts`

**Changes:**
1. In the tool execution section (lines 790-870), check `steeringQueue.length > 0` before each tool call
2. If steering message queued → skip remaining tools, inject message: `"[Steering interrupt: {N} pending tool calls skipped. User message follows.]"`
3. Add `skipPendingTools()` method that drains pending tool calls and returns their names
4. Emit `agent:tools-skipped` event with `{ skippedTools: string[], reason: 'steering' }`

**Also modify:** `packages/core-v2/src/agent/types.ts` — add `agent:tools-skipped` event type

```typescript
// In tool batch execution loop:
for (const toolCall of toolCalls) {
  if (this.steeringQueue.length > 0) {
    const skipped = toolCalls.slice(currentIndex)
    this.emit('agent:tools-skipped', {
      agentId: this.id,
      skippedTools: skipped.map(t => t.name),
      reason: 'steering'
    })
    // Add skip notice to history
    history.push({
      role: 'tool_result',
      content: `[Skipped: ${skipped.length} pending tool calls due to steering interrupt]`
    })
    break
  }
  // ... execute tool
}
```

### Tests
- `packages/core-v2/src/agent/steering.test.ts` (new)
- Test: steer during single tool call → completes current, skips rest
- Test: steer with empty queue → no skip
- Test: skipped tools are reported in event
- Test: steering message appears in next turn's context

---

## Feature 2: Reviewer Agent Loop

### Competitor Research
Read these files and extract the reviewer/validator pattern:
- `docs/reference-code/swe-agent/sweagent/agent/` — Look for reviewer agent, second-LLM validation, quality gates
- `docs/reference-code/swe-agent/sweagent/agent/action_sampler.py` — Look for `BinaryTrajectoryComparison`, tournament-style evaluation, expert judgment prompts
- `docs/research/audits/swe-agent-audit.md` — "Worth Stealing" section on reviewer loop

**Key patterns to extract from SWE-agent:**
- How a second LLM validates the primary agent's output
- The binary comparison prompt format (thought1+action1 vs thought2+action2)
- How validation failures trigger retry with feedback
- Tournament-style selection (pairwise comparison, O(n))

### Current AVA State
AVA has a `validator` extension (`packages/extensions/validator/src/index.ts`) that runs on `agent:completing` event. It calls `syntaxValidator`, `typescriptValidator`, `lintValidator`, `testValidator`. But there's no **LLM-based review** — it's all static analysis.

### What to Build
Add an LLM-based reviewer that validates agent output before returning to the user.

**File:** `packages/extensions/validator/src/reviewer.ts` (new)

```typescript
export interface ReviewResult {
  approved: boolean
  feedback: string
  confidence: number  // 0-1
  issues: string[]
}

export async function reviewAgentOutput(
  goal: string,
  output: string,
  filesChanged: string[],
  diffs: string[],
  provider: LLMProvider,
  model: string,
  signal?: AbortSignal
): Promise<ReviewResult>
```

**Implementation:**
- Hook into existing `agent:completing` event in validator extension
- Use a cheaper/faster model for review (configurable, default: same provider, smaller model)
- Review prompt includes: original goal, agent's output summary, file diffs
- Reviewer checks: goal satisfaction, code correctness, missing edge cases, test coverage
- If `approved: false` → inject feedback into agent context, allow 1 retry
- Max 1 review cycle (no infinite loops)
- Config: `validator.reviewEnabled: boolean` (default: false, opt-in)

**Review Prompt Template:**
```
You are a code reviewer. The agent was given this goal:
{goal}

It produced these changes:
{diffs}

And this summary:
{output}

Review for:
1. Does the output satisfy the goal?
2. Are there obvious bugs or missing edge cases?
3. Are there test gaps?

Respond with JSON: { "approved": bool, "feedback": "...", "issues": ["..."] }
```

**Integration:** Register in `packages/extensions/validator/src/index.ts` alongside existing validators. Runs after static validators pass (no point reviewing if syntax is broken).

### Tests
- `packages/extensions/validator/src/reviewer.test.ts` (new)
- Test: approved output passes through unchanged
- Test: rejected output triggers retry with feedback
- Test: max 1 retry cycle (no infinite loop)
- Test: reviewer disabled by default
- Test: reviewer uses configured model (not hardcoded)

---

## Feature 3: Model Fallback on Context Overflow

### Competitor Research
Read these files and extract the model fallback pattern:
- `docs/reference-code/plandex/app/server/model/` — Look for model fallback chains, context overflow handling, switching to larger-context models
- `docs/research/audits/plandex-audit.md` — "Worth Stealing" section on model fallback
- Also read AVA's existing fallback code: `packages/extensions/models/src/availability.ts` — existing `getFallbackModel()` and health tracking

**Key patterns to extract from Plandex:**
- How context overflow is detected (token count vs model limit)
- How the system switches to a larger-context model instead of truncating
- Fallback chain ordering (by context window size, not just availability)
- How the switch is transparent to the agent

### Current AVA State
AVA has model health tracking and fallback chains in `availability.ts` (lines 28-103). Fallback chains exist per provider tier. But fallback only triggers on **model unavailability** (errors, degraded status) — NOT on context overflow. When context overflows, AVA compacts/truncates instead of switching models.

### What to Build
Add context-overflow-triggered model switching: when usage > 90% of current model's context window, switch to a model with a larger context window instead of aggressive truncation.

**File:** MODIFY `packages/extensions/models/src/availability.ts`

**Add:**
```typescript
export interface ContextFallbackChain {
  provider: string
  model: string
  contextWindow: number
}

/** Ordered by context window size (ascending) */
const CONTEXT_FALLBACK_CHAINS: Record<string, ContextFallbackChain[]> = {
  'anthropic': [
    { provider: 'anthropic', model: 'claude-haiku-4-5', contextWindow: 200_000 },
    { provider: 'anthropic', model: 'claude-sonnet-4-6', contextWindow: 200_000 },
    { provider: 'anthropic', model: 'claude-opus-4-6', contextWindow: 200_000 },
  ],
  'openrouter': [
    { provider: 'openrouter', model: 'anthropic/claude-sonnet-4-6', contextWindow: 200_000 },
    { provider: 'openrouter', model: 'google/gemini-2.5-pro', contextWindow: 1_000_000 },
  ],
}

export function getContextFallback(
  currentProvider: string,
  currentModel: string,
  requiredTokens: number
): ContextFallbackChain | null
```

**File:** MODIFY `packages/core-v2/src/agent/loop.ts`

**Changes in context compaction section (lines 349-385):**
1. Before compacting, check if a larger-context model is available
2. If `getContextFallback()` returns a model → switch provider/model for remaining turns
3. Emit `model:context-fallback` event: `{ from: { provider, model }, to: { provider, model }, reason: 'context_overflow' }`
4. Log: "Switching from {model} ({contextWindow} tokens) to {fallbackModel} ({contextWindow} tokens) due to context overflow"
5. Only fall back to compaction if no larger model available

**Priority:** Model switch > compaction > truncation

### Tests
- `packages/extensions/models/src/context-fallback.test.ts` (new)
- Test: overflow with larger model available → returns fallback
- Test: overflow with no larger model → returns null (triggers compaction)
- Test: already on largest model → returns null
- Test: fallback chain ordering is by context window

---

## Feature 4: Progressive Error Escalation

### Competitor Research
Read these files and extract the error escalation pattern:
- `docs/reference-code/cline/src/core/task/index.ts` — Look for `consecutiveMistakeCount`, escalation strategies, forced strategy switches, context-aware error messages
- `docs/reference-code/cline/src/core/task/tools/handlers/` — Look for error handling in tool handlers
- `docs/research/audits/cline-audit.md` — "Worth Stealing" section on progressive escalation

**Key patterns to extract from Cline:**
- How consecutive failure counter works (reset on success)
- The 3-tier escalation: retry → different strategy → context compress
- How forced strategy switches are applied
- Context-aware error messages that become more directive

### Current AVA State
AVA has error-recovery middleware (`packages/extensions/hooks/src/error-recovery-middleware.ts`) that retries with fuzzy variants (max 3 attempts). But it doesn't escalate — every failure gets the same treatment. There's no consecutive failure tracking across different tool calls.

### What to Build
Track consecutive failures and escalate error handling strategy as failures accumulate.

**File:** `packages/extensions/hooks/src/progressive-escalation.ts` (new)

```typescript
export interface EscalationLevel {
  level: number
  message: string
  forceStrategy?: string
  compressContext?: boolean
}

const ESCALATION_LEVELS: EscalationLevel[] = [
  {
    level: 1,
    message: 'The previous action failed: {error}. Please retry with a different approach.',
  },
  {
    level: 2,
    message: 'Second consecutive failure. The current approach is not working.\n{error}\nTry a fundamentally different strategy (e.g., write_file instead of edit, or break into smaller changes).',
    forceStrategy: 'write_file',
  },
  {
    level: 3,
    message: 'Multiple consecutive failures detected. This may indicate a deeper issue.\n{error}\nStop and re-read the target file before attempting further changes. Consider whether the file structure has changed.',
    compressContext: true,
  },
]

export function createProgressiveEscalationMiddleware(
  api: ExtensionAPI,
  logger: Logger
): ToolMiddleware
```

**Implementation:**
- Priority 6 (right after reliability at 5)
- Track `consecutiveFailures` per session (reset to 0 on any tool success)
- On failure: increment counter, look up escalation level
- Level 1: inject directive error message
- Level 2: inject message + suggest `write_file` strategy in metadata
- Level 3: inject message + trigger context compaction + emit `escalation:max-reached` event
- After 5 consecutive failures: emit `stuck:detected` with `scenario: 'error-escalation'`

**Integration:** Register in `packages/extensions/hooks/src/index.ts`.

### Tests
- `packages/extensions/hooks/src/progressive-escalation.test.ts` (new)
- Test: single failure → level 1 message
- Test: two consecutive failures → level 2 with strategy suggestion
- Test: three consecutive failures → level 3 with context compression
- Test: success resets counter to 0
- Test: 5 failures emits stuck:detected

---

## Feature 5: StuckDetector — 2 New Scenarios

### Competitor Research
Read these files and extract the stuck detection patterns:
- `docs/reference-code/openhands/openhands/controller/stuck.py` — Look for `StuckDetector.is_stuck()`, all 5 scenarios, `filtered_history`, how events are compared, `StuckAnalysis` dataclass
- `docs/research/audits/openhands-audit.md` — "Worth Stealing" section on stuck detection

**Key patterns to extract from OpenHands:**
- Scenario 4: Alternating action-observation pairs (A1,O1,A2,O2,A1,O1 — 3x repeat of 2-step pattern)
- Scenario 5: Context window error loop (10+ consecutive condensation events)
- How filtered_history excludes NullAction and user messages
- How events are compared ignoring process IDs
- The `loop_start_idx` for recovery positioning

### Current AVA State
AVA already has 5 scenarios in `packages/extensions/agent-modes/src/doom-loop.ts` (lines 380-584):
1. repeated-tool-call (3+ identical consecutive)
2. error-cycling (3+ identical errors)
3. empty-response-loop (3+ turns no tool calls)
4. monologue-loop (5+ turns pure text)
5. self-assessment (10th turn, no progress)

Missing from OpenHands: **alternating pairs** and **context window error loop**.

### What to Build
Add 2 new stuck scenarios to the existing detector.

**File:** MODIFY `packages/extensions/agent-modes/src/doom-loop.ts`

**Scenario 6: Alternating Action Pairs**
```typescript
// Detect: (toolA, toolB, toolA, toolB, toolA, toolB) — 3x repeat of a 2-step pattern
function detectAlternatingPairs(history: ToolCallRecord[]): StuckScenario | null {
  if (history.length < 6) return null
  const last6 = history.slice(-6)
  // Check if [0,1] == [2,3] == [4,5] by tool name + args hash
  const pair1 = hash(last6[0]) + '|' + hash(last6[1])
  const pair2 = hash(last6[2]) + '|' + hash(last6[3])
  const pair3 = hash(last6[4]) + '|' + hash(last6[5])
  if (pair1 === pair2 && pair2 === pair3) {
    return { scenario: 'alternating-pairs', severity: 'high', count: 3 }
  }
  return null
}
```

**Scenario 7: Context Window Error Loop**
```typescript
// Detect: 5+ consecutive compaction events with no productive tool calls between them
function detectContextWindowLoop(events: AgentEvent[]): StuckScenario | null {
  let consecutiveCompactions = 0
  for (const event of events.slice().reverse()) {
    if (event.type === 'context:compacting') {
      consecutiveCompactions++
    } else if (event.type === 'tool:finish' && event.success) {
      break  // Productive work happened
    }
  }
  if (consecutiveCompactions >= 5) {
    return { scenario: 'context-window-loop', severity: 'high', count: consecutiveCompactions }
  }
  return null
}
```

**Integration:** Add both checks to the existing `turn:end` event handler in `doom-loop.ts`. Emit `stuck:detected` with the new scenario names.

### Tests
- Add to existing `packages/extensions/agent-modes/src/doom-loop.test.ts`
- Test: alternating pairs (A,B,A,B,A,B) → detected
- Test: non-alternating mixed calls → not detected
- Test: 5 consecutive compactions → context-window-loop detected
- Test: compactions with productive work between → not detected

---

## Feature 6: Architect Mode (2-Model Workflow)

### Competitor Research
Read these files and extract the architect mode pattern:
- `docs/reference-code/aider/aider/coders/` — Look for architect coder, 2-model workflow, planner + executor split
- `docs/reference-code/aider/aider/coders/architect_coder.py` — Look for how the expensive model plans and the cheap model executes
- `docs/research/audits/aider-audit.md` — "Worth Stealing" section on architect mode

**Key patterns to extract from Aider:**
- How the planner model generates a structured plan (no tool calls)
- How the plan is passed to the executor model
- How the executor model is restricted to implementation-only tools
- How conflicts between plan and execution are resolved

### Current AVA State
AVA has an agent mode system (`packages/extensions/agent-modes/src/`) with `plan-mode`, `minimal-mode`, and `best-of-n-mode`. The `AgentMode` interface supports `filterTools()` and `systemPrompt()`. There's no 2-model split workflow.

### What to Build
An architect mode where an expensive model plans, then a cheaper model executes the plan.

**File:** `packages/extensions/agent-modes/src/architect-mode.ts` (new)

```typescript
export interface ArchitectConfig {
  plannerProvider: string   // e.g., 'anthropic'
  plannerModel: string      // e.g., 'claude-opus-4-6'
  executorProvider: string  // e.g., 'anthropic'
  executorModel: string     // e.g., 'claude-sonnet-4-6'
  maxPlanSteps: number      // default: 10
}

export function createArchitectMode(config: ArchitectConfig): AgentMode
```

**Implementation:**

**Phase 1 — Planning (expensive model):**
- System prompt: "You are an architect. Analyze the task and produce a step-by-step implementation plan. Do NOT write code. Output a numbered list of changes with file paths and descriptions."
- Tools: read-only only (read_file, glob, grep, ls, websearch)
- Output: structured plan (parsed from LLM response)

**Phase 2 — Execution (cheap model):**
- System prompt: "You are an executor. Follow this plan exactly: {plan}. Implement each step. Do not deviate from the plan."
- Tools: all tools (edit, write, bash, etc.)
- Context: plan + relevant file contents
- Each plan step becomes a delegation to the executor

**Mode registration:**
```typescript
api.registerAgentMode({
  name: 'architect',
  description: 'Two-model workflow: expensive model plans, cheaper model executes',
  systemPrompt: (base) => base + architectPromptAddition,
  filterTools: (tools) => readOnlyTools,  // Phase 1 only
  onEnter: () => { /* switch to planner model */ },
  onExit: () => { /* restore original model */ },
})
```

**Slash command:** `/architect` to enter architect mode, `/architect off` to exit.

**Integration:** Register in `packages/extensions/agent-modes/src/index.ts`. Add slash command in `packages/extensions/slash-commands/src/index.ts`.

### Tests
- `packages/extensions/agent-modes/src/architect-mode.test.ts` (new)
- Test: planning phase only has read-only tools
- Test: execution phase has all tools
- Test: plan is passed to executor in system prompt
- Test: mode can be entered and exited via slash command
- Test: config allows different provider/model per phase

---

## Post-Implementation Verification

After ALL 6 features are implemented:

1. Run full test suite: `npm run test:run`
2. Run type check: `npx tsc --noEmit`
3. Run linter: `npm run lint`
4. Run format check: `npm run format:check`
5. Verify no files exceed 300 lines
6. Commit with: `git commit -m "feat(sprint-13): agent intelligence — steering interrupts, reviewer loop, model fallback, progressive escalation, stuck detection, architect mode"`

---

## File Change Summary

| Action | File |
|--------|------|
| MODIFY | `packages/core-v2/src/agent/loop.ts` (steering skip + context fallback) |
| MODIFY | `packages/core-v2/src/agent/types.ts` (new event types) |
| CREATE | `packages/core-v2/src/agent/steering.test.ts` |
| CREATE | `packages/extensions/validator/src/reviewer.ts` |
| CREATE | `packages/extensions/validator/src/reviewer.test.ts` |
| MODIFY | `packages/extensions/validator/src/index.ts` (register reviewer) |
| MODIFY | `packages/extensions/models/src/availability.ts` (context fallback chains) |
| CREATE | `packages/extensions/models/src/context-fallback.test.ts` |
| CREATE | `packages/extensions/hooks/src/progressive-escalation.ts` |
| CREATE | `packages/extensions/hooks/src/progressive-escalation.test.ts` |
| MODIFY | `packages/extensions/hooks/src/index.ts` (register escalation middleware) |
| MODIFY | `packages/extensions/agent-modes/src/doom-loop.ts` (2 new scenarios) |
| MODIFY | `packages/extensions/agent-modes/src/doom-loop.test.ts` (new scenario tests) |
| CREATE | `packages/extensions/agent-modes/src/architect-mode.ts` |
| CREATE | `packages/extensions/agent-modes/src/architect-mode.test.ts` |
| MODIFY | `packages/extensions/agent-modes/src/index.ts` (register architect mode) |
| MODIFY | `packages/extensions/slash-commands/src/index.ts` (add /architect command) |
