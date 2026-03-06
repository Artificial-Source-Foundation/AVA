# AVA SOTA Gap Analysis - March 2026

> State-of-the-art analysis of where AVA leads, matches, or trails competitors  
> Based on deep audit of 12 AI coding agent codebases  
> Date: 2026-03-05

---

## Executive Summary

This analysis is based on a comprehensive audit of 12 competitor AI coding agents: **Aider, Cline, Codex CLI, Continue, Gemini CLI, Goose, OpenCode, OpenHands, Pi Mono, Plandex, SWE Agent, and Zed**. The audit examined 6 dimensions per competitor: Edit System, Context & Memory, Agent Loop, Safety, UX, and Unique Features.

**Key Finding**: AVA is competitive in most areas but has significant gaps in **streaming diff application**, **per-hunk review UI**, **PageRank repo mapping**, and **sophisticated loop detection**. The most urgent gaps to close are in the **Edit System** and **Context Management** dimensions.

---

## 1. Features Where AVA LEADS

These are capabilities AVA has that few or no competitors match:

### 1.1 Comprehensive Tool Suite (~41 tools)

**Competitive Position**: AVA has the most comprehensive tool suite of any competitor.

| Tool Category | AVA Count | Nearest Competitor |
|---------------|-----------|-------------------|
| File Operations | 7 | Cline (~15) |
| Search & Navigation | 6 | OpenCode (~10) |
| Shell & Execution | 5 | Cline (~5) |
| Git & Version Control | 6 | Cline (~2) |
| Agent & Planning | 6 | OpenCode (~4) |
| Context & Memory | 4 | Gemini CLI (~2) |
| LSP Integration | **9** | Cline/Continue (0 as discrete tools) |
| Protocols & Interop | 3 | Cline (~2) |
| **TOTAL** | **~41** | Cline (~25) |

**Why it matters**: AVA's LSP integration (9 tools) is unique — no other standalone tool exposes LSP as discrete agent tools. This enables sophisticated code understanding and refactoring.

### 1.2 Richest Extension API

**Competitive Position**: Most complete extension surface.

| Extension Capability | AVA | Competitors |
|---------------------|-----|-------------|
| registerTool | ✅ | OpenCode, Goose, Continue |
| registerProvider | ✅ | Unique (non-IDE tools) |
| registerAgentMode | ✅ | Unique |
| registerValidator | ✅ | Unique |
| addToolMiddleware | ✅ | Unique (with priority) |
| registerHook / callHook | ✅ | OpenCode |
| registerCommand | ✅ | Zed |
| Per-extension storage | ✅ | Unique |
| Plugin scaffold CLI | ✅ | Unique |

**Why it matters**: AVA's middleware priority system and validators are unique capabilities that enable fine-grained control over tool execution.

### 1.3 Most Structured Multi-Agent System

**Competitive Position**: Only tool with hierarchical delegation.

| Agent Architecture | AVA | Competitors |
|-------------------|-----|-------------|
| **3-tier hierarchy** | **✅ Commander → Leads → Workers** | ❌ All others flat |
| Built-in agents | 13 | OpenCode (7), Cline (subagents) |
| Domain specialization | ✅ Frontend/Backend/Tester/etc | ❌ Generic |
| Typed delegation interfaces | ✅ 6 `delegate_*` tools | ❌ Generic spawn |

**Why it matters**: AVA's Praxis hierarchy enables sophisticated task decomposition with domain expertise at each level.

### 1.4 Session Recall Across Conversations

**Competitive Position**: Only tool with cross-session search.

| Capability | AVA | Competitors |
|------------|-----|-------------|
| FTS5 full-text search | ✅ | ❌ Unique to AVA |
| Cross-session recall | ✅ | ❌ Unique to AVA |
| Session branching | ✅ | OpenCode, Pi Mono |

**Why it matters**: Users can search across all past sessions — no competitor offers this.

### 1.5 Unified Hooks + Middleware Architecture

**Competitive Position**: Unique dual system.

| System | AVA | Competitors |
|--------|-----|-------------|
| Hooks (sequential chaining) | ✅ | OpenCode, Gemini CLI |
| Middleware (priority-based) | ✅ | ❌ Unique combination |
| Dual system | ✅ | ❌ Unique |

**Why it matters**: AVA combines cross-cutting concerns (permissions) with extension-specific logic in a unique dual architecture.

---

## 2. Features Where AVA MATCHES

These are capabilities where AVA has competitive parity:

### 2.1 Edit System

| Capability | AVA | Best Competitor | Status |
|------------|-----|-----------------|--------|
| Multiple strategies | 8 | Aider (10) | ✅ Parity |
| Fuzzy matching | ✅ | Aider, Cline, Gemini, Zed | ✅ Parity |
| Self-correction | ✅ | Aider, Cline, Gemini, OpenCode | ✅ Parity |
| Streaming | ✅ | Cline, Continue, Zed | ✅ Parity |
| Indentation handling | ✅ | Aider (RelativeIndenter) | ✅ Parity |
| Unicode normalization | ✅ | Pi Mono (NFC/NFD) | ✅ Parity |

### 2.2 Context Management

| Capability | AVA | Best Competitor | Status |
|------------|-----|-----------------|--------|
| Token tracking | ✅ | All | ✅ Parity |
| Compaction | ✅ | Aider, OpenHands, Gemini | ✅ Parity |
| Session management | ✅ DAG | OpenCode, Pi Mono (tree) | ✅ Parity |
| Persistence | ✅ | All | ✅ Parity |

### 2.3 Safety & Permissions

| Capability | AVA | Best Competitor | Status |
|------------|-----|-----------------|--------|
| Middleware pipeline | ✅ | Goose (3-layer) | ✅ Parity |
| Command filtering | ✅ | All | ✅ Parity |
| Git checkpoints | ✅ | Cline (shadow), Codex (ghost) | ✅ Parity |
| Docker sandbox | ✅ optional | OpenHands (default) | ⚠️ Partial |

### 2.4 Multi-Agent / Delegation

| Capability | AVA | Best Competitor | Status |
|------------|-----|-----------------|--------|
| Subagent spawning | ✅ | Cline, Codex, OpenHands | ✅ Parity |
| Hierarchy | ✅ 3-tier | ❌ None | ✅ **Better** |
| Domain roles | ✅ | ❌ None | ✅ **Better** |

---

## 3. Features Where AVA TRAILS

These are concrete gaps with specific competitor implementations to reference:

### 3.1 High Priority Gaps (Critical)

#### 3.1.1 StreamingDiff — Zed

**Gap**: AVA applies edits after full generation; Zed applies AS LLM streams.

**Reference Implementation**: `crates/streaming_diff/src/streaming_diff.rs`

**What it does**:
- Character-level incremental diffing
- Scoring heuristics: INSERTION=-1, DELETION=-20, EQUALITY_BASE=1.8^run
- Applies edits as tokens arrive, not after completion

**Impact**: Reduces perceived latency, enables real-time preview.

**Effort**: Large (new algorithm + UI integration)

#### 3.1.2 Per-Hunk Accept/Reject — Zed

**Gap**: AVA shows all-or-nothing diff; Zed allows per-hunk review.

**Reference Implementation**: `crates/agent_ui/src/buffer_codegen.rs`

**What it does**:
- `ActionLog` tracks per-buffer diffs
- `TrackedBuffer` + `UnreviewedEdits`
- `accept_edits_in_ranges()` updates diff_base
- `reject_edits_in_ranges()` with 3 cases + undo

**Impact**: Users can accept/reject individual changes, not entire edits.

**Effort**: Large (UI redesign + state management)

#### 3.1.3 PageRank Repo Map — Aider

**Gap**: AVA has basic repo map; Aider has PageRank-based.

**Reference Implementation**: `aider/repomap.py`

**What it does**:
- Tree-sitter parses all files → extracts tags
- Build networkx `MultiDiGraph` with files as nodes
- Weighted reference edges (boost mentioned identifiers 10x)
- `nx.pagerank()` with personalization toward chat files
- Binary search to fit ranked tags into token budget

**Impact**: Intelligent context selection based on code graph structure.

**Effort**: Medium (Rust implementation via `dispatchCompute`)

#### 3.1.4 3-Layer Loop Detection — Gemini CLI

**Gap**: AVA has basic doom loop; Gemini has sophisticated 3-layer.

**Reference Implementation**: `packages/core/src/services/loopDetectionService.ts`

**What it does**:
- **Layer 1**: Tool hash matching (5 identical consecutive calls)
- **Layer 2**: Content chanting (10 identical 50-char chunks)
- **Layer 3**: LLM-as-judge (after 40 turns, adaptive intervals)
- Adaptive check intervals based on confidence

**Impact**: Zero false positives on productive patterns (batch operations).

**Effort**: Medium (Layer 1-2 in TypeScript, Layer 3 uses LLM)

#### 3.1.5 9 Condenser Strategies — OpenHands

**Gap**: AVA has 1 strategy; OpenHands has 9.

**Reference Implementation**: `openhands/memory/condenser/impl/`

**What it does**:
1. **NoOp** — Passthrough
2. **RecentEvents** — Keep last N
3. **LLMSummarizing** — Structured 9-section summary
4. **AmortizedForgetting** — Drop middle half
5. **ObservationMasking** — Replace with `<MASKED>`
6. **StructuredSummary** — Function-calling output
7. **BrowserOutput** — Keep only recent browser obs
8. **LLMAttention** — Rank by importance
9. **ConversationWindow** — Token-aware window

**Impact**: Most sophisticated context compaction in the field.

**Effort**: Medium (implement 3-4 key strategies)

### 3.2 Medium Priority Gaps (Important)

#### 3.2.1 OS-Level Sandboxing — Codex CLI

**Gap**: AVA has Docker; Codex has OS-level (Seatbelt/Landlock/seccomp).

**Reference Implementation**:
- macOS: `codex-rs/core/src/seatbelt.rs`
- Linux: `codex-rs/linux-sandbox/src/landlock.rs`
- Windows: `codex-rs/windows-sandbox-rs/src/`

**What it does**:
- Seatbelt: macOS sandbox with base policy
- Landlock: Linux filesystem sandbox
- seccomp: Linux network restrictions
- Windows: Restricted tokens + firewall

**Impact**: Lighter than Docker, no container overhead.

**Effort**: Large (3 platform implementations)

#### 3.2.2 History Processors — SWE Agent

**Gap**: AVA has middleware; SWE Agent has specialized history transforms.

**Reference Implementation**: `sweagent/agent/history_processors.py`

**What it does**:
- `LastNObservations` — Keep only last N
- `ClosedWindowHistoryProcessor` — Summarize stale file windows
- `CacheControlHistoryProcessor` — Anthropic prompt caching
- Chain-of-responsibility pattern

**Impact**: Flexible history transformation pipeline.

**Effort**: Medium (new extension point)

#### 3.2.3 Action Samplers — SWE Agent

**Gap**: AVA takes first response; SWE Agent samples N.

**Reference Implementation**: `sweagent/agent/action_sampler.py`

**What it does**:
- Generate N candidate responses
- Evaluate each
- Pick best

**Impact**: Improves quality at compute cost.

**Effort**: Medium (new agent loop mode)

#### 3.2.4 Conseca — Gemini CLI

**Gap**: AVA has static policies; Gemini has dynamic LLM-generated.

**Reference Implementation**: `packages/core/src/safety/conseca/`

**What it does**:
- Phase 1: LLM generates least-privilege policies from prompt
- Phase 2: Second LLM enforces per tool call
- Adapts to user intent dynamically

**Impact**: Security policies that adapt to context.

**Effort**: Medium (two LLM calls per session)

#### 3.2.5 Concurrent Builds — Plandex

**Gap**: AVA applies edits sequentially; Plandex runs 4 strategies concurrently.

**Reference Implementation**: `app/server/model/plan/build.go`

**What it does**:
- Auto-apply (direct structured edit)
- Fast-apply (quick edit)
- Validation loop (iterative fix)
- Whole-file fallback
- Race: first valid wins

**Impact**: Higher success rate via parallel strategies.

**Effort**: Medium (parallel execution + race logic)

### 3.3 Lower Priority Gaps (Nice-to-have)

#### 3.3.1 Ghost Snapshots — Codex CLI

**Reference**: `codex-rs/utils/git/src/ghost_commits.rs`

Invisible git commits for rollback without polluting history.

#### 3.3.2 MCP Server Mode — Zed

**Reference**: `crates/agent/src/native_agent_server.rs`

Expose AVA's tools to other MCP clients.

#### 3.3.3 A2A Protocol — Gemini CLI

**Reference**: `packages/a2a-server/src/`

Agent-to-agent interoperability protocol.

#### 3.3.4 Tab Autocomplete — Continue

**Reference**: `core/autocomplete/`

Inline edit suggestions (requires editor integration).

#### 3.3.5 Cross-Provider Normalization — Pi Mono

**Reference**: `packages/ai/src/providers/transform-messages.ts`

Handle thinking blocks, tool ID normalization, orphaned tool repair.

---

## 4. Recommended Next Actions

### 4.1 Immediate (This Quarter)

| Priority | Action | Reference | Effort | Impact |
|----------|--------|-----------|--------|--------|
| 1 | Implement PageRank repo map | Aider: `aider/repomap.py` | Medium | High — Better context selection |
| 2 | Upgrade loop detection to 3-layer | Gemini: `loopDetectionService.ts` | Medium | High — Fewer false positives |
| 3 | Add 3-4 condenser strategies | OpenHands: `memory/condenser/` | Medium | High — Better context compaction |
| 4 | Implement streaming diff | Zed: `streaming_diff.rs` | Large | Very High — Better UX |

### 4.2 Short-term (Next Quarter)

| Priority | Action | Reference | Effort | Impact |
|----------|--------|-----------|--------|--------|
| 5 | Add per-hunk accept/reject | Zed: `buffer_codegen.rs` | Large | Very High — Better UX |
| 6 | Implement history processors | SWE Agent: `history_processors.py` | Medium | Medium — Flexible history |
| 7 | Add action samplers | SWE Agent: `action_sampler.py` | Medium | Medium — Better quality |
| 8 | Explore OS-level sandbox | Codex: `seatbelt.rs`, `landlock.rs` | Large | Medium — Lighter isolation |

### 4.3 Medium-term (Following Quarter)

| Priority | Action | Reference | Effort | Impact |
|----------|--------|-----------|--------|--------|
| 9 | Implement concurrent builds | Plandex: `build.go` | Medium | Medium — Higher success rate |
| 10 | Add Conseca-like policies | Gemini: `safety/conseca/` | Medium | Medium — Dynamic security |
| 11 | Add MCP server mode | Zed: `native_agent_server.rs` | Medium | Low — Ecosystem play |
| 12 | Cross-provider normalization | Pi Mono: `transform-messages.ts` | Small | Low — Better multi-provider |

---

## 5. Summary Table

| Dimension | Lead | Match | Trail (High Priority) |
|-----------|------|-------|----------------------|
| **Edit System** | ~41 tools, 8 strategies | Fuzzy matching, streaming | StreamingDiff, per-hunk UI |
| **Context & Memory** | FTS5 recall, DAG sessions | Compaction, token tracking | PageRank repo map, 9 condensers |
| **Agent Loop** | Praxis 3-tier hierarchy | Subagent spawning, retry | 3-layer loop detection |
| **Safety** | Middleware pipeline | Git checkpoints, filtering | OS-level sandbox, Conseca |
| **UX** | Desktop Tauri app | Streaming, session mgmt | Per-hunk UI, autocomplete |
| **Extensions** | Richest API (8 methods) | Hooks, MCP client | MCP server, A2A |

---

## 6. Key Takeaways

1. **AVA leads in breadth** — ~41 tools, richest extension API, best multi-agent system
2. **AVA matches in depth** — Competitive edit strategies, context management, safety
3. **AVA trails in polish** — Zed's streaming diff and per-hunk UI are significant UX gaps
4. **Most urgent gaps** — Edit system (StreamingDiff, per-hunk), context (PageRank, condensers), loop detection
5. **Biggest opportunity** — Combining AVA's breadth with Zed's streaming polish would be unmatched

---

*Analysis based on deep audit of 12 competitor codebases, March 2026*
