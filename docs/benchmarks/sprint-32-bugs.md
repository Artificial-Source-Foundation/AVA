# Sprint 32 — Bug Backlog

## Bug 1: Empty turn waste (Medium)

**File**: `crates/ava-agent/src/loop.rs` (lines 272-274, streaming path)

**Symptom**: After the agent completes its answer, it generates 2-3 empty turns before the stuck detector fires `StuckAction::Stop("2 consecutive empty responses")`. Wastes ~2 extra API calls per run, adds 2-4 seconds latency.

**Root cause**: No "natural completion" signal. The model finishes answering but doesn't call `attempt_completion`, so the loop keeps requesting turns until stuck detector intervenes.

**Fix options**:
1. Add `attempt_completion` to tool definitions so the model can signal done
2. Heuristic: if model returns content with no tool calls, treat as complete (stop after 1 such response)
3. System prompt instruction to call `attempt_completion` when done

---

## Bug 2: Large file read causes tool_use/tool_result mismatch (High)

**File**: `crates/ava-agent/src/loop.rs` (message construction path, lines 276-293)

**Symptom**: Reading a 10K-line file causes the next API call to fail:
```
Provider 'OpenAI' error: request failed (400 Bad Request):
"tool_use ids were found without tool_result blocks immediately after"
```

**Root cause**: Large tool result causes context to exceed limits or breaks the `tool_use` -> `tool_result` message pairing that Anthropic's API requires.

**Fix options**:
1. Truncate tool results exceeding a threshold (e.g., 50KB) before adding to context
2. Context compaction should detect oversized tool results pre-emptively
3. Add `max_output` cap to read tool (already has `limit` for lines)

---

## Bug 3: TUI 400 error on API calls (High)

**Symptom**: TUI boots fine, can type and send messages, but every API call fails:
```
Provider 'OpenAI' error: request failed (400 Bad Request): {"error":{"message":"anthropic/claude...
```
Headless mode with the same provider/model (`--provider openrouter --model anthropic/claude-sonnet-4`) works perfectly. The TUI uses the same `AgentStack::run` → `AgentLoop::run_streaming` code path.

**To investigate**: Compare how the TUI vs headless constructs messages or initializes the provider. Could be a message format difference, or the model name is being sent incorrectly in the TUI path.

---

## Bug 4: No default config — requires --provider --model flags (Medium)

**Symptom**: Running `ava` without `--provider` and `--model` either errors or uses a broken default. Users must type:
```
cargo run --bin ava -- --provider openrouter --model anthropic/claude-sonnet-4
```
Competitors like OpenCode just run `opencode` and read config from a file.

**Expected**: `ava` should read `~/.ava/config.yaml` for default provider/model, or prompt on first run. The `--provider`/`--model` flags should be overrides, not required.

**File**: `crates/ava-tui/src/config/cli.rs` + `crates/ava-config/`

---

## Feature Gap: TUI design vs OpenCode (Epic)

**Current state**: AVA's TUI is functional but minimal — plain message list, simple status bar, basic composer box.

**OpenCode reference** (v1.2.20):
- ASCII art logo splash screen on empty state
- Centered input with placeholder text ("Ask anything...")
- Model selector badge inline with input (e.g., "Build  Kimi K2.5  Model Studio  Coding  Plan")
- Keyboard shortcut hints below input ("tab agents  ctrl+p commands")
- Tips section ("Start a message with ! to run shell commands directly")
- Rich status bar: git branch + path, MCP server count, version number
- `/command` system with palette (ctrl+p)
- `/models` command to switch models interactively

**AVA TUI gaps**:
1. No splash/welcome screen — jumps straight to empty message list
2. No inline model selector badge
3. No `/command` palette (only `/model` exists)
4. No keyboard shortcut hints
5. No tips/onboarding
6. Status bar is basic: turn count + activity + message count
7. No git branch display in status bar
8. No MCP server count display
9. No version display
10. No `!` shell command shortcut

**Priority order for parity**:
1. `/command` palette with `/models`, `/help`, `/clear`, `/session`
2. Welcome screen with logo + input hints
3. Rich status bar (git branch, model badge, version)
4. `!` prefix for shell commands
5. Model selector inline with input

---

## Observation: Agent prefers bash over native tools (Low)

**Symptom**: Agent calls `bash(ls -la)` instead of `glob(*)` for directory listing.

**Fix**: Nudge in system prompt to prefer native tools (read/write/edit/glob/grep) over bash equivalents for better sandboxing and performance.
