# Delta9 vs oh-my-opencode Gap Analysis

Detailed comparison after Phase 1-6 enhancements.

---

## Summary

| Pattern | oh-my-opencode | Delta9 Status |
|---------|---------------|---------------|
| Intent Classification (Metis) | ✅ Full | ✅ Implemented (Phase 6) |
| Commander Guard | ✅ Full | ✅ Implemented (Phase 1) |
| File Assignment | ✅ Full | ✅ Implemented (Phase 2) |
| Compaction Context | ✅ Full | ✅ Implemented (Phase 3) |
| Operator Handoff | ✅ Full | ✅ Implemented (Phase 4) |
| Output Guardrails | ✅ Full | ✅ Implemented (Phase 5) |
| **Phased Agent Behavior** | ✅ Full | ❌ Missing |
| **Dynamic Prompt Building** | ✅ Full | ❌ Missing |
| **Category-Based Model Routing** | ✅ Full | ⚠️ Partial |
| **Skill Loading in Delegation** | ✅ Full | ❌ Missing |
| **Operator Tool Blocking** | ✅ Full | ❌ Missing |
| **Edit Error Recovery Hook** | ✅ Full | ❌ Missing |
| **Agent Prompt Metadata** | ✅ Full | ⚠️ Partial |
| **Prometheus Planning Agent** | ✅ Full | ❌ Missing |
| **AI-Slop Detection** | ✅ Full | ❌ Missing |
| **Notepad System** | ✅ Full | ❌ Missing |

---

## Critical Gaps (P0)

### 1. Phased Agent Behavior

**oh-my-opencode**: Sisyphus operates through distinct phases with different behaviors.

```
Intent Gate → Codebase Assessment → Exploration → Implementation → Completion
```

Each phase has:
- Different tool restrictions
- Different delegation triggers
- Phase-specific prompts

**Delta9 Current**: Commander has a single prompt. No phased behavior.

**Required Changes**:
- Add `CommanderPhase` enum
- Create phase-specific prompt builders
- Add phase transition logic
- Track current phase in mission state

```typescript
type CommanderPhase =
  | 'intent_gate'      // Classify request, check triggers
  | 'assessment'       // Fire explore/scout agents
  | 'exploration'      // Deep research if needed
  | 'planning'         // Create mission/objectives
  | 'orchestration'    // Dispatch and coordinate
  | 'completion'       // Verify and wrap up
```

---

### 2. Dynamic Prompt Building

**oh-my-opencode**: Prompts are assembled dynamically based on available resources.

```typescript
buildToolSelectionTable(agents, tools, skills)
buildExploreSection(agents)
buildDelegationTable(agents)
buildCategorySkillsDelegationGuide(categories, skills)
buildOracleSection(agents)
buildHardBlocksSection()
buildAntiPatternsSection()
```

**Delta9 Current**: Static prompts in agent files. No dynamic assembly.

**Required Changes**:
- Create `src/prompts/dynamic-builder.ts`
- Build sections based on available agents/tools
- Include cost/trigger information
- Inject anti-patterns from learning engine

```typescript
interface DynamicPromptSections {
  toolSelectionTable: string      // Cost-based tool guide
  delegationTable: string         // Domain → Agent → Trigger
  categoryGuide: string           // Category selection protocol
  hardBlocks: string              // Things agent MUST NOT do
  antiPatterns: string            // Learned failures to avoid
}
```

---

### 3. Operator Tool Blocking (Executor Blocking)

**oh-my-opencode**: Sisyphus-Junior is BLOCKED from delegation tools.

```typescript
const BLOCKED_TOOLS = ["task", "delegate_task"]
```

**Purpose**: Prevents infinite delegation loops - executors execute, they don't delegate.

**Delta9 Current**: Operators can call `delegate_task`. No blocking.

**Required Changes**:
- Create `src/guards/operator-guard.ts`
- Block `delegate_task`, `dispatch_task` for operators
- Add to `tool.execute.before` hook

```typescript
const OPERATOR_BLOCKED_TOOLS = [
  'delegate_task',
  'dispatch_task',
  'mission_create',
  'mission_add_objective',
  // Operators execute, they don't orchestrate
]
```

---

### 4. Edit Error Recovery Hook

**oh-my-opencode**: Detects Edit tool failures and injects recovery instructions.

```typescript
const EDIT_ERROR_PATTERNS = [
  "oldString and newString must be different",
  "oldString not found",
  "oldString found multiple times",
]

const EDIT_ERROR_REMINDER = `
[EDIT ERROR - IMMEDIATE ACTION REQUIRED]

You made an Edit mistake. STOP and do this NOW:
1. READ the file immediately to see its ACTUAL current state
2. VERIFY what the content really looks like
3. APOLOGIZE briefly to the user for the error
4. CONTINUE with corrected action
`
```

**Delta9 Current**: No edit error detection. Agents may retry blindly.

**Required Changes**:
- Create `src/hooks/edit-error-recovery.ts`
- Detect edit error patterns in `tool.execute.after`
- Inject recovery instructions into next message

---

### 5. Skill Loading in Delegation

**oh-my-opencode**: Skills injected at delegation time.

```typescript
delegate_task({
  category: "visual-engineering",
  load_skills: ["playwright", "frontend-ui-ux"],
  prompt: "..."
})
```

**Skill Selection Protocol**:
1. Select category - Match task to domain
2. Evaluate ALL skills - Ask: "Does this overlap with my task?"
3. Justify omissions - Provide reason for skipping

**Delta9 Current**: `delegate_task` doesn't have `load_skills` parameter. Skills loaded separately.

**Required Changes**:
- Add `loadSkills?: string[]` to delegate_task args
- Resolve skills before spawning agent
- Inject skill content into agent prompt

---

## High Priority Gaps (P1)

### 6. Category-Based Model Routing

**oh-my-opencode**: Different models for different task domains.

```typescript
const DEFAULT_CATEGORIES = {
  "visual-engineering": { model: "gemini-3-pro" },
  "ultrabrain": { model: "gpt-5.2-codex" },
  "quick": { model: "claude-haiku-4-5" },
  "writing": { model: "gemini-3-flash" },
}
```

**Delta9 Current**: Category detection exists (`src/routing/categories.ts`) but doesn't change models. All operators use same model.

**Required Changes**:
- Map categories to optimal models
- Update `delegate_task` to use category-specific model
- Add category prompt appends (domain-specific context)

---

### 7. Agent Prompt Metadata

**oh-my-opencode**: Structured metadata for agent selection.

```typescript
interface AgentPromptMetadata {
  category: "orchestrator" | "advisor" | "executor" | "utility"
  cost: "FREE" | "CHEAP" | "EXPENSIVE"
  promptAlias: string
  triggers: Array<{ domain: string; trigger: string }>
  useWhen: string[]
  avoidWhen: string[]
  keyTrigger?: string
}
```

**Delta9 Current**: Agents have metadata but not in this structured format. No cost tracking.

**Required Changes**:
- Extend `AgentConfig` with metadata
- Add cost estimation per agent
- Use metadata in dynamic prompt building

---

### 8. AI-Slop Pattern Detection

**oh-my-opencode**: Metis detects AI over-engineering patterns.

| Pattern | Example | Question to Ask |
|---------|---------|-----------------|
| Scope inflation | "Also tests for adjacent modules" | "Should I add tests beyond [TARGET]?" |
| Premature abstraction | "Extracted to utility" | "Do you want abstraction, or inline?" |
| Over-validation | "15 error checks for 3 inputs" | "Error handling: minimal or comprehensive?" |
| Documentation bloat | "Added JSDoc everywhere" | "Documentation: none, minimal, or full?" |

**Delta9 Current**: Intent classifier exists but no slop detection.

**Required Changes**:
- Add slop pattern detection to intent classifier
- Generate clarifying questions when patterns detected
- Add to Validator checks

---

## Medium Priority Gaps (P2)

### 9. Prometheus Planning Agent

**oh-my-opencode**: Dedicated planning consultant that NEVER implements.

```
"YOU ARE A PLANNER. YOU ARE NOT AN IMPLEMENTER. YOU DO NOT WRITE CODE."
```

Features:
- Interview Mode (default)
- Plan Generation (auto-transitions)
- Momus Loop (high-accuracy review)
- Draft Management (`.sisyphus/drafts/`)
- Plan Output (`.sisyphus/plans/`)

**Delta9 Current**: Commander does planning and orchestration. No dedicated planner.

**Required Changes**:
- Create `src/agents/council/planner.ts`
- Add planning-only council member
- Implement draft/plan file management
- Add interview mode

---

### 10. Notepad System

**oh-my-opencode**: Sisyphus-Junior records learnings during execution.

**Delta9 Current**: No per-execution notepad. Learnings go to global learning engine.

**Required Changes**:
- Add scratchpad per task execution
- Record discoveries, assumptions, decisions
- Make available to Validator
- Archive with task completion

---

## Already Implemented ✅

| Pattern | Implementation |
|---------|---------------|
| Intent Classification | `src/planning/intent-classifier.ts` |
| Commander Guard | `src/guards/commander-guard.ts` |
| File Assignment | `src/types/mission.ts` (files, filesReadonly, mustNot) |
| Conflict Detection | `src/mission/conflict-detector.ts` |
| Compaction Context | `src/hooks/compaction.ts` (5-section template) |
| Operator Handoff | `src/dispatch/handoff.ts` |
| Output Guardrails | `src/lib/output-guardrails.ts` |

---

## Implementation Priority

### Phase 7: Critical Gaps (Week 1)

1. **Operator Guard** - Block operators from delegation tools
2. **Edit Error Recovery** - Detect and recover from edit failures
3. **Skill Loading in Delegation** - Add `loadSkills` to delegate_task

### Phase 8: High Priority (Week 2)

4. **Phased Commander Behavior** - Add phase transitions
5. **Dynamic Prompt Building** - Build prompts from available resources
6. **Category Model Routing** - Optimal model per category

### Phase 9: Polish (Week 3)

7. **Agent Metadata** - Structured metadata with cost
8. **AI-Slop Detection** - Detect over-engineering
9. **Notepad System** - Per-execution scratchpad
10. **Prometheus Planner** - Dedicated planning agent

---

## Files to Create

| Phase | File | Purpose |
|-------|------|---------|
| 7 | `src/guards/operator-guard.ts` | Block delegation for operators |
| 7 | `src/hooks/edit-error-recovery.ts` | Edit error detection |
| 7 | (modify) `src/tools/delegation.ts` | Add loadSkills parameter |
| 8 | `src/agents/phases.ts` | Commander phase definitions |
| 8 | `src/prompts/dynamic-builder.ts` | Dynamic prompt assembly |
| 8 | `src/routing/model-selector.ts` | Category-to-model mapping |
| 9 | `src/types/agent-metadata.ts` | Structured metadata |
| 9 | `src/planning/slop-detector.ts` | AI-slop pattern detection |
| 9 | `src/mission/notepad.ts` | Per-execution scratchpad |
| 9 | `src/agents/council/planner.ts` | Dedicated planning agent |

---

## Quick Wins (Can Do Now)

1. **Operator Guard** - Simple, high impact, 1 hour
2. **Edit Error Recovery** - Pattern matching in hook, 2 hours
3. **Skill Loading** - Add parameter, resolve skills, 2 hours

These three would bring Delta9 much closer to oh-my-opencode's robustness.
