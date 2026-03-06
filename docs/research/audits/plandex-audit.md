# Plandex Deep Audit

> Comprehensive analysis of Plandex AI coding agent implementation
> Audited: 2026-03-05
> Based on codebase at `docs/reference-code/plandex/`

---

## Overview

**Note: Plandex was shut down in October 2025. This audit is for historical reference.**

Plandex was a Go-based AI coding assistant distinguished by its **tell/build pipeline** — a two-phase approach where `tell` generates a plan (numbering subtasks with file lists) and `build` executes concurrently. Its core differentiator was **diff sandbox / review mode** — AI changes were stored server-side as pending `PlanFileResult` records and never touched the user's filesystem until explicitly applied. Plandex achieved **2M token effective context** through a model fallback chain (Claude Sonnet → Gemini 2.5 Pro → Gemini Pro 1.5) with smart context layering. It implemented **9 model roles** (planner, coder, namer, committer, summarizer, auto-continue, verifier, builder, wholeFile) with different models per role. The **concurrent build race pattern** applied edits via 4 parallel strategies: auto-apply, fast-apply, validation loop, and whole-file fallback.

---

## Key Capabilities

### Edit System

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Tell/Build Pipeline** | Planning phase + execution phase | `app/server/model/plan/tell.go`, `app/server/model/plan/build.go` |
| **Concurrent Build Race** | 4 strategies in parallel | `app/server/model/plan/build.go` |
| **Auto-Apply** | Direct structured edit application | `app/server/model/plan/build.go` |
| **Fast-Apply** | Quick edit for simple changes | `app/server/model/plan/build.go` |
| **Validation Loop** | Iterative fix with LLM | `app/server/model/plan/build.go` |
| **Whole-File Fallback** | Complete file replacement | `app/server/model/plan/build.go` |
| **Tree-Sitter Validation** | AST-aware edit verification | `app/server/syntax/` |
| **File Maps** | Structural summaries via tree-sitter | `app/server/syntax/file_map/map.go` |
| **Structured Edits** | AST node matching | `app/server/syntax/structured_edits_tree_sitter.go` |
| **Replacement-Based** | `PlanFileResult` with replacements | `app/shared/plan_result.go` |

### Context & Memory

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **2M Token Context** | Model fallback chain | `app/shared/ai_models_large_context.go` |
| **Model Fallback** | Claude → Gemini 2.5 Pro → Gemini Pro 1.5 | `app/shared/ai_models_large_context.go` |
| **Tree-Sitter Indexing** | 30+ language parsers | `app/server/syntax/parsers.go` |
| **File Maps** | Structural summaries | `app/server/syntax/file_map/map.go` |
| **Server-Side Context** | Assembled server-side, filtered per subtask | `app/server/model/plan/tell_context.go` |
| **Smart Filtering** | Context restricted to subtask files | `app/server/model/plan/tell_context.go` |
| **Conversation Summarization** | Gradual summaries at token thresholds | `app/server/model/plan/tell_summary.go` |
| **9 Model Roles** | Different models per role | `app/shared/ai_models_packs.go` |
| **PostgreSQL Storage** | Relational storage with git repos | `app/server/db/` |

### Agent Loop & Reliability

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Tell Phase** | Generates numbered subtasks with file lists | `app/server/model/plan/tell.go` |
| **Build Phase** | Iterates through subtasks with scoped context | `app/server/model/plan/build.go` |
| **Subtask Decomposition** | Regex-based parsing | `app/server/model/parse/subtasks.go` |
| **Concurrent File Builds** | Parallel execution per path | `app/server/model/plan/build.go` |
| **Race Pattern** | 4 strategies compete, first valid wins | `app/server/model/plan/build.go` |
| **Two-Phase Stage** | Context → Tasks → Implementation | `app/server/model/plan/tell_stage.go` |
| **Model Escalation** | Stronger model on failure | Error handling |
| **Distributed Locking** | PostgreSQL-backed repo locks | `app/server/db/locks.go` |

### Safety & Permissions

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Diff Sandbox** | Changes never touch disk until apply | `app/server/db/result_helpers.go` |
| **Server-Side Isolation** | All changes stored server-side | `app/server/db/fs.go` |
| **Client-Only Execution** | Server never runs user code | Architecture |
| **Apply/Reject/Diff Commands** | Explicit user approval | `app/cli/lib/apply.go` |
| **Git Versioning** | Per-plan git repositories | `app/server/db/git.go` |
| **Plan Branching** | Dual DB + git branch tracking | `app/server/db/branch_helpers.go` |
| **Auth & RBAC** | Token auth, org membership, permissions | `app/server/handlers/auth_helpers.go` |
| **Conflict Detection** | Detects overlapping changes | `app/server/db/diff_helpers.go` |

### UX & Developer Experience

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **TUI Interface** | Bubbletea-based | `app/cli/` |
| **Diff Sandbox / Review** | Review before apply | `app/cli/lib/apply.go` |
| **Plan Branching** | Version control for strategies | `app/cli/cmd/checkout.go`, `app/cli/cmd/branches.go` |
| **Model Packs** | 9 roles with 16 built-in packs | `app/shared/ai_models_packs.go` |
| **5 Autonomy Levels** | From manual to full-auto | `app/shared/plan_config.go` |
| **Browser Debug** | Debug integration | Various |
| **50+ CLI Commands** | Comprehensive CLI | `app/cli/cmd/` |

### Unique/Novel Features

| Feature | Description | File Path |
|---------|-------------|-----------|
| **Diff Sandbox / Review Pipeline** | Server-side changes, explicit apply | `app/server/db/result_helpers.go`, `app/cli/lib/apply.go` |
| **Tell/Build Pipeline** | Two-phase planning and execution | `app/server/model/plan/tell.go`, `app/server/model/plan/build.go` |
| **Concurrent Build Race** | 4 strategies compete | `app/server/model/plan/build.go` |
| **2M Token Context** | Model fallback chain | `app/shared/ai_models_large_context.go` |
| **9 Model Roles** | Different models per role | `app/shared/ai_models_packs.go` |
| **Plan Branching** | Git-backed strategy branches | `app/server/db/branch_helpers.go` |
| **Server-Client Architecture** | Server manages, client executes | Architecture |
| **Tree-Sitter File Maps** | Structural code summaries | `app/server/syntax/file_map/map.go` |
| **5 Autonomy Levels** | Configurable automation | `app/shared/plan_config.go` |

---

## Worth Stealing (for AVA)

### High Priority

1. **Diff Sandbox / Review Pipeline** (`app/server/db/result_helpers.go`, `app/cli/lib/apply.go`)
   - Server-side changes, explicit user approval
   - Perfect for safety-critical workflows

2. **Concurrent Build Race** (`app/server/model/plan/build.go`)
   - 4 strategies compete, first valid wins
   - Auto-apply → fast-apply → validation loop → whole-file

3. **Model Packs with 9 Roles** (`app/shared/ai_models_packs.go`)
   - Different models per role
   - Fallback chains per role

### Medium Priority

4. **2M Token Context Handling** (`app/shared/ai_models_large_context.go`)
   - Model fallback chain for large contexts
   - Smart context layering

5. **Tell/Build Pipeline** (`app/server/model/plan/tell.go`, `app/server/model/plan/build.go`)
   - Planning phase + execution phase
   - Subtask decomposition

6. **Plan Branching** (`app/server/db/branch_helpers.go`)
   - Git-backed strategy branches
   - Experimentation without risk

7. **Tree-Sitter File Maps** (`app/server/syntax/file_map/map.go`)
   - Structural code summaries
   - Better than simple file listing

### Lower Priority

8. **9 Model Roles** — May be overkill; start with 3-4
9. **PostgreSQL + Git** — Heavy infrastructure; AVA's SQLite is lighter
10. **5 Autonomy Levels** — Good but not critical

---

## AVA Already Has (or Matches)

| Plandex Feature | AVA Equivalent | Status |
|-----------------|----------------|--------|
| Concurrent builds | (Not implemented) | ❌ Gap |
| Diff sandbox | (Not implemented) | ❌ Gap |
| Model packs | Per-agent model selection | ⚠️ Should expand to roles |
| 2M context | Supports large contexts | ✅ Parity |
| Tree-sitter | Used in repo map | ✅ Parity |
| Subtask decomposition | Praxis hierarchy | ✅ Better |
| Tell/build | Plan mode | ✅ Similar |
| Git branching | Worktrees | ✅ Similar |

---

## Anti-Patterns to Avoid

1. **Server-Heavy Architecture** — Plandex required server; AVA should remain local-first
2. **PostgreSQL Dependency** — Heavy database; SQLite is sufficient
3. **Shutdown Risk** — Plandex shut down; avoid external dependencies
4. **Complex Infrastructure** — Distributed locking, multiple servers
5. **Go-Only** — Limited extension ecosystem

---

## Recent Additions (Pre-Shutdown)

Before shutdown in October 2025:

- **Enhanced Concurrent Builds** — Better race strategy
- **Improved File Maps** — Better tree-sitter integration
- **Model Pack Expansion** — More built-in packs
- **Browser Debug** — Better debugging integration

---

## File Reference Index

| File | Lines | Purpose |
|------|-------|---------|
| `app/server/model/plan/tell.go` | ~1,000 | Planning phase |
| `app/server/model/plan/build.go` | ~1,500 | Build/execution phase |
| `app/server/db/result_helpers.go` | ~400 | Diff sandbox |
| `app/shared/ai_models_large_context.go` | ~200 | 2M token handling |
| `app/shared/ai_models_packs.go` | ~300 | 9 model roles |
| `app/server/syntax/file_map/map.go` | ~400 | Tree-sitter file maps |
| `app/cli/lib/apply.go` | ~500 | Apply/reject commands |
| `app/server/db/git.go` | ~400 | Per-plan git repos |

---

*Audit generated by subagent analysis across 6 dimensions: Edit System, Context & Memory, Agent Loop, Safety, UX, and Unique Features.*

**Note: Plandex shut down in October 2025. Patterns are worth studying but the project is no longer maintained.**
