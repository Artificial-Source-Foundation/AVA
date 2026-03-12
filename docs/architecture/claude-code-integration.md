# Claude Code Integration

> AVA uses Claude Code (CC) as an autonomous subagent, not as an API proxy.
> CC runs as a subprocess via `claude -p`, using the user's own CC subscription.
> No TOS violation: we invoke CC the same way a user would from their terminal.

## Concept

Claude Code is an agent — it has its own brain, its own tools, its own
permission system. AVA treats it the same way it treats any other subagent:
give it a goal, let it work, get the result back.

CC appears in AVA as just another provider option. When configuring a
subagent, you pick `provider = "claude-code"` the same way you'd pick
`provider = "openrouter"`. The difference is that CC manages its own tools
and context instead of AVA controlling them.

```
User → AVA TUI → AgentStack
                    │
                    ├── Native subagents (provider = "openrouter", "anthropic", etc.)
                    │   └── AVA's agent loop + AVA's tools
                    │
                    └── CC subagents (provider = "claude-code")
                        └── spawns `claude -p` subprocess
                            └── CC runs autonomously with its own tools
                            └── returns structured result to AVA
```

## Why Not Use CC as a Raw LLM Provider?

CC is not a transparent API proxy. It wraps your message in its own system
prompt, manages its own context window, and you can't control sampling
params. Using CC without its tools would mean paying for its overhead while
fighting its design.

Users who want raw Claude API access should add an Anthropic API key —
that's what `ava-llm/providers/anthropic.rs` is for.

**CC's value is as an agent, not as a model.** We leverage that.

## Usage Patterns

### Pattern A: Agent Invokes CC as a Tool

The AVA agent decides to delegate a subtask to Claude Code:

```
User: "Review auth.rs for security issues"
AVA agent: [thinking] This is a focused review task, I'll delegate to CC
AVA agent: [tool_call: claude_code] {
  "goal": "Review crates/ava-auth/src/lib.rs for security vulnerabilities."
}
  └── spawns: claude -p "..." --output-format json --allowedTools "Read,Grep,Glob"
  └── CC reads files, greps for patterns, analyzes code
  └── returns: { "result": "Found 3 issues: ...", "cost_usd": 0.02 }
AVA agent: [to user] Claude Code found 3 security issues: ...
```

### Pattern B: User Configures a CC-Powered Subagent

Users define subagents in `agents.toml` with `provider = "claude-code"`:

```toml
# .ava/agents.toml
[agents.code-reviewer]
description = "Security-focused code reviewer"
provider = "claude-code"
prompt = "You are a security code reviewer. Focus on OWASP top 10."
allowed_tools = ["Read", "Grep", "Glob"]
max_turns = 15
max_budget_usd = 0.50

[agents.refactorer]
description = "Autonomous code refactorer"
provider = "claude-code"
prompt = "Refactor code for clarity and performance."
allowed_tools = ["Read", "Edit", "Bash", "Glob", "Grep"]
max_turns = 25
max_budget_usd = 2.00
```

When AVA's agent spawns a subagent, it checks the `provider` field:
- `provider = "openrouter"` (or any LLM provider) → AVA's agent loop
- `provider = "claude-code"` → spawns `claude -p` subprocess

The user sees the subagent in the sidebar like any other, with status,
token count, and cost — but CC is doing the work.

## Tool Implementation

### Location

`crates/ava-tools/src/core/claude_code.rs`

### Tool Definition

```rust
pub struct ClaudeCodeTool {
    binary_path: PathBuf,        // resolved via `which claude` or config
    default_allowed_tools: Vec<String>,
    session_persistence: bool,   // default: false
}
```

**Parameters:**

| Param | Type | Required | Description |
|-------|------|----------|-------------|
| `goal` | string | yes | What CC should accomplish |
| `allowed_tools` | string[] | no | CC tools to enable (default: Read,Grep,Glob) |
| `max_turns` | int | no | Turn limit (default: 10) |
| `max_budget_usd` | float | no | Cost limit (default: configurable) |
| `working_directory` | string | no | CWD for CC (default: workspace root) |
| `system_prompt` | string | no | Append to CC's system prompt |

**Returns:** Structured result with CC's response text, usage stats, and cost.

### Subprocess Invocation

```bash
claude -p "<goal>" \
  --output-format json \
  --allowedTools "Read,Grep,Glob" \
  --max-turns 10 \
  --max-budget-usd 1.00 \
  --no-session-persistence \
  --append-system-prompt "<extra instructions>"
```

### Streaming (for subagent sidebar display)

For subagent use, we use `--output-format stream-json` to show real-time
progress in AVA's sidebar:

```bash
claude -p "<goal>" \
  --output-format stream-json \
  --verbose \
  --allowedTools "Read,Edit,Bash" \
  --max-turns 25 \
  --no-session-persistence
```

**Stream event parsing:**

Each line is a JSON object. Key event types:

| Event | Type Path | AVA Mapping |
|-------|-----------|-------------|
| Init | `type: "system", subtype: "init"` | Session started |
| Text streaming | `event.delta.type: "text_delta"` | `AgentEvent::StreamChunk` |
| Tool call start | `event.content_block.type: "tool_use"` | `AgentEvent::ToolCall` |
| Tool input | `event.delta.type: "input_json_delta"` | Tool args streaming |
| Tool done | `event.type: "content_block_stop"` | `AgentEvent::ToolResult` |
| Turn end | `event.type: "message_stop"` | One agentic turn finished |
| Final result | `type: "result"` | `AgentEvent::Complete` |

### JSON Response Shape

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "result": "Found 3 security issues in auth.rs: ...",
  "duration_ms": 8500,
  "cost_usd": 0.035,
  "turns": 4,
  "usage": {
    "input_tokens": 12000,
    "output_tokens": 2500,
    "cache_read_input_tokens": 8000
  }
}
```

## Binary Discovery

Two sources, checked in order:

1. **Config** — `~/.ava/config.yaml`:
   ```yaml
   claude_code:
     binary_path: "/usr/local/bin/claude"
     session_persistence: false
     default_max_turns: 10
     default_max_budget_usd: 1.00
     default_allowed_tools:
       - Read
       - Grep
       - Glob
   ```

2. **PATH** — `which claude` at startup. Cached for session lifetime.

If neither found, the `claude_code` tool is not registered (graceful absence).

## Safety & Isolation

### Tool Control

AVA controls what CC can do via `--allowedTools`. Default is **read-only**:

| Preset | Tools | Use Case |
|--------|-------|----------|
| `readonly` | Read, Grep, Glob | Code review, analysis |
| `edit` | readonly + Edit, Write | Refactoring, bug fixes |
| `full` | edit + Bash | Autonomous coding |

Users configure per-agent in `agents.toml`.

### Session Isolation

- Default: `--no-session-persistence` — CC sessions don't pollute user's
  `~/.claude/sessions/`
- Configurable via `claude_code.session_persistence` in config
- Each invocation is independent (no cross-contamination between tasks)

### Cost Control

- `--max-budget-usd` prevents runaway spending
- **All cost limits are user-configurable** — per-agent in `agents.toml`,
  global defaults in `config.yaml`, and per-invocation via tool params
- Most users will be on CC subscriptions, so defaults should be generous
- AVA tracks cumulative CC cost across all subagent invocations
- Displayed in status bar alongside AVA's own LLM costs

### File Safety

- CC operates in the same workspace as AVA
- CC respects its own permission system (blocked commands, path safety)
- AVA's permission inspector does NOT double-check CC's actions —
  CC has its own safety layer

## Subagent Integration

### AgentStack Changes

`crates/ava-agent/src/stack.rs` — when spawning a subagent, check the
`provider` field in `agents.toml`:

```rust
// In agent config resolution
match agent_config.provider.as_deref() {
    Some("claude-code") => spawn_claude_code_subagent(agent_config),
    _ => spawn_native_subagent(agent_config),  // uses ava-agent + ava-llm
}
```

### Sidebar Display

CC subagents appear in the sidebar like native subagents:

```
┌─ Agents ──────────────────┐
│ ● code-reviewer [CC]  3s  │  ← [CC] badge distinguishes from native
│   Reading auth.rs...      │  ← parsed from stream events
│ ✓ linter           done   │
│ ○ main                    │
└───────────────────────────┘
```

The `[CC]` badge tells the user this subagent is powered by Claude Code.
Stream events are parsed to show what CC is currently doing.

## Configuration Reference

### `~/.ava/config.yaml`

```yaml
claude_code:
  # Path to claude binary (default: auto-detect via PATH)
  binary_path: null

  # Save CC sessions to ~/.claude/sessions/ (default: false)
  session_persistence: false

  # Defaults for claude_code tool invocations (all user-configurable)
  default_max_turns: 10
  default_max_budget_usd: 5.00
  default_allowed_tools:
    - Read
    - Grep
    - Glob
```

### `.ava/agents.toml`

```toml
[agents.security-reviewer]
description = "Reviews code for security vulnerabilities"
provider = "claude-code"
prompt = "You are a security expert. Check for OWASP top 10 vulnerabilities."
allowed_tools = ["Read", "Grep", "Glob"]
max_turns = 15
max_budget_usd = 1.00

[agents.full-coder]
description = "Autonomous coder that can read, edit, and run commands"
provider = "claude-code"
prompt = "You are an expert programmer."
allowed_tools = ["Read", "Edit", "Write", "Bash", "Glob", "Grep"]
max_turns = 30
max_budget_usd = 10.00
```

## Implementation Plan

### Phase 1: Core Tool

1. Add `claude_code.rs` to `crates/ava-tools/src/core/`
2. Implement `Tool` trait — spawn `claude -p --output-format json`
3. Binary discovery (`which claude` + config override)
4. Parse JSON response → tool result
5. Register in `register_core_tools()` (conditional on binary found)
6. Config: `claude_code` section in `~/.ava/config.yaml`
7. Tests: mock subprocess, parse response, binary not found

### Phase 2: Streaming Subagent

1. Add `provider = "claude-code"` support in agent config
2. Parse `stream-json` events → `AgentEvent` mapping
3. Wire into `agents.toml` provider field
4. Sidebar display with `[CC]` badge
5. Cumulative cost tracking across CC invocations

### Phase 3: Polish

1. `/agents` modal shows CC-powered agents with badge
2. Cost breakdown (AVA cost vs CC cost) in status bar
3. Error handling: CC not installed, CC auth expired, CC rate limited
4. User guide for setting up CC subagents

## Key Files

| File | Role |
|------|------|
| `crates/ava-tools/src/core/claude_code.rs` | Tool implementation |
| `crates/ava-agent/src/stack.rs` | Provider-based subagent dispatch |
| `crates/ava-config/src/lib.rs` | ClaudeCodeConfig |
| `crates/ava-config/src/agents.rs` | provider field in AgentConfig |
| `crates/ava-tui/src/ui/sidebar.rs` | [CC] badge rendering |
| `docs/architecture/claude-code-integration.md` | This document |

## TOS Compliance

This integration is **TOS-compliant** because:

1. We invoke `claude` the same way a user would from their terminal
2. We don't extract, intercept, or reuse CC's OAuth tokens
3. We don't bypass CC's authentication or billing
4. The user's own CC subscription handles all API calls and costs
5. We don't modify CC's behavior — we pass flags it officially supports
6. `-p` (print mode) is explicitly designed for programmatic use
