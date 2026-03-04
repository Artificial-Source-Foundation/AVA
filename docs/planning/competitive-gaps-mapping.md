# Competitive Gaps Mapping → Implementation Tasks

> Maps specific competitive advantages from 7 analyzed competitors to concrete AVA implementation tasks.
> This document is the sprint backlog for closing competitive gaps.

**Source**: `docs/research/backend-analysis/*-detailed.md` (7 competitors, 7000+ lines of analysis)
**Reference**: `docs/research/backend-analysis/COMPETITIVE-SYNTHESIS-QUALITY-OVER-QUANTITY.md`
**Date**: 2026-03-03

---

## Priority Legend

| Priority | Definition | Criteria |
|----------|-----------|----------|
| **P0** | Critical — lose users without it | Core edit/apply reliability, safety, context management |
| **P1** | Competitive parity — expected by power users | Planning, permissions, cost optimization |
| **P2** | Differentiation — nobody else has it well | Unique integration opportunities leveraging AVA strengths |

| Effort | Definition |
|--------|-----------|
| **S** | 1-3 days, single file/module |
| **M** | 3-7 days, 2-4 files/modules |
| **L** | 1-3 weeks, new module or major refactor |

---

## Table of Contents

1. [Edit Reliability (P0)](#1-edit-reliability-p0)
2. [Context Management (P0)](#2-context-management-p0)
3. [Safety & Sandboxing (P0)](#3-safety--sandboxing-p0)
4. [Code Understanding (P1)](#4-code-understanding-p1)
5. [Planning & Orchestration (P1)](#5-planning--orchestration-p1)
6. [Permissions & Trust (P1)](#6-permissions--trust-p1)
7. [Cost Optimization (P1)](#7-cost-optimization-p1)
8. [Parallel Execution (P1)](#8-parallel-execution-p1)
9. [Error Recovery & Self-Correction (P0)](#9-error-recovery--self-correction-p0)
10. [Agent Lifecycle (P2)](#10-agent-lifecycle-p2)
11. [Sprint Assignment Summary](#sprint-assignment-summary)
12. [Dependency Graph](#dependency-graph)

---

## 1. Edit Reliability (P0)

**Current state**: AVA has a single edit strategy in `tools/edit.ts` + `tools/edit-replacers.ts`. Edits are batch-applied after full LLM response. No fuzzy matching cascade, no streaming application.

**Why P0**: Edit reliability is the #1 determinant of user trust. A failed edit wastes the entire context window turn. Competitors achieve 85-90% success rates; AVA is estimated at ~70%.

### Task 1.1: Multi-Strategy Edit Cascade

**Source**: Gemini CLI (4-tier recovery), Aider (8-strategy fuzzy matching), OpenCode (9 cascading strategies)

**What to build**: Replace the single edit-replacer with a cascading strategy system that tries progressively fuzzier matching until one succeeds.

**Cascade order**:
1. Exact string match (current behavior)
2. Whitespace-normalized match (ignore leading/trailing whitespace, normalize indentation)
3. Comment-stripped match (ignore comment differences)
4. Levenshtein fuzzy match with configurable threshold (start at 0.85 similarity)
5. Line-anchor match (match first/last N lines, replace interior)
6. AST-aware match (tree-sitter: match by node type + position)
7. LLM self-correction (re-prompt with failure context + file content)

**Components touched**:
- `packages/core/src/tools/edit-replacers.ts` — Refactor into strategy pattern
- `packages/core/src/tools/edit.ts` — Wire cascade into edit execution
- `packages/core/src/tools/multiedit.ts` — Apply same cascade
- New: `packages/core/src/diff/strategies/` — Strategy implementations

**Effort**: L (1-2 weeks)
**Impact**: High — directly improves edit success rate from ~70% to target 90%
**Sprint**: 24-25

---

### Task 1.2: Streaming Diff Application

**Source**: Zed (Edit Agent streams edits as tokens arrive, fuzzy matcher with asymmetric costs)

**What to build**: Apply edits incrementally as the LLM streams tokens, rather than waiting for the complete response. Requires a streaming parser that detects complete edit blocks within partial responses.

**Design**:
- Parse tool call arguments incrementally (detect complete `old_text`/`new_text` pairs as they stream)
- Apply each hunk immediately when detected
- Use fuzzy matcher with asymmetric costs (substitution=2, indel=1) to prefer insertions/deletions over replacements
- Show real-time diff in UI as changes apply

**Components touched**:
- `packages/core/src/llm/` — Streaming response parser needs edit-block detection
- `packages/core/src/diff/` — New streaming diff applier
- `packages/core/src/tools/edit.ts` — Streaming execution path
- Frontend: `src/` — Real-time diff visualization

**Effort**: L (2-3 weeks)
**Impact**: High — reduces perceived latency from 3-5s to <0.5s per edit
**Sprint**: 25-26

---

### Task 1.3: Per-Hunk Review UI

**Source**: Zed (per-hunk accept/reject), Plandex (granular per-replacement review)

**What to build**: Instead of all-or-nothing edit approval, show each hunk independently with accept/reject controls. Integrates with Task 1.2 streaming for real-time hunks.

**Components touched**:
- Frontend: `src/` — Hunk-level diff viewer with accept/reject buttons
- `packages/core/src/diff/` — Hunk splitting and selective application
- `packages/core/src/tools/edit.ts` — Partial application support

**Effort**: M (5-7 days)
**Impact**: Medium — improves user trust and control over edits
**Sprint**: 26

---

### Task 1.4: Build Race (Concurrent Edit Strategies)

**Source**: Plandex (multiple edit strategies run concurrently, winner applies)

**What to build**: For complex edits, spawn 2-3 concurrent attempts using different strategies (e.g., SEARCH/REPLACE, unified diff, whole-file rewrite). First successful result wins. Especially valuable for large multi-file edits where one strategy may fail.

**Components touched**:
- `packages/core/src/diff/` — Race coordinator
- `packages/core/src/tools/edit.ts` — Race execution mode
- `packages/core/src/llm/` — Parallel LLM calls for different formats

**Effort**: M (5-7 days)
**Impact**: Medium — improves success rate for complex edits, higher cost per edit
**Sprint**: 28

---

### Task 1.5: Freeform Patch Format

**Source**: Codex CLI (30-50% token savings with freeform patch format, Lark grammar parser)

**What to build**: Support a compact patch format that uses `*** Begin Patch` / `*** End Patch` with minimal context lines and `[N lines omitted]` markers. Saves 30-50% tokens on edit tool calls compared to full SEARCH/REPLACE blocks.

**Components touched**:
- `packages/core/src/tools/apply-patch/` — New parser for freeform format
- `packages/core/src/diff/` — Format conversion utilities
- `packages/core/src/llm/` — System prompt to teach models the format

**Effort**: M (3-5 days)
**Impact**: Medium — significant token savings on edit-heavy tasks
**Sprint**: 27

---

## 2. Context Management (P0)

**Current state**: AVA has a single compaction strategy in `packages/core/src/context/`. No observation masking, no agent-initiated condensation, no adaptive strategy selection.

**Why P0**: Context window overflow is the #2 failure mode after edit failures. When context is lost, the agent loses coherence and makes contradictory decisions.

### Task 2.1: Multi-Strategy Condenser Framework

**Source**: OpenHands (9 condenser strategies), Codex CLI (handoff-style compaction)

**What to build**: Replace single compaction with a strategy registry that supports multiple condensation approaches, selectable per-situation.

**Strategies to implement**:
1. **RecentEvents** — Keep last N turns (current behavior, baseline)
2. **ObservationMasking** — Keep all actions/decisions, mask old tool outputs with `[output truncated — N lines]`
3. **StructuredSummary** — LLM summarizes completed subtasks into structured bullets
4. **AmortizedForgetting** — Gradually decay older content importance, keep high-signal items
5. **Handoff** — Codex-style: generate a "handoff document" that a fresh agent can continue from

**Components touched**:
- `packages/core/src/context/` — Refactor into strategy pattern with `CondenserRegistry`
- New: `packages/core/src/context/strategies/` — Individual strategy implementations
- `packages/core/src/agent/loop.ts` — Wire strategy selection into turn loop

**Effort**: L (1-2 weeks)
**Impact**: High — prevents context overflow, maintains agent coherence on long tasks
**Sprint**: 24-25

---

### Task 2.2: Observation Masking

**Source**: OpenHands (ObservationMaskingCondenser — keeps actions, masks old observations)

**What to build**: Specifically implement observation masking as the default condensation strategy. When context approaches limit, mask old tool outputs while preserving the action that triggered them and the agent's reasoning about the output.

**Design**:
- Track each message as either "action" (tool calls, decisions) or "observation" (tool outputs, file contents)
- When compacting, replace observations older than N turns with `[observation: read_file src/foo.ts — 245 lines, showed function definitions for X, Y, Z]`
- Preserve the most recent K observations in full
- Never mask the system prompt or user messages

**Components touched**:
- `packages/core/src/context/` — Message tagging (action vs observation)
- `packages/core/src/context/strategies/observation-masking.ts` — Implementation
- `packages/core/src/tools/` — Tool outputs need metadata for smart summarization

**Effort**: M (5-7 days)
**Impact**: High — preserves intent while dramatically reducing context size
**Sprint**: 25

---

### Task 2.3: Agent-Initiated Condensation

**Source**: OpenHands (agent can request context reduction), Gemini CLI (tail tool calls)

**What to build**: Allow the agent to explicitly request condensation when it detects context bloat. Add a `compact` tool or extend the existing compaction to be agent-invocable, not just automatic.

**Components touched**:
- `packages/core/src/tools/` — New `compact` tool definition
- `packages/core/src/context/` — On-demand condensation API
- `packages/core/src/agent/loop.ts` — Handle compact requests mid-turn

**Effort**: S (2-3 days)
**Impact**: Medium — agent can proactively manage its own context
**Sprint**: 25

---

### Task 2.4: Stuck Detection & Recovery

**Source**: OpenHands (5 stuck loop scenarios: action repetition, monologue, incomplete action, context overflow, empty response)

**What to build**: Detect when the agent is stuck in a loop and automatically intervene. Track last N actions and detect repetition patterns.

**Detection patterns**:
1. **Action repetition** — Same tool call with same args 3+ times
2. **Monologue** — Agent produces text without tool calls for 3+ turns
3. **Incomplete action** — Tool call started but not finished
4. **Context overflow** — Approaching token limit without progress
5. **Empty response** — LLM returns empty or near-empty response

**Recovery actions**: Force condensation, inject "you appear stuck" prompt, switch strategy, escalate to user

**Components touched**:
- `packages/core/src/agent/loop.ts` — Stuck detection in turn loop
- New: `packages/core/src/agent/stuck-detector.ts` — Pattern matching logic
- `packages/core/src/context/` — Emergency condensation path

**Effort**: M (3-5 days)
**Impact**: High — prevents wasted compute and user frustration on stuck agents
**Sprint**: 26

---

## 3. Safety & Sandboxing (P0)

**Current state**: AVA has no OS-level sandboxing. `tools/bash.ts` executes commands directly. `permissions/` has static rule matching. No terminal command security analysis.

**Why P0**: Safety is table-stakes for autonomous agents. Users won't run AVA in full-auto mode without sandboxing guarantees.

### Task 3.1: OS-Level Sandbox (Linux)

**Source**: Codex CLI (Landlock LSM + bubblewrap + seccomp on Linux)

**What to build**: Implement Linux sandboxing using Landlock (kernel 5.13+) for filesystem restrictions and optionally bubblewrap for namespace isolation. Commands executed via `bash` tool run inside the sandbox.

**Design**:
- **Landlock**: Restrict filesystem access to project directory + explicit allowlist (node_modules, /tmp, etc.)
- **bubblewrap fallback**: For kernels without Landlock, use bwrap for mount namespace isolation
- **seccomp**: Block dangerous syscalls (network bind, ptrace, etc.) in strict mode
- **Configuration**: Per-project sandbox config in `.ava/sandbox.json`

**Components touched**:
- `packages/platform-node/` — Sandbox implementation (platform-specific)
- `packages/core/src/tools/bash.ts` — Execute commands inside sandbox
- `packages/core/src/permissions/` — Sandbox policy configuration
- New: `packages/platform-node/src/sandbox/` — Linux sandbox module

**Effort**: L (2-3 weeks)
**Impact**: High — enables safe full-auto mode
**Sprint**: 26-27

---

### Task 3.2: Terminal Command Security Classifier

**Source**: Continue (1241-line terminal security parser, AST-based command analysis)

**What to build**: Before executing any bash command, parse and classify it for risk level. Use tree-sitter bash grammar to AST-parse commands and check against security rules.

**Risk levels**:
- **Safe**: read-only commands (ls, cat, grep, git status, npm test)
- **Moderate**: write commands within project (mkdir, cp, npm install)
- **Dangerous**: system-wide writes, network operations, package installs with scripts
- **Blocked**: rm -rf /, curl | bash, sudo, chmod 777, etc.

**Components touched**:
- New: `packages/core/src/permissions/terminal-classifier.ts` — Command parser + classifier
- `packages/core/src/tools/bash.ts` — Pre-execution security check
- `packages/core/src/codebase/` — Tree-sitter bash grammar integration
- `packages/core/src/permissions/` — Security rules registry

**Effort**: L (1-2 weeks)
**Impact**: High — granular command safety without user fatigue
**Sprint**: 27

---

### Task 3.3: Per-Action Security Risk Self-Declaration

**Source**: OpenHands (agent declares risk level of each action before execution)

**What to build**: Require the agent to self-declare the risk level of each tool call in its reasoning. Cross-reference against the terminal classifier (Task 3.2) for validation. Discrepancies trigger user approval.

**Components touched**:
- `packages/core/src/agent/loop.ts` — Risk declaration in tool call flow
- `packages/core/src/permissions/` — Risk validation logic
- `packages/core/src/tools/types.ts` — Risk level in tool call metadata

**Effort**: S (2-3 days)
**Impact**: Medium — adds defense-in-depth to security model
**Sprint**: 28

---

### Task 3.4: Network Proxy for Sandboxed Execution

**Source**: Codex CLI (all network goes through controlled proxy)

**What to build**: Route all network traffic from sandboxed commands through a local proxy that enforces allowlist/blocklist rules. Prevents data exfiltration and unauthorized API calls.

**Components touched**:
- New: `packages/platform-node/src/sandbox/network-proxy.ts` — HTTP/HTTPS proxy
- `packages/core/src/permissions/` — Network allowlist configuration
- `packages/core/src/tools/bash.ts` — Proxy environment injection

**Effort**: M (5-7 days)
**Impact**: Medium — prevents data exfiltration in autonomous mode
**Sprint**: 29

---

## 4. Code Understanding (P1)

**Current state**: AVA has tree-sitter based repo mapping in `packages/core/src/codebase/` but no dependency graph analysis or PageRank-based ranking.

**Why P1**: Better context selection = fewer tokens wasted = higher quality responses. This is the difference between sending 100K of relevant code vs 1M of irrelevant code.

### Task 4.1: PageRank Repo Map

**Source**: Aider (RepoMap with PageRank, dependency graph, weight tuning: Defs=3.0, Declarations=2.0, Identifiers=0.5, Keywords=0.1)

**What to build**: Build a dependency graph from tree-sitter symbol extraction, then run PageRank to rank files/symbols by relevance to the current task. Use ranked results to select which files to include in context.

**Design**:
- Extract symbols (functions, classes, imports) via tree-sitter → nodes
- Build edges from import/require/usage relationships → directed graph
- Run PageRank with weighted edges (definitions weight more than references)
- Given a user query, identify seed nodes (mentioned files/symbols), compute personalized PageRank from those seeds
- Return top-N ranked symbols as context

**Components touched**:
- `packages/core/src/codebase/` — Dependency graph builder, PageRank implementation
- `packages/core/src/context/` — Use PageRank rankings for context selection
- `packages/core/src/tools/read.ts` — PageRank-aware file reading

**Effort**: L (1-2 weeks)
**Impact**: High — 30% fewer tokens, 40% higher success rate (Aider's measured improvement)
**Sprint**: 25-26

---

### Task 4.2: AST-Aware Grep

**Source**: Zed (tree-sitter powered search that understands code structure)

**What to build**: Extend grep/codesearch to understand code structure. Search for "function definitions named X" vs "all references to X" vs "class Y methods". Uses tree-sitter queries.

**Components touched**:
- `packages/core/src/tools/grep.ts` — AST-aware search mode
- `packages/core/src/tools/codesearch.ts` — Structural search queries
- `packages/core/src/codebase/` — Tree-sitter query builder

**Effort**: M (5-7 days)
**Impact**: Medium — more precise code search reduces irrelevant results
**Sprint**: 28

---

### Task 4.3: JIT Context Discovery

**Source**: Gemini CLI (just-in-time context — discovers needed files during execution rather than upfront)

**What to build**: Instead of loading all potentially relevant files before the first LLM call, discover and load files on-demand as the agent works. Track which files the agent references and preload likely-needed files.

**Components touched**:
- `packages/core/src/context/` — Lazy context loading
- `packages/core/src/codebase/` — Predictive file loading based on dependency graph
- `packages/core/src/agent/loop.ts` — JIT context injection between turns

**Effort**: M (5-7 days)
**Impact**: Medium — reduces initial context bloat, especially on large repos
**Sprint**: 29

---

## 5. Planning & Orchestration (P1)

**Current state**: AVA has plan mode (`agent/modes/plan.ts`) and commander delegation but no explicit architect phase, no auto-context gathering before planning, no multi-stage pipeline.

**Why P1**: Complex multi-file tasks need structured planning. Without it, the agent makes locally optimal but globally suboptimal decisions.

### Task 5.1: Explicit Architect Phase

**Source**: Plandex (Context → Tasks → Implement pipeline), Aider (architect + editor model separation)

**What to build**: Before coding, run an "architect" phase that: (1) analyzes the codebase structure relevant to the task, (2) creates a structured plan with file-level changes, (3) estimates complexity per subtask, (4) hands off to the coder phase.

**Design**:
- Architect uses a thinking/reasoning model (cheaper, better at planning)
- Output: structured JSON plan with `{file, action, description, dependencies, complexity}`
- Coder receives the plan and executes file-by-file
- Plan is visible to user for review before execution begins

**Components touched**:
- `packages/core/src/agent/modes/plan.ts` — Enhance plan mode with architect output
- `packages/core/src/commander/` — Architect as first delegation step
- New: `packages/core/src/agent/architect.ts` — Architect phase logic
- `packages/core/src/llm/` — Model routing (use cheaper model for planning)

**Effort**: L (1-2 weeks)
**Impact**: High — coherent multi-file changes, reduced rework
**Sprint**: 27-28

---

### Task 5.2: Auto-Context Architect

**Source**: Plandex (automatic context gathering phase before planning)

**What to build**: Before the architect phase, automatically gather relevant context: run grep/codesearch for related code, read dependency files, check test files, read docs. Feed this into the architect prompt.

**Components touched**:
- New: `packages/core/src/agent/auto-context.ts` — Context gathering heuristics
- `packages/core/src/codebase/` — Relevance scoring for context items
- `packages/core/src/agent/architect.ts` — Consume auto-gathered context

**Effort**: M (5-7 days)
**Impact**: Medium — architect makes better plans with more context
**Sprint**: 28

---

### Task 5.3: Validation Loop with Model Escalation

**Source**: Plandex (validation loop with model escalation), Aider (reflection loop: lint → test → retry)

**What to build**: After each edit, run automated validation (lint, typecheck, test). On failure, retry with error context. If retries exhaust with the current model, escalate to a more capable (expensive) model.

**Design**:
- Validation steps: tree-sitter parse → lint → typecheck → test (configurable)
- Retry budget: 3 attempts at current model, then escalate
- Escalation chain: fast model → standard model → premium model
- Track validation results for learning (which patterns fail)

**Components touched**:
- `packages/core/src/validator/` — Validation pipeline with retry
- `packages/core/src/llm/` — Model escalation logic
- `packages/core/src/agent/loop.ts` — Post-edit validation hook
- `packages/core/src/models/` — Escalation chain configuration

**Effort**: M (5-7 days)
**Impact**: High — catches errors immediately, prevents cascading failures
**Sprint**: 27

---

## 6. Permissions & Trust (P1)

**Current state**: AVA has `packages/core/src/permissions/` with static rule matching and `packages/core/src/policy/`. Rules don't adapt based on tool arguments or runtime context.

**Why P1**: Static permissions cause either over-permissioning (security risk) or under-permissioning (user fatigue from constant approval prompts).

### Task 6.1: Dynamic Permission Escalation

**Source**: Continue (evaluateToolCallPolicy — checks tool arguments, not just tool name)

**What to build**: Evaluate permissions based on the actual arguments of each tool call, not just the tool name. `bash("ls")` should auto-approve while `bash("rm -rf /")` should block.

**Design**:
- Each tool defines argument-level policies: `{ arg: "command", patterns: [{ match: /^(ls|cat|git status)/, allow: true }] }`
- Policy evaluator inspects args before execution
- Unknown patterns default to "ask user"
- Learning mode: track user decisions to suggest new rules

**Components touched**:
- `packages/core/src/permissions/` — Argument-aware policy evaluator
- `packages/core/src/policy/` — Policy definition format
- `packages/core/src/tools/bash.ts` — Argument-level policy hints
- `packages/core/src/tools/edit.ts` — File-path-based escalation

**Effort**: M (5-7 days)
**Impact**: High — reduces approval fatigue while maintaining security
**Sprint**: 28

---

### Task 6.2: Four-Tier Rules System

**Source**: Continue (Always, Auto Attached, Agent Requested, Manual)

**What to build**: Replace binary allow/deny with a 4-tier system:
1. **Always** — Always applied, user cannot disable (security-critical rules)
2. **Auto** — Applied automatically based on context (file patterns, project type)
3. **Suggested** — Agent can request, user sees but can dismiss
4. **Manual** — User must explicitly invoke

**Components touched**:
- `packages/core/src/permissions/` — Tier-based rule engine
- `packages/core/src/instructions/` — Rule attachment to project context
- `packages/core/src/config/` — Rule tier configuration

**Effort**: M (3-5 days)
**Impact**: Medium — more nuanced permission model
**Sprint**: 29

---

### Task 6.3: preprocessArgs for Diff Preview

**Source**: Continue (pre-computes edits to show diff preview before user approval)

**What to build**: Before requesting user approval for an edit, compute and display the actual diff that will be applied. User sees exactly what will change, not just the tool call arguments.

**Components touched**:
- `packages/core/src/tools/edit.ts` — Diff preview computation
- `packages/core/src/permissions/` — Preview-enhanced approval flow
- Frontend: `src/` — Diff preview in approval dialog

**Effort**: S (2-3 days)
**Impact**: Medium — dramatically improves user confidence in approvals
**Sprint**: 29

---

## 7. Cost Optimization (P1)

**Current state**: AVA uses a single model for all operations. No cache warming, no model role separation, no dual output system.

**Why P1**: Cost directly affects adoption. Users running long sessions on expensive models will churn.

### Task 7.1: Multi-Model Role Architecture

**Source**: Plandex (9 model roles), Aider (3-model: architect + editor + referee)

**What to build**: Assign different models to different roles based on task complexity and cost requirements.

**Roles**:
1. **Planner** — Cheap reasoning model for task decomposition ($)
2. **Coder** — Primary model for code generation ($$)
3. **Editor** — Fast model for SEARCH/REPLACE application ($)
4. **Reviewer** — Quality model for code review ($$)
5. **Committer** — Cheapest model for commit messages ($)
6. **Summarizer** — Cheap model for context condensation ($)

**Components touched**:
- `packages/core/src/models/` — Role-based model registry
- `packages/core/src/llm/` — Model routing per role
- `packages/core/src/config/` — Role-to-model mapping configuration
- `packages/core/src/commander/` — Role assignment to workers

**Effort**: M (5-7 days)
**Impact**: High — 40-60% cost reduction for typical sessions
**Sprint**: 28

---

### Task 7.2: Prompt Cache Warming

**Source**: Aider (sends common prefix to prime provider cache, reuses across turns)

**What to build**: Structure prompts so that the system prompt + common context forms a stable prefix that LLM providers can cache. Avoid changing early tokens between turns.

**Design**:
- Order messages: system prompt → project context → repo map → conversation (stable → volatile)
- Use cache control headers where providers support them (Anthropic cache_control, etc.)
- Track cache hit rates per provider
- Pre-warm cache on session start with system prompt

**Components touched**:
- `packages/core/src/llm/` — Cache-aware prompt construction
- `packages/core/src/context/` — Stable prefix ordering
- `packages/core/src/models/` — Provider cache capability detection

**Effort**: S (2-3 days)
**Impact**: Medium — 20-30% cost reduction from cache hits
**Sprint**: 27

---

### Task 7.3: Dual Output System

**Source**: Gemini CLI (llmContent vs returnDisplay — different content for LLM context vs user display)

**What to build**: Tool outputs return two versions: a compact version for the LLM context (saves tokens) and a rich version for user display (full detail). Currently, the same output goes to both.

**Design**:
- Tool results return `{ llmContent: string, displayContent: string }`
- `llmContent`: Compact, relevant facts only (e.g., "File created successfully at path/file.ts, 45 lines")
- `displayContent`: Full output with formatting (e.g., syntax-highlighted file content)
- Context system uses `llmContent`; frontend renders `displayContent`

**Components touched**:
- `packages/core/src/tools/types.ts` — Dual output type definition
- `packages/core/src/tools/*.ts` — Each tool returns both versions
- `packages/core/src/context/` — Use llmContent for context
- Frontend: `src/` — Render displayContent

**Effort**: M (5-7 days)
**Impact**: Medium — saves 20-40% tokens on tool-heavy conversations
**Sprint**: 29

---

## 8. Parallel Execution (P1)

**Current state**: AVA has `tools/task-parallel.ts` and `tools/batch.ts` but no RwLock-based concurrent tool execution or read-only parallel optimization.

**Why P1**: Parallel execution reduces latency for multi-file operations significantly. Read-only tools (grep, glob, read) should never block each other.

### Task 8.1: RwLock-Based Tool Execution

**Source**: Codex CLI (RwLock: multiple readers, single writer, read-only tools execute in parallel)

**What to build**: Classify tools as read-only or read-write. Read-only tools acquire shared locks and execute concurrently. Write tools acquire exclusive locks and execute sequentially.

**Tool classification**:
- **Read-only**: read_file, grep, glob, ls, codesearch, git status, todoread
- **Read-write**: edit, write, create, delete, bash, multiedit, apply_patch

**Components touched**:
- `packages/core/src/tools/locks.ts` — RwLock implementation (already exists, extend)
- `packages/core/src/tools/types.ts` — Tool read/write classification
- `packages/core/src/agent/loop.ts` — Parallel read-only execution in batch tool calls
- `packages/core/src/tools/batch.ts` — Lock-aware batch execution

**Effort**: M (3-5 days)
**Impact**: Medium — 2-3x speedup for read-heavy exploration phases
**Sprint**: 27

---

### Task 8.2: Parallel Read-Only Execution in Batch

**Source**: Gemini CLI (parallel read-only execution for file discovery)

**What to build**: When the agent issues multiple read-only tool calls in a single turn (common during exploration), execute them all concurrently rather than sequentially.

**Components touched**:
- `packages/core/src/tools/batch.ts` — Detect all-read-only batches and parallelize
- `packages/core/src/tools/task-parallel.ts` — Ensure parallel tasks respect read-only classification

**Effort**: S (2-3 days)
**Impact**: Medium — faster exploration, especially on large codebases
**Sprint**: 27

---

## 9. Error Recovery & Self-Correction (P0)

**Current state**: AVA has basic retry in the agent loop but no structured error recovery, no LLM self-correction, no failure analysis.

**Why P0**: Error recovery is what separates a demo from a production tool. When things fail (and they always do), graceful recovery is critical.

### Task 9.1: LLM Self-Correction on Edit Failure

**Source**: Gemini CLI (LLM analyzes edit failure and generates corrected edit)

**What to build**: When an edit fails all cascade strategies (Task 1.1), send the failure context (original file, attempted edit, error message) back to the LLM with a self-correction prompt asking it to analyze what went wrong and generate a corrected edit.

**Components touched**:
- `packages/core/src/agent/loop.ts` — Self-correction flow on tool failure
- New: `packages/core/src/agent/self-correction.ts` — Self-correction prompts and logic
- `packages/core/src/tools/edit.ts` — Expose failure details for self-correction

**Effort**: M (3-5 days)
**Impact**: High — recovers from the ~15% of edits that all strategies miss
**Sprint**: 25

---

### Task 9.2: Reflection Loop (Lint → Test → Retry)

**Source**: Aider (automated lint + test after every edit, retry with error context on failure)

**What to build**: After every edit or batch of edits, automatically run: (1) tree-sitter parse check, (2) linter, (3) typecheck, (4) relevant tests. On failure, feed error output back to the LLM for fix.

**Design**:
- Configure per-project: `{ lint: "npm run lint", typecheck: "npx tsc --noEmit", test: "npm test" }`
- Run in order, stop on first failure
- Include error output + the edit that caused it in retry prompt
- Budget: max 3 retries per edit

**Components touched**:
- `packages/core/src/validator/` — Automated validation pipeline
- `packages/core/src/agent/loop.ts` — Post-edit validation hook
- `packages/core/src/config/` — Per-project validation commands
- `packages/core/src/hooks/` — Validation lifecycle hooks

**Effort**: M (5-7 days)
**Impact**: High — catches regressions immediately
**Sprint**: 26

---

### Task 9.3: Modifiable Tool Definitions

**Source**: Gemini CLI (tools whose schemas can be modified at runtime based on context)

**What to build**: Allow tool schemas to adapt based on runtime context. For example, the `edit` tool could expose different parameters based on the current model's capabilities, or the `bash` tool could restrict available commands based on sandbox mode.

**Components touched**:
- `packages/core/src/tools/define.ts` — Dynamic schema modification
- `packages/core/src/tools/registry.ts` — Runtime schema updates
- `packages/core/src/tools/types.ts` — Modifiable schema type

**Effort**: S (2-3 days)
**Impact**: Low — enables model-adaptive tool behavior
**Sprint**: 30

---

## 10. Agent Lifecycle (P2)

**Current state**: AVA has commander delegation and session forking. No depth limits, no ghost checkpoints, no two-phase memory extraction.

**Why P2**: These are power-user features that enhance the multi-agent experience but aren't critical for core functionality.

### Task 10.1: Hierarchical Depth Limits

**Source**: Codex CLI (Root → level1 → level2, configs inherit but can override)

**What to build**: Enforce depth limits on agent delegation. Root agent can delegate to workers, workers can delegate to sub-workers, but only up to a configurable depth. Prevents runaway delegation chains.

**Components touched**:
- `packages/core/src/commander/` — Depth tracking and limits
- `packages/core/src/config/` — Max depth configuration

**Effort**: S (1-2 days)
**Impact**: Low — safety net for multi-agent scenarios
**Sprint**: 30

---

### Task 10.2: Ghost Checkpoints

**Source**: Codex CLI (invisible rollback points every turn, invisible to the agent)

**What to build**: Automatically create lightweight filesystem snapshots at the start of each turn. If the agent makes a mistake, roll back to any previous checkpoint. Invisible to the agent — it doesn't know checkpoints exist.

**Components touched**:
- `packages/core/src/git/` — Lightweight checkpoint creation (git stash or worktree-based)
- `packages/core/src/agent/loop.ts` — Auto-checkpoint at turn boundaries
- `packages/core/src/session/` — Checkpoint metadata storage

**Effort**: M (3-5 days)
**Impact**: Medium — safety net for destructive operations
**Sprint**: 30

---

### Task 10.3: Two-Phase Memory Extraction

**Source**: Codex CLI (extract learnings from session: phase 1 = identify, phase 2 = synthesize)

**What to build**: At session end, run two LLM passes to extract learnings: (1) Identify interesting patterns, decisions, and outcomes from the session. (2) Synthesize into structured memory entries for long-term storage.

**Components touched**:
- `packages/core/src/session/` — Post-session extraction hook
- New: `packages/core/src/memory/` directory (if not exists) — Long-term memory storage
- `packages/core/src/llm/` — Two-phase extraction prompts

**Effort**: M (3-5 days)
**Impact**: Medium — agents learn from past sessions
**Sprint**: 31

---

### Task 10.4: Model-Adaptive Edit Tool Selection

**Source**: Continue (model-adaptive edit tools — different edit format based on model capabilities)

**What to build**: Automatically select the best edit format based on the current model's known strengths. Some models handle SEARCH/REPLACE well; others are better with unified diffs; others work best with whole-file rewrites.

**Design**:
- Model capability registry: `{ "claude-sonnet": { bestEditFormat: "search_replace", supportsStreaming: true } }`
- Tool selection happens transparently — agent doesn't need to choose
- Track success rates per model per format for continuous optimization

**Components touched**:
- `packages/core/src/models/` — Model capability registry
- `packages/core/src/tools/edit.ts` — Format selection logic
- `packages/core/src/diff/` — Multiple format implementations

**Effort**: M (3-5 days)
**Impact**: Medium — each model uses its optimal edit format
**Sprint**: 31

---

## Sprint Assignment Summary

### Sprint 24-25: Foundation (Edit Reliability + Context)

| # | Task | Priority | Effort | Impact |
|---|------|----------|--------|--------|
| 1.1 | Multi-Strategy Edit Cascade | P0 | L | High |
| 2.1 | Multi-Strategy Condenser Framework | P0 | L | High |
| 2.2 | Observation Masking | P0 | M | High |
| 2.3 | Agent-Initiated Condensation | P0 | S | Medium |
| 4.1 | PageRank Repo Map | P1 | L | High |
| 9.1 | LLM Self-Correction on Edit Failure | P0 | M | High |

**Sprint theme**: Make the core loop reliable — edits succeed, context is managed, failures recover.
**Total effort**: ~4-5 weeks

---

### Sprint 26-27: Safety + Validation

| # | Task | Priority | Effort | Impact |
|---|------|----------|--------|--------|
| 1.2 | Streaming Diff Application | P0 | L | High |
| 1.3 | Per-Hunk Review UI | P0 | M | Medium |
| 2.4 | Stuck Detection & Recovery | P0 | M | High |
| 3.1 | OS-Level Sandbox (Linux) | P0 | L | High |
| 3.2 | Terminal Command Security Classifier | P0 | L | High |
| 5.3 | Validation Loop with Model Escalation | P1 | M | High |
| 7.2 | Prompt Cache Warming | P1 | S | Medium |
| 8.1 | RwLock-Based Tool Execution | P1 | M | Medium |
| 8.2 | Parallel Read-Only Execution | P1 | S | Medium |
| 9.2 | Reflection Loop (Lint → Test → Retry) | P0 | M | High |
| 1.5 | Freeform Patch Format | P1 | M | Medium |

**Sprint theme**: Safety guarantees + streaming UX + automated validation.
**Total effort**: ~6-7 weeks

---

### Sprint 28-29: Intelligence + Optimization

| # | Task | Priority | Effort | Impact |
|---|------|----------|--------|--------|
| 1.4 | Build Race (Concurrent Edits) | P1 | M | Medium |
| 3.3 | Per-Action Security Risk Self-Declaration | P0 | S | Medium |
| 3.4 | Network Proxy for Sandbox | P1 | M | Medium |
| 4.2 | AST-Aware Grep | P1 | M | Medium |
| 4.3 | JIT Context Discovery | P1 | M | Medium |
| 5.1 | Explicit Architect Phase | P1 | L | High |
| 5.2 | Auto-Context Architect | P1 | M | Medium |
| 6.1 | Dynamic Permission Escalation | P1 | M | High |
| 6.2 | Four-Tier Rules System | P1 | M | Medium |
| 6.3 | preprocessArgs for Diff Preview | P1 | S | Medium |
| 7.1 | Multi-Model Role Architecture | P1 | M | High |
| 7.3 | Dual Output System | P1 | M | Medium |

**Sprint theme**: Intelligent planning, adaptive permissions, cost optimization.
**Total effort**: ~7-8 weeks

---

### Sprint 30-31: Polish + Differentiation

| # | Task | Priority | Effort | Impact |
|---|------|----------|--------|--------|
| 9.3 | Modifiable Tool Definitions | P1 | S | Low |
| 10.1 | Hierarchical Depth Limits | P2 | S | Low |
| 10.2 | Ghost Checkpoints | P2 | M | Medium |
| 10.3 | Two-Phase Memory Extraction | P2 | M | Medium |
| 10.4 | Model-Adaptive Edit Tool Selection | P2 | M | Medium |

**Sprint theme**: Agent lifecycle improvements and learning.
**Total effort**: ~2-3 weeks

---

## Dependency Graph

```
Task 1.1 (Edit Cascade) ──────────────→ Task 9.1 (Self-Correction) [cascade exhaustion triggers self-correction]
Task 1.1 (Edit Cascade) ──────────────→ Task 1.2 (Streaming Diff) [cascade strategies needed for streaming]
Task 1.2 (Streaming Diff) ────────────→ Task 1.3 (Per-Hunk Review) [streaming produces hunks for review]
Task 1.1 (Edit Cascade) ──────────────→ Task 1.4 (Build Race) [race uses different cascade strategies]
Task 2.1 (Condenser Framework) ───────→ Task 2.2 (Observation Masking) [framework provides strategy interface]
Task 2.1 (Condenser Framework) ───────→ Task 2.3 (Agent Condensation) [framework provides compact API]
Task 2.1 (Condenser Framework) ───────→ Task 2.4 (Stuck Detection) [stuck triggers emergency condensation]
Task 3.1 (OS Sandbox) ────────────────→ Task 3.4 (Network Proxy) [proxy runs inside sandbox]
Task 3.2 (Terminal Classifier) ───────→ Task 3.3 (Risk Self-Declaration) [classifier validates self-declared risk]
Task 3.2 (Terminal Classifier) ───────→ Task 6.1 (Dynamic Permissions) [classifier feeds permission decisions]
Task 4.1 (PageRank) ──────────────────→ Task 4.3 (JIT Context) [PageRank rankings guide JIT loading]
Task 4.1 (PageRank) ──────────────────→ Task 5.2 (Auto-Context) [PageRank selects auto-context files]
Task 5.1 (Architect Phase) ───────────→ Task 5.2 (Auto-Context) [auto-context feeds architect]
Task 5.3 (Validation Loop) ───────────→ Task 9.2 (Reflection Loop) [validation pipeline reused by reflection]
Task 7.1 (Multi-Model Roles) ─────────→ Task 5.1 (Architect Phase) [architect uses planner model]
Task 7.1 (Multi-Model Roles) ─────────→ Task 5.3 (Validation + Escalation) [escalation needs model roles]
Task 8.1 (RwLock) ────────────────────→ Task 8.2 (Parallel Read-Only) [RwLock enables parallel reads]
Task 10.4 (Model-Adaptive Edit) ──────→ Task 1.1 (Edit Cascade) [model determines starting strategy]
```

### Critical Path

The critical path for maximum impact is:

```
Sprint 24-25:  1.1 (Edit Cascade) → 9.1 (Self-Correction) → 2.1 (Condenser Framework) → 2.2 (Observation Masking)
Sprint 26-27:  1.2 (Streaming Diff) → 9.2 (Reflection Loop) → 3.1 (OS Sandbox) → 3.2 (Terminal Classifier)
Sprint 28-29:  5.1 (Architect Phase) → 6.1 (Dynamic Permissions) → 7.1 (Multi-Model Roles)
```

These 12 tasks on the critical path deliver ~80% of the competitive gap closure.

---

## Metrics & Success Criteria

| Metric | Current (est.) | After Sprint 25 | After Sprint 27 | After Sprint 29 | Target |
|--------|---------------|-----------------|-----------------|-----------------|--------|
| Edit success rate | 70% | 85% | 90% | 92% | 90%+ |
| Edit recovery rate | 40% | 70% | 80% | 85% | 85%+ |
| Context efficiency | 60% | 75% | 80% | 85% | 85%+ |
| Edit latency | 3-5s | 3-5s | <1s (streaming) | <0.5s | <0.5s |
| Autonomous safety | Low | Low | High (sandbox) | High | High |
| Cost per session | Baseline | Baseline | -20% (cache) | -40% (roles) | -40%+ |

---

## Summary: 30 Tasks, Prioritized

| Priority | Count | Effort Range | Key Deliverables |
|----------|-------|-------------|------------------|
| **P0** | 12 tasks | S-L | Edit cascade, condensers, sandbox, self-correction, stuck detection, streaming diff, reflection loop |
| **P1** | 14 tasks | S-L | PageRank, architect phase, dynamic permissions, multi-model roles, cache warming, parallel execution |
| **P2** | 4 tasks | S-M | Depth limits, ghost checkpoints, memory extraction, model-adaptive edits |

**Total estimated effort**: ~20-24 weeks across Sprints 24-31
**Critical path**: 12 tasks over Sprints 24-29 (~16 weeks) deliver 80% of competitive gap closure
