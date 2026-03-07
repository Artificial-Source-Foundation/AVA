# AVA vs OpenCode Performance Benchmark — 2026-03

## Environment

- **OS**: Linux 6.17.0-14-generic #14~24.04.1-Ubuntu SMP PREEMPT_DYNAMIC x86_64
- **CPU**: Intel(R) Core(TM) i9-10850K CPU @ 3.60GHz
- **RAM**: 32GB DDR4
- **AVA version**: f2620f5 (master)
- **OpenCode version**: 1.2.20
- **Provider**: OpenRouter / anthropic/claude-sonnet-4
- **Date**: 2026-03-07

## Performance Results

| Metric | AVA (Rust) | OpenCode (Go) | Ratio |
|--------|-----------|---------------|-------|
| Cold start (`--help`) | **2ms** | 680ms | **340x faster** |
| Time-to-first-token | **2,361ms** | ~15,870ms* | **~6.7x faster** |
| Memory — idle (`--help`) | **7.8MB** | 343MB | **44x less** |
| Memory — real run | **17.8MB** | 551MB | **31x less** |
| Binary size | **17MB** | 160MB | **9.4x smaller** |
| File read (10K lines) | 4,758ms** | — | N/A |
| Grep (codebase) | 9,545ms** | — | N/A |

\* OpenCode `run` timing includes full completion, not just TTFT. Direct comparison is approximate.
\** Tool execution times are dominated by LLM API latency, not local I/O.

## Tool Test Results

| Tool | Status | Tool Called | Notes |
|------|--------|-----------|-------|
| read | **PASS** | `read(path, limit:3)` | Used `limit` param correctly |
| write | **PASS** | `write(path, content)` | 14 bytes written, verified |
| edit | **PASS** | `edit(old_text, new_text)` | Agent did read->edit->verify cycle |
| bash | **PASS** | `bash(uname -a)` | Clean execution, correct OS identification |
| glob | **PASS** | `glob(**/Cargo.toml)` | Found all files including worktrees |
| grep | **PASS** | `grep(LLMProvider)` | 30+ matches across provider files |

All 6 core tools pass.

## Mode Test Results

| Mode | Status | Notes |
|------|--------|-------|
| Headless (`--headless`) | **PASS** | Tool results on stderr, final answer on stdout, exit 0 |
| JSON (`--headless --json`) | **PASS** | Valid NDJSON, all event types present, parseable by jq |
| Multi-agent (`--multi-agent`) | **PASS** | Commander spawned 1 worker, 14 turns, summary event emitted |
| Interactive TUI | **PARTIAL** | Boots, renders, input works. API calls fail with 400 (Bug 3). Design gap vs OpenCode (see below). |

## Interactive TUI Test (Part 1.1)

Tested with: `cargo run --bin ava -- --provider openrouter --model anthropic/claude-sonnet-4`

| Check | Status | Notes |
|-------|--------|-------|
| TUI boots without crash | **PASS** | Clean startup |
| Status bar shows provider/model | **PASS** | `openrouter/anthropic/claude-sonnet-` (truncated) |
| Can type a message and press Enter | **PASS** | Input works |
| Agent streams tokens | **FAIL** | 400 Bad Request on API call (Bug 3) |
| Tool calls show in UI | **FAIL** | Blocked by Bug 3 |
| Ctrl+Q quits cleanly | **PASS** | Clean exit |
| Design quality vs OpenCode | **GAP** | See TUI design gap in bugs doc |

## Desktop Integration

| Component | Status | Notes |
|-----------|--------|-------|
| Tauri backend compile | **PASS** | `cargo check --manifest-path src-tauri/Cargo.toml` clean |
| TypeScript frontend compile | **PASS** | `npx tsc --noEmit` clean |
| `agent_run` command | **PASS** | Exists in `agent_integration.rs` |
| `agent_stream` command | **PASS** | Exists — streaming support |
| `list_tools` command | **PASS** | Returns `Vec<ToolInfo>` |
| `execute_tool` command | **PASS** | Exists |
| Session commands | **MISSING** | No session CRUD in Tauri commands — desktop can't list/load/create sessions |
| `npm run tauri dev` | **PENDING** | Requires display + manual test |

Desktop backend has 30+ Tauri commands across 20 modules. Agent integration, memory, tools, git, permissions all present. Session management is the gap.

## Issues Found

### Bug 1: Empty turn waste (Medium)

**Symptom**: After the agent completes its answer, it generates 2-3 empty turns before the stuck detector fires `StuckAction::Stop("2 consecutive empty responses")`. This wastes API calls.

**Root cause**: The agent loop (`crates/ava-agent/src/loop.rs`) has no "natural completion" signal. The model finishes answering but doesn't call `attempt_completion`, so the loop keeps requesting more turns until the stuck detector intervenes.

**Fix options**:
1. Add `attempt_completion` to the tool definitions so the model can signal it's done
2. Add a heuristic: if the model returns content with no tool calls, treat it as complete (stop after 1 such response instead of waiting for 2 empty ones)
3. Instruct the model in the system prompt to call `attempt_completion` when done

**Impact**: ~2 extra API calls per run, adds 2-4 seconds latency.

### Bug 2: Large file read causes tool_use/tool_result mismatch (High)

**Symptom**: When the agent reads a large file (10K lines), the next API call fails with:
```
Provider 'OpenAI' error: request failed (400 Bad Request):
"tool_use ids were found without tool_result blocks immediately after"
```

**Root cause**: The large tool result causes context to exceed limits or message formatting to break the `tool_use` -> `tool_result` pairing that Anthropic's API requires.

**Fix options**:
1. Truncate tool results that exceed a threshold (e.g., 50KB) before adding to context
2. The context compaction pipeline should detect oversized tool results pre-emptively
3. Add a `max_output` parameter to the read tool (already has `limit`)

**Impact**: Agent crashes on large file reads. Workaround: user can specify `limit` param.

### Bug 3: TUI API calls fail with 400 (High)

**Symptom**: TUI boots and renders correctly but every API call returns `400 Bad Request`. The same provider/model works perfectly in headless mode. Both code paths use `AgentStack::run` → `AgentLoop::run_streaming`.

### Bug 4: No default config — requires --provider --model flags (Medium)

Running `ava` requires `--provider openrouter --model anthropic/claude-sonnet-4` every time. Should read defaults from `~/.ava/config.yaml` like OpenCode does.

### Feature Gap: TUI design parity with OpenCode

AVA's TUI is functional but minimal. OpenCode (v1.2.20) has: splash screen, model selector badge, `/command` palette (ctrl+p), keyboard hints, tips, rich status bar (git branch, MCP count, version), `!` shell shortcut. Full gap analysis in `docs/benchmarks/sprint-32-bugs.md`.

### Observation: Agent prefers bash over native tools

The agent sometimes calls `bash(ls -la)` instead of `glob(*)` for file listing. Not a bug, but the system prompt could nudge toward native tools for better performance and sandboxing.

## Summary

AVA's Rust stack is **production-viable for headless/CLI use**, with TUI needing more work:

**What works well:**
- All 6 core tools pass (read, write, edit, bash, glob, grep)
- Headless, JSON, and multi-agent modes all functional
- Performance is dramatically better than OpenCode: 340x faster cold start, 31x less memory, 9.4x smaller binary
- Desktop backend compiles with 30+ Tauri commands

**What needs fixing (4 bugs):**
1. Empty turn waste — 2 extra API calls per run (medium)
2. Large file context crash — tool_use/tool_result mismatch (high)
3. TUI API calls fail with 400 — works headless, broken in TUI (high)
4. No default config — requires --provider --model flags every time (medium)

**What needs building (TUI design):**
- Welcome screen, command palette, model selector, rich status bar, keyboard hints
- Full gap analysis: `docs/benchmarks/sprint-32-bugs.md`

**Desktop gap:**
- Session CRUD commands missing from Tauri backend
