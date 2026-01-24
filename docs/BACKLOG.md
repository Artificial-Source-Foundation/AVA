# Delta9 Project Backlog

> Tracking development progress from Foundation to Launch

---

## Project Status Dashboard

| Phase | Name | Progress | Status |
|-------|------|----------|--------|
| 1 | Foundation | 95% | Nearly Complete |
| 2 | Council System | 88% | Sprint 1 Complete |
| 3 | Intelligence Layer | 0% | Not Started |
| 4 | Robustness | 0% | Not Started |
| 5 | Polish & Support | 0% | Not Started |
| 6 | Launch | 0% | Not Started |

**Overall Progress:** ~32% (Sprint 1 complete, Sprint 2 ready)

**Last Updated:** 2026-01-24

---

## Phase 1: Foundation (95% Complete)

> Core plugin infrastructure, basic agents, and tooling

### Completed

- [x] Plugin scaffold (`src/index.ts`)
- [x] Mission state management
  - [x] `src/mission/state.ts` - MissionState class
  - [x] `src/mission/schema.ts` - Zod validation
  - [x] `src/mission/history.ts` - Action history
  - [x] `src/mission/markdown.ts` - mission.md generation
- [x] Config system
  - [x] `src/lib/config.ts` - Config loader with caching
  - [x] `src/types/config.ts` - Type definitions
  - [x] `src/schemas/config.schema.ts` - Validation
- [x] Commander agent (`src/agents/commander.ts`)
- [x] Execution agents
  - [x] `src/agents/execution/operator.ts` - Standard operator
  - [x] `src/agents/execution/operator-complex.ts` - Multi-file operator
  - [x] `src/agents/execution/validator.ts` - QA verification
- [x] Background task system
  - [x] `src/lib/background-manager.ts` - Task manager
  - [x] SDK integration
  - [x] Graceful shutdown
  - [x] Stale task detection
  - [x] TTL-based cleanup
- [x] Tools (30+ across 8 categories)
  - [x] Mission tools
  - [x] Background tools
  - [x] Delegation tools
  - [x] Memory tools
  - [x] Council tools
  - [x] Routing tools
  - [x] Validation tools
  - [x] Diagnostics tools
- [x] DX improvements
  - [x] `src/lib/logger.ts` - Structured logging
  - [x] `src/lib/errors.ts` - Rich error handling
  - [x] `src/lib/hints.ts` - Context-aware hints
  - [x] `src/tools/diagnostics.ts` - Health check tool
- [x] Unit tests (136 passing)
  - [x] `tests/lib/logger.test.ts`
  - [x] `tests/lib/errors.test.ts`
  - [x] `tests/lib/hints.test.ts`
  - [x] `tests/lib/config.test.ts`
  - [x] `tests/mission/state.test.ts`

### Remaining

- [ ] **F-1: Complete task router** - `src/routing/task-router.ts` is placeholder
  - Acceptance: Routes tasks to correct agent based on complexity/type
  - Priority: Medium (needed for Phase 3)

---

## Phase 2: Council System

> Multi-model planning with The Delta Team

### Epic: Oracle Agents (The Delta Team)

Each Oracle has a distinct personality, temperature, and specialty. Users configure which AI model powers each in `delta9.json`.

| ID | Task | Status | Acceptance Criteria |
|----|------|--------|---------------------|
| C-1 | CIPHER - The Strategist | ✅ Done | Temp 0.2, architecture specialty, returns `{recommendation, confidence, caveats, suggestedTasks}` |
| C-2 | VECTOR - The Analyst | ✅ Done | Temp 0.4, logic specialty, catches edge cases and validates correctness |
| C-3 | PRISM - The Creative | ✅ Done | Temp 0.6, UI/UX specialty, explores alternatives and user impact |
| C-4 | APEX - The Optimizer | ✅ Done | Temp 0.3, performance specialty, Big-O analysis and bottleneck detection |

### Epic: Council Orchestration

| ID | Task | Status | Acceptance Criteria |
|----|------|--------|---------------------|
| C-5 | Council orchestrator | ✅ Done | Parallel Oracle execution, timeout handling, fallback to available models |
| C-6 | Council modes | ✅ Done | NONE (Commander only), QUICK (1 Oracle), STANDARD (all), XHIGH (all + recon) |
| C-7 | Opinion synthesis | ✅ Done | Confidence weighting, consensus detection, conflict resolution |
| C-8 | Council status tool | ⬜ Pending | `council_status` shows Oracle responses, confidences, conflicts |

### Files Created

```
src/agents/council/
├── oracle-cipher.ts   ✅ CIPHER - The Strategist (architecture)
├── oracle-vector.ts   ✅ VECTOR - The Analyst (logic)
├── oracle-prism.ts    ✅ PRISM - The Creative (ui)
├── oracle-apex.ts     ✅ APEX - The Optimizer (performance)
└── index.ts           ✅ Registry with helper functions

src/council/
├── oracle.ts          ✅ Oracle invocation with SDK integration
├── orchestrator.ts    ⬜ Pending
├── modes.ts           ⬜ Pending
├── synthesis.ts       ⬜ Pending
└── types.ts           ⬜ Pending
```

### Reference

See [spec.md#council-layer](spec.md) for Oracle specifications and council modes.

---

## Phase 3: Intelligence Layer

> Support agents and smart routing

### Epic: Support Agents

| ID | Task | Status | Acceptance Criteria |
|----|------|--------|---------------------|
| I-1 | Scout agent | ⬜ Pending | Haiku, fast codebase search (grep, file discovery), returns file list + snippets |
| I-2 | Intel agent | ⬜ Pending | GLM 4.7, documentation lookup, GitHub search, example finding |
| I-3 | Strategist agent | ⬜ Pending | GPT 5.2, mid-execution advice when Operator hits wall |

### Epic: Smart Features

| ID | Task | Status | Acceptance Criteria |
|----|------|--------|---------------------|
| I-4 | XHIGH mode | ⬜ Pending | Each Oracle gets Scout/Intel access before giving opinion |
| I-5 | Complexity detection | ⬜ Pending | Auto-determine council mode based on task analysis |
| I-6 | Smart task routing | ⬜ Pending | Route to specialist (UI-Ops, QA, etc.) by task type |
| I-7 | Codebase knowledge | ⬜ Pending | Persist learned patterns, file structure, common issues |

### Files to Create

```
src/agents/support/
├── scout.ts
├── intel.ts
├── strategist.ts
└── index.ts

src/routing/
├── task-router.ts (update)
├── complexity.ts
└── knowledge.ts
```

### Reference

See [spec.md#support-layer](spec.md) for support agent specifications.

---

## Phase 4: Robustness

> Reliability, recovery, and resource management

### Epic: Reliability Features

| ID | Task | Status | Acceptance Criteria |
|----|------|--------|---------------------|
| R-1 | Mission checkpoints | ⬜ Pending | Save/restore points, resume after crash |
| R-2 | Budget tracking | ⬜ Pending | Token usage per agent category, budget warnings |
| R-3 | Rate limit handling | ⬜ Pending | Graceful degradation, queue requests, fallback models |
| R-4 | Memory/learning | ⬜ Pending | Learn from past missions, avoid repeated mistakes |
| R-5 | Recovery strategies | ⬜ Pending | Auto-recover from agent failures, retry logic |

### Files to Create/Update

```
src/mission/
├── checkpoints.ts
└── recovery.ts

src/lib/
├── budget.ts
├── rate-limiter.ts
└── learning.ts
```

---

## Phase 5: Polish & Support

> Additional agents, templates, and user experience

### Epic: Specialist Agents

| ID | Task | Status | Acceptance Criteria |
|----|------|--------|---------------------|
| S-1 | UI-Ops agent | ⬜ Pending | Gemini Pro, frontend specialist (components, styling, a11y) |
| S-2 | Scribe agent | ⬜ Pending | Gemini Flash, documentation writer (READMEs, API docs) |
| S-3 | Optics agent | ⬜ Pending | Gemini Flash, vision/multimodal (image analysis, diagrams) |
| S-4 | QA agent | ⬜ Pending | Sonnet 4, dedicated test writer |
| S-5 | Patcher agent | ⬜ Pending | Haiku, quick targeted fixes for FIXABLE validation |

### Epic: User Experience

| ID | Task | Status | Acceptance Criteria |
|----|------|--------|---------------------|
| S-6 | Mission templates | ⬜ Pending | Pre-built structures for common tasks (feature, bugfix, refactor) |
| S-7 | Progress notifications | ⬜ Pending | Real-time updates via OpenCode toasts |
| S-8 | CLI commands | ⬜ Pending | `delta9 status`, `delta9 abort`, `delta9 resume` |

### Files to Create

```
src/agents/support/
├── ui-ops.ts
├── scribe.ts
├── optics.ts
├── qa.ts
└── patcher.ts

src/templates/
├── feature.ts
├── bugfix.ts
├── refactor.ts
└── index.ts

src/cli/
├── commands.ts
└── index.ts
```

---

## Phase 6: Launch

> Publishing and marketing

### Epic: Release

| ID | Task | Status | Acceptance Criteria |
|----|------|--------|---------------------|
| L-1 | npm publish | ⬜ Pending | Package published to npm as `delta9` |
| L-2 | Documentation | ⬜ Pending | User guide, configuration docs, examples |
| L-3 | Marketing | ⬜ Pending | GitHub README, social media, OpenCode plugin directory |

---

## Sprint Plan

Recommended sprint structure (each sprint ~2-3 days of work):

### Sprint 1: Council Foundation (COMPLETE ✅)
**Goal:** Basic multi-model planning with The Delta Team

- ✅ C-1: CIPHER - The Strategist (architecture, temp 0.2)
- ✅ C-2: VECTOR - The Analyst (logic, temp 0.4)
- ✅ C-3: PRISM - The Creative (UI/UX, temp 0.6)
- ✅ C-4: APEX - The Optimizer (performance, temp 0.3)
- ✅ C-5: Council orchestrator (parallel execution, timeout, fallback)
- ✅ C-6: Council modes (NONE, QUICK, STANDARD, XHIGH)
- ✅ C-7: Opinion synthesis (consensus extraction, confidence weighting)

### Sprint 2: Advanced Council
**Goal:** Complete council system with synthesis

- C-6: Council modes (STANDARD, XHIGH)
- C-7: Opinion synthesis
- C-8: Council status tool

### Sprint 3: Intelligence Foundation
**Goal:** Support agents for reconnaissance

- I-1: Scout agent
- I-2: Intel agent
- I-5: Complexity detection

### Sprint 4: Advanced Council
**Goal:** Complete council system

- C-6: STANDARD + XHIGH modes
- I-4: XHIGH mode implementation
- C-8: Council status tool

### Sprint 5: Smart Routing
**Goal:** Intelligent task dispatch

- I-3: Strategist agent
- I-6: Smart task routing
- F-1: Complete task router

### Sprint 6: Robustness
**Goal:** Production reliability

- R-1: Mission checkpoints
- R-2: Budget tracking
- R-3: Rate limit handling
- R-5: Recovery strategies

### Sprint 7: Support Agents
**Goal:** Specialist workforce

- S-1: UI-Ops agent
- S-2: Scribe agent
- S-4: QA agent
- S-5: Patcher agent

### Sprint 8: Polish
**Goal:** User experience

- S-3: Optics agent
- S-6: Mission templates
- S-7: Progress notifications

### Sprint 9: Launch
**Goal:** Public release

- L-1: npm publish
- L-2: Documentation
- S-8: CLI commands
- L-3: Marketing

---

## Task Summary

| Category | Pending | In Progress | Complete |
|----------|---------|-------------|----------|
| Phase 1 | 1 | 0 | 12 |
| Phase 2 (Council) | 1 | 0 | 7 |
| Phase 3 (Intelligence) | 7 | 0 | 0 |
| Phase 4 (Robustness) | 5 | 0 | 0 |
| Phase 5 (Polish) | 8 | 0 | 0 |
| Phase 6 (Launch) | 3 | 0 | 0 |
| **Total** | **25** | **0** | **19** |

---

## Notes

### Dependencies

- Phase 2 requires Phase 1 complete (foundation)
- Phase 3 (I-4 XHIGH) requires Phase 2 (C-5 orchestrator)
- Phase 4 can run parallel to Phase 3
- Phase 5 requires Phases 2-3 complete
- Phase 6 requires all phases complete

### Risk Areas

1. **Multi-model API access** - Users may not have all provider subscriptions
2. **Rate limiting** - Heavy council use could hit limits
3. **Context management** - XHIGH mode recon could bloat context

### Quality Gates

Before marking phase complete:
- [ ] All unit tests passing
- [ ] TypeScript strict mode passing
- [ ] Manual integration test in OpenCode
- [ ] Documentation updated

---

## References

- [spec.md](spec.md) - Full specification (SOURCE OF TRUTH)
- [delta9/architecture.md](delta9/architecture.md) - System design
- [plugin-guide/](plugin-guide/) - OpenCode plugin development

---

*This backlog is maintained as the central tracking document for Delta9 development.*
