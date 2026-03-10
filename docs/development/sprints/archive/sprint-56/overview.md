# Sprint 56: Codebase Quality Audit

## Goal

Thorough, automated quality audit of the entire Rust codebase using parallel sub-agents. **Read-only** — no code changes. All findings go into `results/` as structured reports that inform a follow-up fix sprint.

## Approach

The prompt instructs the executing agent to spawn **6 parallel sub-agents**, each auditing a different dimension. After all complete, a final Code Reviewer pass synthesizes findings into a prioritized action plan.

## Audit Dimensions

| Sub-Agent | Focus | Output File |
|-----------|-------|-------------|
| 1. Unwrap Auditor | `unwrap()`/`expect()` in prod code, panic risk | `results/01-unwrap-audit.md` |
| 2. Test Coverage | Missing tests, gaps, untested modules | `results/02-test-coverage.md` |
| 3. Doc Coverage | Missing doc comments on public API | `results/03-doc-coverage.md` |
| 4. Modularity | Large files, split candidates, coupling | `results/04-modularity.md` |
| 5. Performance | Clone abuse, allocation patterns, hot paths | `results/05-performance.md` |
| 6. Code Hygiene | TODO/FIXME, dead code, unsafe, stale imports | `results/06-hygiene.md` |

Final synthesis: `results/00-action-plan.md`

## Rules

- **NO code changes** — audit only
- All output goes to `docs/development/sprints/sprint-56/results/`
- Each report must list findings with file:line references
- Findings categorized as: CRITICAL / HIGH / MEDIUM / LOW
- Code Reviewer invoked after each sub-agent milestone

## Status: Complete
