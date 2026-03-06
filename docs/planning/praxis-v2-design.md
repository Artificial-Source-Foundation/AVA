# Praxis v2 — Multi-Agent Hierarchy Design

> Redesign of AVA's agent orchestration system.
> Status: Design phase
> Date: 2026-03-06

---

## Overview

Praxis v2 replaces the current flat commander/lead/worker model with a 4-tier hierarchy that mirrors a real software engineering organization. The key insight: **the Director never touches code** — it orchestrates, documents, and communicates with the user. Actual work flows down through Tech Leads and Engineers, with self-check loops at every tier.

---

## Hierarchy

```
USER (Stakeholder / CEO)
  │
  ▼
DIRECTOR (Engineering Manager)                 Tier 0
  │  - Plans, documents, orchestrates
  │  - NO edit tools — read-only + invoke others
  │  - Uses subagents for research & doc updates
  │  - Reports to user with summaries + recommendations
  │
  ├──▶ TECH LEAD (Staff / Senior Engineer)     Tier 1
  │      │  - Supervises a team of Engineers
  │      │  - Reviews Engineers' work + can make small fixes
  │      │  - Merges worktrees when Engineers finish
  │      │  - Resolves merge conflicts
  │      │  - Runs final validation before reporting to Director
  │      │  - Reports clean summary to Director
  │      │
  │      ├──▶ ENGINEER (Software Engineer)     Tier 2
  │      │      │  - Does the actual coding
  │      │      │  - Works in isolated git worktree
  │      │      │  - Self-checks via reviewer subagent
  │      │      │  - Presents to Tech Lead only after reviewer approves
  │      │      │
  │      │      └──▶ REVIEWER (Code Review)    Tier 3
  │      │             - Good model (Sonnet-tier) for quality
  │      │             - Runs lint + typecheck + affected tests
  │      │             - Reviews code for correctness & conventions
  │      │             - Engineer iterates until approved
  │      │
  │      ├──▶ ENGINEER 2 (own worktree)
  │      │      └──▶ REVIEWER
  │      └──▶ ENGINEER 3 (own worktree)
  │             └──▶ REVIEWER
  │
  ├──▶ TECH LEAD 2 (Backend)
  │      └──▶ Engineers...
  │
  └──▶ SUBAGENT (Director's own)
         - Read-only research, codebase scanning
         - NOT a worker — disposable helper
```

---

## Key Terminology

| Term | Tier | Role | Real Company Equivalent |
|------|------|------|------------------------|
| **Director** | 0 | Orchestrator, planner, user-facing | Engineering Manager |
| **Tech Lead** | 1 | Team supervisor, reviewer, merge gatekeeper | Staff / Senior Engineer |
| **Engineer** | 2 | Coder, implementer, isolated worker | Software Engineer |
| **Reviewer** | 3 | Automated quality gate for Engineers | CI / Code Review Bot |
| **Subagent** | any | Ephemeral read-only helper at any tier | Research Assistant |

### Subagents vs Team Members

**This distinction is critical:**

- **Team members** (Tech Leads, Engineers) are persistent agents with state, worktrees, and history. They produce code changes that must be reviewed and merged up the chain.
- **Subagents** are ephemeral, disposable, read-only helpers. Any tier can invoke them for research, validation, or analysis. They don't persist, don't have worktrees, and don't edit code.
- **Reviewers** are a specific type of subagent that Engineers invoke to self-check before presenting to Tech Leads. Unlike generic subagents, Reviewers run lint, typecheck, and tests — they need a good model (Sonnet-tier).

---

## Three Operating Modes

### Mode 1: Full Orchestration

For large tasks: sprints, multi-file features, architecture changes.

```
User → Director → Tech Leads → Engineers (+ reviewers)
```

- Director decomposes the task into domain-specific chunks
- Assigns each chunk to a Tech Lead (frontend, backend, QA, etc.)
- Each Tech Lead spawns Engineers in isolated worktrees
- Engineers code, self-check with reviewers
- Tech Leads review, make small fixes, merge worktrees, report to Director
- Director summarizes and presents to user

### Mode 2: Light Delegation

For medium tasks: fix a bug, add a feature, refactor a module.

```
User → Director → Engineers directly (no Tech Leads)
```

- Director analyzes the task
- Spawns 1-3 Engineers directly (skips Tech Lead tier)
- Director reviews results itself (acts as Tech Lead)
- Faster for tasks that don't need team coordination

### Mode 3: Solo Director

For research, planning, documentation, analysis.

```
User → Director + Subagents only
```

- Director uses subagents for codebase exploration
- No code edits — only research, planning, documentation
- Director produces plans, recommendations, or doc updates
- Subagents scan code, read files, search the web

### Mode Selection

Mode can be:
1. **Explicit** — User selects via UI button or slash command (`/director full`, `/director light`, `/director solo`)
2. **Auto-detected** — Director analyzes task complexity and picks mode:
   - Keywords like "sprint", "refactor entire", "redesign" → Full
   - Keywords like "fix", "add", "update" → Light
   - Keywords like "explain", "research", "plan", "analyze" → Solo

---

## Tool Access by Tier

| Tool Category | Director | Tech Lead | Engineer | Reviewer | Subagent |
|--------------|----------|-----------|----------|----------|----------|
| Read (read_file, glob, grep, ls) | ✅ | ✅ | ✅ | ✅ | ✅ |
| Write (write_file, edit, create_file) | ❌ | ✅ (reviewed files + merge fixes) | ✅ | ❌ | ❌ |
| Execute (bash) | ❌ | ✅ (tests, merge validation) | ✅ | ✅ (lint, typecheck, tests) | ❌ |
| Search (websearch, webfetch) | ✅ | ✅ | ❌ | ❌ | ✅ |
| Invoke Tech Lead | ✅ | ❌ | ❌ | ❌ | ❌ |
| Invoke Engineer | ❌ | ✅ | ❌ | ❌ | ❌ |
| Invoke Subagent | ✅ | ✅ | ✅ (reviewer only) | ❌ | ❌ |
| Attempt Completion | ✅ | ✅ | ✅ | ✅ | ✅ |
| Git (merge, worktree) | ❌ | ✅ | ❌ | ❌ | ❌ |

### Tech Lead Edit Access

Tech Leads can edit files that Engineers worked on (to make small fixes found during review) but should delegate new work to Engineers. This avoids full round-trips for trivial issues like wrong imports, style fixes, or minor logic errors. For substantial changes, Tech Leads re-delegate to an Engineer.

### Reviewer Capabilities

Reviewers are NOT cheap throwaway agents. They use a good model (Sonnet-tier) and run real validation:

1. **Lint** — `npx biome check <changed-files>` (scoped to files the Engineer touched)
2. **Typecheck** — `npx tsc --noEmit` (full project, TS doesn't support partial)
3. **Tests** — Find test files related to changed files → `npx vitest <test-files>` (scoped)
4. **Code review** — Read the diff, check for correctness, conventions, edge cases, missing tests

The Reviewer reports back to the Engineer with:
- `approved: true` — Engineer can present to Tech Lead
- `approved: false` + feedback — Engineer must fix and re-invoke Reviewer

---

## Model Defaults (Configurable)

| Tier | Default Model | Rationale |
|------|--------------|-----------|
| Director | Opus | Best reasoning for planning & orchestration |
| Tech Lead | Sonnet | Good judgment for code review & merge decisions |
| Engineer | Haiku / mini | Bulk coding work, cost-efficient |
| Reviewer | **Sonnet** | Must catch real bugs, runs lint/tests — needs quality |
| Subagent | Haiku / mini | Fast research reads |

All defaults are overridable per-role in settings:
```json
{
  "praxis": {
    "models": {
      "director": { "provider": "anthropic", "model": "claude-opus-4-6" },
      "tech-lead": { "provider": "anthropic", "model": "claude-sonnet-4-6" },
      "engineer": { "provider": "openrouter", "model": "anthropic/claude-haiku-4-5" },
      "reviewer": { "provider": "anthropic", "model": "claude-sonnet-4-6" },
      "subagent": { "provider": "openrouter", "model": "anthropic/claude-haiku-4-5" }
    }
  }
}
```

---

## Worktree Isolation

### Engineer Worktrees
- Each Engineer gets an isolated git worktree: `.ava/worktrees/<session-id>/`
- Worktree has its own branch: `ava/engineer/<session-id>`
- Engineer codes freely in its worktree without affecting main branch
- On completion, Engineer's branch is ready for Tech Lead to review/merge

### Tech Lead Merge Flow
1. Tech Lead reviews each Engineer's worktree diff
2. If approved (maybe with small fixes): Tech Lead merges Engineer's branch into working branch
3. If conflicts: Tech Lead resolves them (has edit + git tools)
4. If rejected: Tech Lead sends feedback, Engineer iterates
5. After all Engineers merged: Tech Lead runs final validation, reports clean result to Director

### Conflict Resolution
When multiple Engineers edit overlapping files:
- Tech Lead detects conflicts during merge
- Tech Lead resolves conflicts using its code understanding
- If too complex: Tech Lead can spawn a new Engineer to fix conflicts

---

## Communication Flow

### Upward (Results)
```
Engineer → "Here's what I coded + reviewer approved it"
  → Tech Lead → "Here's the merged result from 3 engineers, all tests pass"
    → Director → "Sprint 12 Feature 1 complete. 4 files changed, tests pass. Recommend Feature 2 next."
      → User
```

### Downward (Tasks)
```
User → "Implement Sprint 12"
  → Director → decomposes into domain chunks
    → Tech Lead (Frontend) → "Implement streaming fuzzy matcher"
      → Engineer 1 → "Code the DP matrix in streaming-fuzzy-matcher.ts"
      → Engineer 2 → "Code the integration in streaming-edit-parser.ts"
      → Engineer 3 → "Write tests in streaming-fuzzy-matcher.test.ts"
```

### Context Window Protection
- Director's context stays clean — it only sees summaries, not raw code
- Tech Leads see their domain's code but not other domains
- Engineers see only their assigned files
- This prevents context window rot from too much detail

---

## Invocation API

### Unified Invocation Tools (inspired by OpenCode)

Instead of separate `delegate_coder`, `delegate_researcher` tools, use two unified tools:

```typescript
// Director invoking a Tech Lead
invoke_team({
  role: 'tech-lead',
  domain: 'frontend',
  task: 'Implement streaming fuzzy matcher',
  context: 'See docs/reference-code/zed/ for patterns',
})

// Tech Lead invoking Engineers
invoke_team({
  role: 'engineer',
  task: 'Code the DP matrix algorithm',
  files: ['packages/extensions/tools-extended/src/edit/streaming-fuzzy-matcher.ts'],
  worktree: true
})

// Engineer invoking reviewer
invoke_subagent({
  type: 'reviewer',
  task: 'Review this code for correctness and conventions',
  context: currentDiff,
  run_validation: true  // lint + typecheck + affected tests
})

// Director invoking research subagent
invoke_subagent({
  type: 'explore',
  task: 'Scan docs/reference-code/zed/ for streaming diff patterns',
})
```

Two distinct tools:
- `invoke_team` — for persistent team members (Tech Leads, Engineers) with worktrees and state
- `invoke_subagent` — for ephemeral helpers (research, review) that return and dispose

---

## Event Flow

```
praxis:mode-selected      { mode: 'full' | 'light' | 'solo' }
praxis:lead-assigned      { leadId, domain, task }
praxis:engineer-spawned   { engineerId, leadId, worktree, task }
praxis:review-requested   { engineerId, reviewerId }
praxis:review-complete    { engineerId, reviewerId, approved, feedback, lintPassed, testsPassed }
praxis:engineer-complete  { engineerId, leadId, success, diff }
praxis:merge-started      { leadId, engineerIds }
praxis:merge-complete     { leadId, success, conflicts }
praxis:lead-complete      { leadId, directorId, summary }
praxis:complete           { directorId, summary, mode }
```

---

## Comparison with Current System

| Aspect | Praxis v1 (Current) | Praxis v2 (New) |
|--------|---------------------|------------------|
| Tiers | 3 (Commander, Lead, Worker) | 4 (Director, Tech Lead, Engineer, Reviewer) |
| Top-level edits code | Yes (Commander has all tools) | No (Director is read-only) |
| Self-check loop | None | Engineer → Reviewer → iterate |
| Reviewer validation | None | Lint + typecheck + affected tests |
| Merge responsibility | Manual / ad-hoc | Tech Lead merges worktrees |
| Context protection | None (Commander sees everything) | Summaries bubble up, details stay down |
| Mode selection | Implicit | Explicit 3 modes (full/light/solo) |
| Subagent distinction | No (subagent = worker) | Yes (subagent ≠ team member) |
| Worktree per worker | Optional | Default for Engineers |
| Model per tier | Optional override | Configurable defaults per role |
| Naming | Commander/Lead/Worker | Director/Tech Lead/Engineer |
| Invocation tools | 4 separate delegate_* tools | 2 unified tools (invoke_team + invoke_subagent) |

---

## Implementation Plan

### Sprint 17: Praxis v2 Core

**Hierarchy & Tools:**
1. **Rename hierarchy** — Commander → Director, Lead → Tech Lead, Worker → Engineer in all code
2. **Strip Director tools** — Remove edit/write/bash, keep read-only + invoke
3. **Implement `invoke_team` tool** — Unified team invocation (replaces `delegate_*`)
4. **Implement `invoke_subagent` tool** — Ephemeral subagent invocation

**Quality Loops:**
5. **Add reviewer subagent** — Engineer self-check loop with lint/typecheck/affected tests
6. **Tech Lead merge flow** — Worktree review + small fixes + merge + conflict resolution
7. **Tech Lead integration tests** — After merging all Engineers, Tech Lead runs full `npm run test:run` + `npx tsc --noEmit` to catch cross-engineer breakage

**Modes & UX:**
8. **3 operating modes** — Full/Light/Solo with auto-detection + slash commands (`/director full|light|solo`)
9. **Update system prompts** — Per-tier prompt guidance
10. **Update UI** — Team panel shows 4-tier hierarchy with structured progress dashboard
11. **Configurable model defaults** — Per-role model settings in praxis config

**Intelligence:**
12. **Director memory** — Auto-query FTS5 recall at session start. Director remembers past decisions, failed approaches, successful patterns across sessions.
13. **Recommendation engine** — After each completed task, Director reads the roadmap (`docs/planning/`) and suggests next steps based on dependencies and what was just built.
14. **Graceful model degradation** — If configured model for any tier is unavailable, auto-fallback to next-best model using existing `getFallbackModel()` chain.

### Sprint 18: Billing & Ecosystem (Deferred)

- `x-initiator` header support for Copilot billing
- Provider billing metadata awareness

---

## Director Memory & Recommendations

### Cross-Session Memory

Director automatically queries the memory extension at session start:
- Searches for past sessions related to the current task domain
- Loads relevant decisions, patterns, and failures
- Avoids repeating known-bad approaches

Example: "Last sprint, the concurrent edit race approach had issues with Promise.race cleanup. This time, use AbortController per strategy."

### Proactive Recommendations

After each completed task, Director:
1. Reads current roadmap from `docs/planning/post-v2-sota-backlog.md`
2. Checks which sprints/features are done vs pending
3. Identifies what depends on the just-completed work
4. Suggests next action: "Sprint 12 Feature 3 is done. Feature 4 (auto-formatting detection) depends on it and can start now. Feature 5 is independent and can run in parallel."

This makes Director feel like a real engineering manager — not just "task done", but "task done, here's what we should do next and why."

---

## Progress Dashboard

The UI team panel should show structured progress, not a flat list:

```
Sprint 12: Edit Excellence II
├── Frontend Tech Lead ✅ Done (3/3 engineers)
│   ├── Engineer 1: streaming-fuzzy-matcher.ts ✅
│   ├── Engineer 2: four-pass-matcher.ts ✅
│   └── Engineer 3: tests ✅
├── Backend Tech Lead 🔄 In Progress (1/2 engineers)
│   ├── Engineer 4: race.ts ✅
│   └── Engineer 5: windowed-view.ts 🔄 (reviewer rejected, iterating)
└── Status: 4/5 complete
```

Events that drive this:
- `praxis:engineer-spawned` → add node to tree
- `praxis:review-complete` → update engineer status
- `praxis:engineer-complete` → mark engineer done
- `praxis:merge-complete` → update Tech Lead status
- `praxis:lead-complete` → mark Tech Lead done

---

## Graceful Model Degradation

Each tier has a fallback chain:

| Tier | Primary | Fallback 1 | Fallback 2 |
|------|---------|------------|------------|
| Director | Opus | Sonnet | Haiku |
| Tech Lead | Sonnet | Haiku | — |
| Engineer | Haiku | mini | — |
| Reviewer | Sonnet | Haiku | — |

On model unavailability (`model:unavailable` event):
1. Check `getFallbackModel()` from models extension
2. Switch to fallback for remaining turns
3. Log: "Director model (opus) unavailable, falling back to sonnet"
4. Emit: `praxis:model-fallback` event for UI notification

---

## Future Considerations

- **Dynamic team sizing** — Spawn more Engineers for larger tasks, fewer for simple ones
- **Cross-Lead collaboration** — Frontend Tech Lead can request help from Backend Tech Lead
- **User as reviewer** — Optional mode where user acts as Tech Lead for critical changes
- **Parallel sprint execution** — Multiple Tech Leads working simultaneously on different domains
- **Intern tier** — If needed later, Engineers can use `invoke_subagent` for simple coding tasks that don't need full worktrees (effectively an intern pattern without a formal tier)
