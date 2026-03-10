# Sprint 57: Quality Fixes (from Sprint 56 Audit)

## Goal

Fix the P0 (Critical) and P1 (High) findings from the Sprint 56 codebase audit.
See `docs/development/sprints/sprint-56/results/00-action-plan.md` for the full findings.

## Phases

| Phase | Prompt | Focus | Priority |
|-------|--------|-------|----------|
| 57a | `01-p0-critical-fixes.md` | 4 crash risks + process violations | P0 Critical |
| 57b | `02-p1-high-fixes.md` | Perf, modularity, docs, tests | P1 High |

## Key Fixes

### P0 (Phase a)
1. `AgentStack` expect panic → proper error propagation
2. LLM parsing layer: 15 pub fns with 0 tests → full test suite
3. `reqwest::Client` expect in pool → `Result` return
4. TUI blanket `#![allow(dead_code)]` → remove + fix warnings

### P1 (Phase b)
1. 11 modal `unwrap()` calls → safe pattern matching
2. `model_catalog.rs` (870 lines) → split into 4 modules
3. God structs: AgentStack (17 fields), Theme (28 fields), AppState (17 fields)
4. Document 9 core traits + 16 error types
5. Clone abuse in agent loop hot paths
6. Gemini provider + memory/codebase tools: 0 tests → add coverage

## Status: Planned
