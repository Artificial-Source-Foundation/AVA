# Sprint 17: Praxis v2 — Multi-Agent Hierarchy

> For AI coding agent. Estimated: 14 items, mix M/L effort.
> Run `npm run test:run && npx tsc --noEmit` after each feature.

---

## Role

You are implementing Sprint 17 (Praxis v2) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, architecture, dispatchCompute pattern)
- `AGENTS.md` (code standards, common workflows)
- `docs/planning/praxis-v2-design.md` (FULL design spec — read every section)

**IMPORTANT**: This sprint replaces the current flat Commander/Lead/Worker model with a 4-tier Director/Tech Lead/Engineer/Reviewer hierarchy. Read the design doc carefully before implementing.

---

## Pre-Implementation: Understand Current System

Before writing any code, read the existing implementation:
- `packages/extensions/commander/src/` — ALL files (current orchestration)
- `packages/extensions/commander/src/workers.ts` — Current agent definitions
- `packages/extensions/commander/src/delegate.ts` — Current delegation tools
- `packages/extensions/commander/src/orchestrator.ts` — Current orchestration logic
- `packages/extensions/commander/src/router.ts` — Current task routing
- `packages/extensions/commander/src/types.ts` — Current type definitions
- `packages/extensions/agent-modes/src/index.ts` — Mode registration
- `packages/extensions/agent-modes/src/selector.ts` — Mode auto-selection
- `packages/core-v2/src/agent/loop.ts` — Agent execution loop
- `packages/core-v2/src/agent/types.ts` — Agent types

---

## Feature 1: Rename Hierarchy

### What to Build
Rename Commander → Director, Lead → Tech Lead, Worker → Engineer throughout the codebase.

**Files to modify:**
- `packages/extensions/commander/src/types.ts` — Update type names and enums
- `packages/extensions/commander/src/workers.ts` — Update agent definitions
- `packages/extensions/commander/src/orchestrator.ts` — Update references
- `packages/extensions/commander/src/router.ts` — Update routing labels
- `packages/extensions/commander/src/delegate.ts` — Update delegation labels
- `packages/extensions/commander/src/agent-definition.ts` — Update definitions
- All test files in `packages/extensions/commander/src/` — Update assertions

**Implementation:**
- Update `AgentRole` enum/type: `commander → director`, `lead → tech-lead`, `worker → engineer`
- Update `BUILTIN_AGENTS` definitions with new names and descriptions
- Update system prompt text in agent definitions
- Keep the extension folder named `commander/` (avoid rename churn) but update internal terminology
- Ensure existing tests still pass with new names

### Tests
- All existing commander tests pass with renamed types
- Test: `AgentRole` includes `director`, `tech-lead`, `engineer`, `reviewer`

---

## Feature 2: Director Read-Only Restriction

### What to Build
Strip edit/write/bash tools from Director tier. Director is read-only + invoke.

**File:** `packages/extensions/commander/src/orchestrator.ts` (modify)

**Implementation:**
- Define tool allowlists per tier:
```typescript
const TIER_TOOLS: Record<AgentRole, { allowed: string[]; denied: string[] }> = {
  director: {
    allowed: ['read_file', 'glob', 'grep', 'websearch', 'webfetch', 'invoke_team', 'invoke_subagent', 'attempt_completion', 'remember', 'recall'],
    denied: ['write_file', 'edit', 'create_file', 'bash', 'multiedit', 'apply_patch'],
  },
  'tech-lead': {
    allowed: ['*'], // All tools including edit (for reviewed files) + git merge
    denied: [],
  },
  engineer: {
    allowed: ['*'], // All tools except invoke_team and websearch
    denied: ['invoke_team', 'websearch', 'webfetch'],
  },
  reviewer: {
    allowed: ['read_file', 'glob', 'grep', 'bash', 'attempt_completion'],
    denied: ['write_file', 'edit', 'create_file', 'invoke_team', 'invoke_subagent'],
  },
}
```
- Apply tool filtering when spawning agents at each tier
- Director prompt must emphasize: "You NEVER write code directly. You plan, delegate, and summarize."

### Tests
- `packages/extensions/commander/src/orchestrator.test.ts` — Director cannot use edit tools
- Test: Tech Lead has edit access
- Test: Engineer cannot invoke_team or websearch
- Test: Reviewer can only read + bash (for lint/tests)

---

## Feature 3: `invoke_team` Tool

### Competitor Research
Read these files:
- `docs/reference-code/opencode/packages/opencode/src/` — Look for subagent/task invocation patterns
- `packages/extensions/commander/src/delegate.ts` — Current delegate_* tools

### What to Build
Unified tool for invoking persistent team members (replaces `delegate_coder`, `delegate_reviewer`, `delegate_researcher`, `delegate_explorer`).

**File:** `packages/extensions/commander/src/invoke-team.ts` (new)

```typescript
export interface InvokeTeamInput {
  role: 'tech-lead' | 'engineer'
  task: string
  domain?: string              // e.g., 'frontend', 'backend', 'testing'
  files?: string[]             // scope to specific files
  context?: string             // additional context
  worktree?: boolean           // default: true for engineers
}

export interface InvokeTeamResult {
  agentId: string
  success: boolean
  summary: string
  filesChanged: string[]
  worktreeBranch?: string
}
```

**Implementation:**
- Director can invoke Tech Leads (role: 'tech-lead')
- Tech Leads can invoke Engineers (role: 'engineer')
- Engineers CANNOT invoke team members
- Each invocation spawns a persistent agent with:
  - Its own message history
  - Its own tool allowlist (per tier)
  - Its own worktree (if `worktree: true`)
  - A system prompt tailored to its role
- Worktree creation: `git worktree add .ava/worktrees/<agent-id>/ -b ava/engineer/<agent-id>`
- Agent runs until completion or max turns
- Returns structured result with summary + files changed
- Emit `praxis:lead-assigned` or `praxis:engineer-spawned` events

**Migration:** Remove `delegate_coder`, `delegate_reviewer`, `delegate_researcher`, `delegate_explorer` tools. Register `invoke_team` instead.

### Tests
- `packages/extensions/commander/src/invoke-team.test.ts`
- Test: Director can invoke tech-lead
- Test: Tech Lead can invoke engineer
- Test: Engineer cannot invoke team
- Test: Worktree created for engineer invocations
- Test: Result contains summary and files changed
- Test: Events emitted correctly

---

## Feature 4: `invoke_subagent` Tool

### What to Build
Unified tool for ephemeral helpers (research, review, analysis).

**File:** `packages/extensions/commander/src/invoke-subagent.ts` (new)

```typescript
export interface InvokeSubagentInput {
  type: 'explore' | 'reviewer' | 'research' | 'analyze'
  task: string
  context?: string
  run_validation?: boolean     // for reviewer: lint + typecheck + tests
  changed_files?: string[]     // for reviewer: scope validation
}

export interface InvokeSubagentResult {
  success: boolean
  output: string
  approved?: boolean           // for reviewer type
  feedback?: string            // for reviewer type
  lintPassed?: boolean
  testsPassed?: boolean
  typecheckPassed?: boolean
}
```

**Implementation:**
- Any tier can invoke subagents (Director, Tech Lead, Engineer)
- Subagents are ephemeral — no worktree, no persistent state
- Read-only tools only (except reviewer which gets bash for lint/tests)
- Subagent model defaults to Haiku (cheap/fast), reviewer defaults to Sonnet
- For `type: 'reviewer'` with `run_validation: true`:
  1. Run `npx biome check <changed_files>` → report lint results
  2. Run `npx tsc --noEmit` → report typecheck results
  3. Find test files related to changed_files → run `npx vitest <test-files>`
  4. Review the diff for correctness and conventions
  5. Return `approved: true/false` with feedback
- Emit `praxis:review-requested` / `praxis:review-complete` events

### Tests
- `packages/extensions/commander/src/invoke-subagent.test.ts`
- Test: Subagent is read-only (no write tools)
- Test: Reviewer runs lint, typecheck, tests when run_validation=true
- Test: Reviewer returns approved boolean + feedback
- Test: Subagent uses Haiku model by default
- Test: Reviewer uses Sonnet model by default

---

## Feature 5: Engineer Self-Check Loop

### Competitor Research
Read these files:
- `packages/extensions/validator/src/reviewer.ts` — Existing reviewer logic from Sprint 13
- `docs/planning/praxis-v2-design.md` — Reviewer Capabilities section

### What to Build
Engineer automatically invokes reviewer before presenting work to Tech Lead.

**File:** `packages/extensions/commander/src/engineer-loop.ts` (new)

```typescript
export interface EngineerLoopConfig {
  maxReviewAttempts: number    // default: 3
  autoReview: boolean          // default: true
}

export async function runEngineerWithReview(
  task: string,
  config: EngineerLoopConfig,
  context: AgentContext
): Promise<EngineerResult>
```

**Implementation:**
1. Engineer codes the assigned task
2. Engineer invokes `invoke_subagent({ type: 'reviewer', run_validation: true })`
3. If reviewer approves → Engineer reports completion to Tech Lead
4. If reviewer rejects → Engineer reads feedback, fixes issues, re-invokes reviewer
5. Max 3 review cycles (configurable) — if still failing, report partial with warnings
6. Events: `praxis:review-requested`, `praxis:review-complete` (per cycle)

### Tests
- `packages/extensions/commander/src/engineer-loop.test.ts`
- Test: Engineer invokes reviewer after coding
- Test: Reviewer rejection triggers re-code + re-review
- Test: Max 3 review cycles enforced
- Test: Approved result reports success to parent

---

## Feature 6: Tech Lead Merge Flow

### What to Build
Tech Lead reviews Engineer worktrees, makes small fixes, merges branches.

**File:** `packages/extensions/commander/src/merge-flow.ts` (new)

```typescript
export async function techLeadMerge(
  leadId: string,
  engineerResults: EngineerResult[],
  context: AgentContext
): Promise<MergeResult>
```

**Implementation:**
1. Tech Lead receives completed Engineer results
2. For each Engineer worktree:
   a. Review the diff (`git diff main...<engineer-branch>`)
   b. If small issues: Tech Lead edits files directly in the worktree (has edit access for reviewed files)
   c. If major issues: Re-delegate to Engineer with feedback
   d. If approved: Merge engineer branch (`git merge <engineer-branch>`)
3. After all merges:
   a. Check for conflicts → resolve or spawn new Engineer
   b. Run `npm run test:run` (full integration tests)
   c. Run `npx tsc --noEmit` (full typecheck)
4. Report clean result to Director
5. Cleanup worktrees: `git worktree remove .ava/worktrees/<agent-id>/`
6. Events: `praxis:merge-started`, `praxis:merge-complete`, `praxis:lead-complete`

### Tests
- `packages/extensions/commander/src/merge-flow.test.ts`
- Test: Tech Lead reviews and merges Engineer worktree
- Test: Small fix applied by Tech Lead before merge
- Test: Major issue re-delegates to Engineer
- Test: Conflict detection during merge
- Test: Integration tests run after all merges
- Test: Worktrees cleaned up after merge

---

## Feature 7: Three Operating Modes

### What to Build
Full/Light/Solo modes with auto-detection and slash commands.

**File:** `packages/extensions/commander/src/mode-selector.ts` (new)

```typescript
export type PraxisMode = 'full' | 'light' | 'solo'

export function detectMode(goal: string): PraxisMode

export function getModeConfig(mode: PraxisMode): ModeConfig
```

**Implementation:**

Auto-detection heuristics:
- **Full**: keywords like "sprint", "refactor entire", "redesign", "implement all", "multi-file", "architecture"
- **Light**: keywords like "fix", "add", "update", "change", "modify", "refactor <specific>"
- **Solo**: keywords like "explain", "research", "plan", "analyze", "document", "review", "what is"

Mode configs:
- **Full**: Director → Tech Leads → Engineers (+ reviewers)
- **Light**: Director → Engineers directly (Director acts as Tech Lead)
- **Solo**: Director + subagents only (no code edits)

Slash commands:
- `/praxis full` — force full orchestration
- `/praxis light` — force light delegation
- `/praxis solo` — force solo/research mode
- `/praxis auto` — let Director auto-detect (default)

Register slash commands in `packages/extensions/slash-commands/src/commands.ts`.

Emit `praxis:mode-selected` event.

### Tests
- `packages/extensions/commander/src/mode-selector.test.ts`
- Test: "Implement Sprint 15" → full
- Test: "Fix the bug in auth.ts" → light
- Test: "Explain how the router works" → solo
- Test: Slash command overrides auto-detection

---

## Feature 8: Per-Tier System Prompts

### What to Build
Each tier gets a tailored system prompt that defines its role and boundaries.

**File:** `packages/extensions/commander/src/tier-prompts.ts` (new)

**Implementation:**

Director prompt additions:
```
You are the DIRECTOR. You plan, orchestrate, and communicate — you NEVER write code.
- Decompose tasks into domain chunks for Tech Leads
- Use invoke_team to assign work
- Use invoke_subagent for research
- Summarize results for the user
- After completion, suggest next steps from the roadmap
```

Tech Lead prompt additions:
```
You are a TECH LEAD. You supervise Engineers and ensure quality.
- Assign coding tasks to Engineers via invoke_team
- Review Engineer worktrees when they complete
- Make small fixes (imports, style, minor logic) on reviewed files
- Merge Engineer branches and resolve conflicts
- Run integration tests after merging
- Report clean summary to Director
```

Engineer prompt additions:
```
You are an ENGINEER. You write code in an isolated worktree.
- Focus only on your assigned task and files
- After coding, invoke a reviewer subagent to self-check
- Fix any issues the reviewer finds
- Present your work only after reviewer approves
- You cannot invoke Tech Leads or use web search
```

Reviewer prompt additions:
```
You are a REVIEWER. You validate code quality.
1. Run lint: npx biome check <changed-files>
2. Run typecheck: npx tsc --noEmit
3. Find and run affected tests: npx vitest <test-files>
4. Review the diff for correctness, conventions, edge cases
5. Return approved: true/false with specific feedback
```

### Tests
- `packages/extensions/commander/src/tier-prompts.test.ts`
- Test: Director prompt contains "NEVER write code"
- Test: Engineer prompt contains "isolated worktree"
- Test: Reviewer prompt contains lint/typecheck/test commands

---

## Feature 9: Configurable Model Defaults

### What to Build
Per-role model settings loaded from config.

**File:** `packages/extensions/commander/src/model-config.ts` (new)

```typescript
export interface PraxisModelConfig {
  director: { provider: string; model: string }
  'tech-lead': { provider: string; model: string }
  engineer: { provider: string; model: string }
  reviewer: { provider: string; model: string }
  subagent: { provider: string; model: string }
}

export function loadModelConfig(): PraxisModelConfig
export function getModelForRole(role: AgentRole): { provider: string; model: string }
```

**Implementation:**
- Default models:
  - Director: `anthropic/claude-opus-4-6`
  - Tech Lead: `anthropic/claude-sonnet-4-6`
  - Engineer: `anthropic/claude-haiku-4-5`
  - Reviewer: `anthropic/claude-sonnet-4-6`
  - Subagent: `anthropic/claude-haiku-4-5`
- Override via `~/.ava/config.json` under `praxis.models`
- Override via project `.ava/config.json` (project overrides global)
- Load config at session start, cache for session duration

### Tests
- `packages/extensions/commander/src/model-config.test.ts`
- Test: Defaults returned when no config
- Test: Config override applied
- Test: Project config overrides global config

---

## Feature 10: Graceful Model Degradation

### Competitor Research
Read: `packages/extensions/models/src/` — existing model fallback logic

### What to Build
When a configured model is unavailable, auto-fallback to the next best.

**File:** `packages/extensions/commander/src/model-fallback.ts` (new)

```typescript
export const FALLBACK_CHAINS: Record<AgentRole, string[]> = {
  director: ['claude-opus-4-6', 'claude-sonnet-4-6', 'claude-haiku-4-5'],
  'tech-lead': ['claude-sonnet-4-6', 'claude-haiku-4-5'],
  engineer: ['claude-haiku-4-5', 'gpt-4o-mini'],
  reviewer: ['claude-sonnet-4-6', 'claude-haiku-4-5'],
  subagent: ['claude-haiku-4-5', 'gpt-4o-mini'],
}

export function resolveModel(role: AgentRole, config: PraxisModelConfig): ResolvedModel
```

**Implementation:**
- On `model:unavailable` event, try next in fallback chain
- Log fallback: "Director model (opus) unavailable, falling back to sonnet"
- Emit `praxis:model-fallback` event for UI notification
- Integrate with existing `getFallbackModel()` from models extension if available

### Tests
- `packages/extensions/commander/src/model-fallback.test.ts`
- Test: Primary model available → use it
- Test: Primary unavailable → fallback to next
- Test: All unavailable → error with clear message
- Test: Event emitted on fallback

---

## Feature 11: Director Memory

### Competitor Research
Read: `packages/extensions/memory/src/` — existing memory/recall system

### What to Build
Director auto-queries recall at session start for cross-session continuity.

**File:** `packages/extensions/commander/src/director-memory.ts` (new)

**Implementation:**
- On `session:opened`, Director queries recall:
  - Search for past sessions related to the current working directory
  - Search for past decisions and patterns
  - Search for known failures/antipatterns
- Inject relevant memories into Director's context as a prompt section
- Example context: "Previous session found: 'Concurrent edit race had Promise.race cleanup issues. Use AbortController per strategy.'"
- Use existing `memory:search` and `memory:recent` events from memory extension
- Limit to 5 most relevant memories to avoid context bloat

### Tests
- `packages/extensions/commander/src/director-memory.test.ts`
- Test: Memory queried on session start
- Test: Relevant memories injected into context
- Test: Max 5 memories limit enforced
- Test: No crash when memory extension unavailable

---

## Feature 12: Recommendation Engine

### What to Build
After task completion, Director suggests next steps from roadmap.

**File:** `packages/extensions/commander/src/recommendations.ts` (new)

**Implementation:**
- On `praxis:complete`, Director:
  1. Reads roadmap from `docs/planning/` (glob for markdown files)
  2. Identifies what was just completed
  3. Checks dependencies: what's now unblocked?
  4. Suggests next action with reasoning
- Output format: "Task X complete. Recommendation: Y is now unblocked and can start. Z is independent and can run in parallel."
- Inject as final message in Director's summary to user
- Graceful skip if no roadmap files exist

### Tests
- `packages/extensions/commander/src/recommendations.test.ts`
- Test: Reads roadmap files
- Test: Identifies completed vs pending items
- Test: Suggests unblocked next step
- Test: Graceful skip when no roadmap

---

## Feature 13: Progress Dashboard Events

### What to Build
Structured events that the UI TeamPanel can consume to show the 4-tier hierarchy.

**File:** `packages/extensions/commander/src/progress.ts` (new)

```typescript
export interface PraxisProgress {
  mode: PraxisMode
  leads: LeadProgress[]
}

export interface LeadProgress {
  id: string
  domain: string
  status: 'pending' | 'active' | 'complete' | 'failed'
  engineers: EngineerProgress[]
}

export interface EngineerProgress {
  id: string
  task: string
  status: 'coding' | 'reviewing' | 'approved' | 'merging' | 'complete' | 'failed'
  reviewAttempts: number
}
```

**Implementation:**
- Maintain progress state by listening to all `praxis:*` events
- Expose `getProgress()` for UI consumption
- Emit `praxis:progress-updated` on any state change
- UI `TeamPanel` at `src/components/panels/TeamPanel.tsx` should render the tree structure

### Tests
- `packages/extensions/commander/src/progress.test.ts`
- Test: Progress tree built from events
- Test: Engineer status transitions correctly
- Test: Lead completes when all engineers complete

---

## Feature 14: UI Team Panel Update

### What to Build
Update the desktop TeamPanel to show the 4-tier hierarchy with progress.

**File:** `src/components/panels/TeamPanel.tsx` (modify)

**IMPORTANT**: This is **SolidJS** (NOT React). Use `createSignal`, `Show`, `For`, `onCleanup`.

**Implementation:**
- Listen to `praxis:progress-updated` events
- Render tree structure:
```
Director (Solo/Light/Full mode)
├── Tech Lead: Frontend ✅
│   ├── Engineer 1: streaming-fuzzy.ts ✅ (reviewed 1x)
│   ├── Engineer 2: four-pass.ts ✅ (reviewed 2x)
│   └── Merge: ✅ All tests pass
├── Tech Lead: Backend 🔄
│   ├── Engineer 3: race.ts ✅
│   └── Engineer 4: windowed.ts 🔄 reviewing...
└── Overall: 4/5 engineers complete
```
- Color coding: green (complete), yellow (active), red (failed), gray (pending)
- Expandable nodes: click to see details (task, files, review feedback)
- Live updates as events stream in

### Tests
- `src/components/panels/TeamPanel.test.tsx` (update existing)
- Test: Renders 4-tier hierarchy
- Test: Status colors correct
- Test: Live update on progress event

---

## Post-Implementation Verification

After ALL 14 features:

1. `npm run test:run`
2. `npx tsc --noEmit`
3. `npm run lint`
4. `npm run format:check`
5. Verify no files exceed 300 lines
6. Test the 3 modes manually:
   - Solo: `ava run "explain how the router works" --verbose`
   - Light: `ava run "fix the import in auth.ts" --verbose`
   - Full: `ava run "implement sprint 15" --verbose`
7. Commit: `git commit -m "feat(sprint-17): Praxis v2 multi-agent hierarchy"`

---

## File Change Summary

| Action | File |
|--------|------|
| MODIFY | `packages/extensions/commander/src/types.ts` (rename hierarchy) |
| MODIFY | `packages/extensions/commander/src/workers.ts` (rename agents) |
| MODIFY | `packages/extensions/commander/src/orchestrator.ts` (Director read-only, tier tools) |
| MODIFY | `packages/extensions/commander/src/orchestrator.test.ts` |
| MODIFY | `packages/extensions/commander/src/delegate.ts` (remove old delegate_* tools) |
| MODIFY | `packages/extensions/commander/src/delegate.test.ts` |
| MODIFY | `packages/extensions/commander/src/router.ts` (update labels) |
| MODIFY | `packages/extensions/commander/src/router.test.ts` |
| MODIFY | `packages/extensions/commander/src/agent-definition.ts` |
| MODIFY | `packages/extensions/commander/src/index.ts` (register new tools) |
| MODIFY | `packages/extensions/commander/src/index.test.ts` |
| MODIFY | `packages/extensions/commander/src/workers.test.ts` |
| CREATE | `packages/extensions/commander/src/invoke-team.ts` |
| CREATE | `packages/extensions/commander/src/invoke-team.test.ts` |
| CREATE | `packages/extensions/commander/src/invoke-subagent.ts` |
| CREATE | `packages/extensions/commander/src/invoke-subagent.test.ts` |
| CREATE | `packages/extensions/commander/src/engineer-loop.ts` |
| CREATE | `packages/extensions/commander/src/engineer-loop.test.ts` |
| CREATE | `packages/extensions/commander/src/merge-flow.ts` |
| CREATE | `packages/extensions/commander/src/merge-flow.test.ts` |
| CREATE | `packages/extensions/commander/src/mode-selector.ts` |
| CREATE | `packages/extensions/commander/src/mode-selector.test.ts` |
| CREATE | `packages/extensions/commander/src/tier-prompts.ts` |
| CREATE | `packages/extensions/commander/src/tier-prompts.test.ts` |
| CREATE | `packages/extensions/commander/src/model-config.ts` |
| CREATE | `packages/extensions/commander/src/model-config.test.ts` |
| CREATE | `packages/extensions/commander/src/model-fallback.ts` |
| CREATE | `packages/extensions/commander/src/model-fallback.test.ts` |
| CREATE | `packages/extensions/commander/src/director-memory.ts` |
| CREATE | `packages/extensions/commander/src/director-memory.test.ts` |
| CREATE | `packages/extensions/commander/src/recommendations.ts` |
| CREATE | `packages/extensions/commander/src/recommendations.test.ts` |
| CREATE | `packages/extensions/commander/src/progress.ts` |
| CREATE | `packages/extensions/commander/src/progress.test.ts` |
| MODIFY | `packages/extensions/slash-commands/src/commands.ts` (add /praxis commands) |
| MODIFY | `src/components/panels/TeamPanel.tsx` (4-tier hierarchy UI) |
| MODIFY | `src/components/panels/team/TeamCard.tsx` (update for new tiers) |
