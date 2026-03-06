# Codex CLI Deep Audit

> Comprehensive analysis of OpenAI Codex CLI's AI coding agent implementation
> Audited: 2026-03-05
> Based on codebase at `docs/reference-code/codex-cli/`

---

## Overview

Codex CLI is OpenAI's official coding agent reference implementation and represents the **gold standard for OS-level sandboxing**. Built in Rust across 68+ crates, it implements **Seatbelt (macOS)**, **bwrap + Landlock + seccomp (Linux)**, and **restricted tokens + Windows Firewall (Windows)** for comprehensive security. Its core differentiator is a **minimal tool set** (~15 tools) with a single `apply_patch` tool for edits using a custom envelope-based patch format. Codex CLI pioneered **ghost snapshots** — detached git commits for invisible file state capture and rollback. The architecture is a production-grade, fully async agent runtime centered on a ~10,000-line orchestrator with turn-based execution, parallel tool calls via `ToolCallRuntime` with reader-writer locks, and a sophisticated multi-agent system. The TUI is built on **ratatui + crossterm** (not React Ink), featuring an adaptive two-mode streaming engine with hysteresis-based chunking.

---

## Key Capabilities

### Edit System

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Single `apply_patch` Tool** | Custom envelope format (`*** Begin Patch` / `*** End Patch`) | `codex-rs/apply-patch/src/parser.rs` |
| **4-Pass Fuzzy Matching** | Exact → rstrip → trim-both → Unicode normalization | `codex-rs/apply-patch/src/seek_sequence.rs` |
| **Git Cherry-Pick Fallback** | Uses git three-way merge as final fallback | `codex-rs/apply-patch/src/lib.rs` |
| **Self-Correction** | `FixLLMEditWithInstruction()` with dedicated LLM | `codex-rs/core/src/utils/llm-edit-fixer.rs` |
| **Streaming Diff Progress** | Progress indicator during file generation | UI components |
| **Heredoc Unwrapping** | Detects and strips bash heredoc wrappers | `codex-rs/apply-patch/src/invocation.rs` |
| **Lint Integration** | Automatic linting with retry-based correction | Lint utilities |

### Context & Memory

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **ContextManager** | Central transcript manager with token tracking | `codex-rs/core/src/context_manager/history.rs` |
| **Rollup/Compaction** | Condenses older messages into summaries | `codex-rs/core/src/compact.rs` |
| **Ghost Snapshots** | Detached git commits for file state | `codex-rs/utils/git/src/ghost_commits.rs` |
| **Token Budget Management** | Byte-based heuristic estimation | `codex-rs/core/src/context_manager/history.rs` |
| **JSONL Session Files** | Append-only log, supports session resume | `codex-rs/core/src/rollout/list.rs` |
| **SQLite State Database** | 18 migrations for threads, logs, memory | `codex-rs/state/` |
| **Two-Phase Memory Pipeline** | Phase 1 extraction + Phase 2 consolidation | `codex-rs/core/src/memories/` |

### Agent Loop & Reliability

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Turn-Based Rust Loop** | `Codex` struct with bounded async channels | `codex-rs/core/src/codex.rs` |
| **Responses API Streaming** | WebSocket v1/v2 → HTTPS fallback | `codex-rs/core/src/client.rs` |
| **Parallel Function Calls** | `ToolCallRuntime` with RwLock pattern | `codex-rs/core/src/tools/parallel.rs` |
| **Full Async with Cancellation** | `CancellationToken` hierarchy, `AbortOnDropHandle` | `codex-rs/core/src/codex.rs` |
| **Multi-Agent System** | `AgentControl` + `MultiAgentHandler` | `codex-rs/core/src/agent/control.rs` |
| **Completion Detection** | `SamplingRequestResult.needs_follow_up` check | `codex-rs/core/src/codex.rs` |
| **Retry with Exponential Backoff** | Transport fallback, context-window handling | `codex-rs/core/src/codex.rs` |
| **Model Fallback** | `ModelAvailabilityService` with terminal/sticky failure tracking | `codex-rs/core/src/fallback/handler.rs` |

### Safety & Permissions (Gold Standard)

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Seatbelt (macOS)** | Full sandbox with base policy, network policy | `codex-rs/core/src/seatbelt.rs`, `seatbelt_base_policy.sbpl` |
| **Landlock + seccomp (Linux)** | V5 filesystem + BPF network enforcement | `codex-rs/linux-sandbox/src/landlock.rs` |
| **Bubblewrap (Linux)** | Filesystem sandboxing | `codex-rs/linux-sandbox/src/bwrap.rs` |
| **Windows Restricted Token** | `CreateRestrictedToken` with disable-max-privilege | `codex-rs/windows-sandbox-rs/src/token.rs` |
| **Windows Firewall** | Per-sandbox outbound-block rule | `codex-rs/windows-sandbox-rs/src/firewall.rs` |
| **Exec Policy** | Starlark-based rule DSL | `codex-rs/core/src/exec_policy.rs` |
| **Ghost Commits** | Full rollback system | `codex-rs/utils/git/src/ghost_commits.rs` |
| **Network Proxy** | SSRF protection, MITM/SOCKS5/HTTP support | `codex-rs/network-proxy/src/` |
| **Process Hardening** | Anti-debug, anti-dump, env stripping | `codex-rs/process-hardening/src/lib.rs` |
| **Secret Sanitization** | Regex-based redaction | `codex-rs/secrets/src/sanitizer.rs` |
| **Approval System** | 5 decision types with sandbox escalation | `codex-rs/core/src/tools/sandboxing.rs` |

### UX & Developer Experience

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Ratatui TUI** | Native Rust terminal UI (not React Ink) | `codex-rs/tui/src/` |
| **Two-Mode Streaming** | Smooth (~120fps) vs CatchUp (batch) | `codex-rs/tui/src/streaming/chunking.rs` |
| **Adaptive Markdown Streaming** | Hysteresis-based mode switching | `codex-rs/tui/src/streaming/chunking.rs` |
| **Session Resume** | JSONL with interactive picker | `codex-rs/tui/src/resume_picker.rs` |
| **Diff Display** | Three color depth tiers with auto-detection | `codex-rs/tui/src/diff_render.rs` |
| **Approval Modes** | Ask, Auto-Edit, YOLO, Plan | `codex-rs/protocol/src/approvals.rs` |
| **Approval UI** | Request/response flow | `codex-rs/tui/src/approvals/` |
| **Notify Integration** | Desktop notifications | `codex-rs/tui/src/` |

### Unique/Novel Features

| Feature | Description | File Path |
|---------|-------------|-----------|
| **OS-Level Sandboxing** | Seatbelt/Landlock/seccomp/Windows — the gold standard | `codex-rs/core/src/seatbelt.rs`, `codex-rs/linux-sandbox/`, `codex-rs/windows-sandbox-rs/` |
| **Ghost Snapshots** | Invisible git commits for rollback | `codex-rs/utils/git/src/ghost_commits.rs` |
| **Two-Phase Memory Pipeline** | Phase 1 extraction + Phase 2 consolidation | `codex-rs/core/src/memories/` |
| **Multi-Agent with Fork/Resume** | Spawn, fork, resume, messaging, monitoring | `codex-rs/core/src/agent/control.rs` |
| **Approval with Sandbox Escalation** | Auto-approve → ask → reject → retry cascade | `codex-rs/core/src/tools/orchestrator.rs` |
| **Network Proxy with SSRF Protection** | Managed network with policy enforcement | `codex-rs/network-proxy/src/` |
| **Ratatui Streaming Engine** | Adaptive two-mode with hysteresis | `codex-rs/tui/src/streaming/` |
| **Process Hardening** | Anti-debug, anti-dump, env stripping | `codex-rs/process-hardening/src/lib.rs` |
| **12 Independent Safety Subsystems** | Defense-in-depth security model | Throughout codebase |

---

## Worth Stealing (for AVA)

### High Priority

1. **OS-Level Sandboxing** (`codex-rs/core/src/seatbelt.rs`, `codex-rs/linux-sandbox/`, `codex-rs/windows-sandbox-rs/`)
   - Seatbelt (macOS), Landlock+seccomp (Linux), Windows restricted tokens
   - Gold standard for security
   - Should add as alternative to Docker sandbox

2. **Ghost Snapshots** (`codex-rs/utils/git/src/ghost_commits.rs`)
   - Invisible git commits for file state capture
   - Perfect complement to AVA's existing git snapshots

3. **Two-Phase Memory Pipeline** (`codex-rs/core/src/memories/`)
   - Phase 1: extraction from stale rollouts
   - Phase 2: global consolidation
   - Usage-count prioritization

### Medium Priority

4. **Approval with Sandbox Escalation** (`codex-rs/core/src/tools/orchestrator.rs`)
   - Auto-approve → ask → reject → retry cascade
   - Clean UX for permission handling

5. **Network Proxy with SSRF Protection** (`codex-rs/network-proxy/src/`)
   - Managed network for sandboxed execution
   - Policy-based outbound filtering

6. **Ratatui Streaming Engine** (`codex-rs/tui/src/streaming/`)
   - Adaptive two-mode with hysteresis
   - Excellent UX for streaming responses

7. **Process Hardening** (`codex-rs/process-hardening/src/lib.rs`)
   - Anti-debug, anti-dump, env stripping
   - Pre-main security initialization

### Lower Priority

8. **Multi-Agent System** — AVA's Praxis hierarchy is more sophisticated
9. **Model Fallback** — Useful but not critical
10. **TUI in Rust** — AVA uses Tauri + SolidJS, different approach

---

## AVA Already Has (or Matches)

| Codex CLI Feature | AVA Equivalent | Status |
|-------------------|----------------|--------|
| Apply patch tool | `apply_patch` with diff formats | ✅ Parity |
| Fuzzy matching | Fuzzy matching in edit cascade | ✅ Parity |
| Ghost snapshots | Git snapshots, ghost checkpoints | ✅ Parity |
| Session resume | Session CRUD with DAG | ✅ Better |
| Multi-agent | Praxis 3-tier hierarchy (13 agents) | ✅ Better |
| Tool count | 55+ tools vs ~15 | ✅ Better |
| Sandboxing | Docker sandbox (optional) | ⚠️ Should add OS-level |
| Context compaction | Token compaction extension | ✅ Parity |
| Streaming | Streaming tool execution | ✅ Parity |

---

## Anti-Patterns to Avoid

1. **68+ Crates** — Overly granular crate structure; AVA's workspace is better organized
2. **OpenAI-Only** — Hardcoded to OpenAI; AVA's multi-provider approach is better
3. **Minimal Tool Set** — ~15 tools limits capability; AVA's 55+ tools cover more use cases
4. **Complex Sandboxing** — 12 safety subsystems is heavy; AVA should adopt selectively
5. **Rust-Only Extensions** — No plugin SDK; AVA's TypeScript extensions are more accessible

---

## Recent Additions (Post-March 2026)

Based on git log analysis:

- **Improved Memory Pipeline** — Better consolidation strategies
- **Enhanced Windows Sandbox** — Improved restricted token handling
- **Network Proxy Improvements** — Better SSRF protection
- **UI Polish** — Improved streaming rendering

---

## File Reference Index

| File | Lines | Purpose |
|------|-------|---------|
| `codex-rs/core/src/codex.rs` | ~10,000 | Central orchestrator |
| `codex-rs/core/src/seatbelt.rs` | 1,342 | macOS sandbox |
| `codex-rs/linux-sandbox/src/landlock.rs` | 323 | Linux Landlock/seccomp |
| `codex-rs/windows-sandbox-rs/src/lib.rs` | 646 | Windows sandbox hub |
| `codex-rs/utils/git/src/ghost_commits.rs` | ~1,500 | Ghost snapshot system |
| `codex-rs/core/src/exec_policy.rs` | 1,452 | Execution policy engine |
| `codex-rs/network-proxy/src/runtime.rs` | 1,484 | Network proxy runtime |
| `codex-rs/tui/src/app.rs` | 5,586 | Main TUI app |
| `codex-rs/tui/src/chatwidget.rs` | 8,273 | Chat rendering |
| `codex-rs/core/src/agent/control.rs` | 569 | Multi-agent control |

---

*Audit generated by subagent analysis across 6 dimensions: Edit System, Context & Memory, Agent Loop, Safety, UX, and Unique Features.*
