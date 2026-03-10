# Safety & Permissions

AVA implements a multi-layer permission system that classifies tool calls by
risk, enforces safety policies, and presents approval prompts in the TUI.
The system prevents dangerous operations (like `rm -rf /` or `sudo`) even
when auto-approve mode is enabled.

All permission logic lives in `crates/ava-permissions/`.

## Safety Tags

Eight categories describe what a tool or command does
(`crates/ava-permissions/src/tags.rs:6`):

| Tag | Meaning |
|---|---|
| `ReadOnly` | Only reads data, no side effects |
| `WriteFile` | Creates or modifies files |
| `DeleteFile` | Removes files |
| `ExecuteCommand` | Runs a shell command |
| `NetworkAccess` | Makes network requests |
| `SystemModification` | Modifies system-level resources |
| `Destructive` | Potentially irreversible operation |
| `Privileged` | Requires elevated permissions |

## Risk Levels

Five ordered levels from least to most dangerous
(`crates/ava-permissions/src/tags.rs:17`):

```
Safe < Low < Medium < High < Critical
```

### Default Tool Risk Profiles

Every built-in tool has a default profile (`core_tool_profiles()`,
`crates/ava-permissions/src/tags.rs:51`):

| Risk Level | Tools |
|---|---|
| **Safe** | read, glob, grep, diagnostics, codebase_search, recall, memory_search, session_search, session_list, session_load |
| **Low** | write, edit, multiedit, test_runner, lint, remember |
| **Medium** | bash, apply_patch |

Unknown/custom tools default to `Medium` risk.

## Command Classification

The `classify_bash_command()` function (`crates/ava-permissions/src/classifier/mod.rs:67`)
analyzes bash commands and returns structured risk information. It:

1. Checks **whole-command blocked patterns** first (catches `curl | sh`,
   fork bombs, etc.)
2. Splits the command on pipes (`|`), chains (`&&`, `||`), and semicolons (`;`)
3. Classifies each part individually
4. Returns the **highest risk** from all parts

### Blocked Patterns (Critical, always denied)

These are denied even in auto-approve mode:

- `rm -rf /`, `rm -rf ~`, `rm -rf /*` -- catastrophic deletion
- `sudo` -- privilege escalation
- `curl ... | sh`, `wget ... | bash` -- remote code execution
- `dd if=... of=/dev/...` -- raw device writes
- `mkfs.*` -- filesystem formatting
- `> /dev/...` -- device file writes
- Fork bombs (`:(){ :|:& };:`)

### High Risk

- `rm -rf <path>` (non-root paths)
- `git push --force`, `git push -f`
- `git reset --hard`
- `chmod 777`
- SQL destructive operations: `DROP TABLE`, `DELETE FROM` (no WHERE),
  `TRUNCATE TABLE`
- Network commands: `curl`, `wget`

### Medium Risk

- `rm <file>` (single file, no recursive flag)
- `kill -9`, `pkill`, `killall`

### Low Risk

- Build tools: `cargo test`, `cargo clippy`, `npm run`, `make`, `go build`
- Package managers (non-install): `cargo check`, `npm run build`
- Git read operations: `git status`, `git log`, `git diff`

### Safe

- Read-only commands: `ls`, `cat`, `head`, `tail`, `wc`, `echo`, `pwd`,
  `find`, `grep`, `which`, `file`, `stat`, `du`, `df`

### Parser

Word extraction uses tree-sitter for accurate bash parsing, with a heuristic
fallback (`crates/ava-permissions/src/classifier/parser.rs`). The parser
correctly handles quoted strings, preventing bypass via quoting.

## Permission Policies

Three built-in policies (`crates/ava-permissions/src/policy.rs`):

### Permissive

```rust
max_risk_level: RiskLevel::High
blocked_tags: []
```
Allows everything up to High risk automatically. Only Critical operations
require approval. Blocked commands are still denied.

### Standard (Default)

```rust
max_risk_level: RiskLevel::Low
blocked_tags: [Destructive]
```
Allows Safe and Low risk tools automatically. Medium and above require user
approval. Destructive-tagged operations are denied.

### Strict

```rust
max_risk_level: RiskLevel::Safe
blocked_tags: [Destructive, Privileged]
```
Only ReadOnly tools are auto-approved. Everything else requires approval.
Destructive and Privileged operations are denied.

Policies can also specify `allowed_tools` (bypass risk check) and
`blocked_tools` (always deny) lists.

## The 9-Step Inspection

The `DefaultInspector` (`crates/ava-permissions/src/inspector.rs:62`)
evaluates tool calls in this order:

1. **Bash classification** -- For `bash` tool, classify the command. If
   blocked, return `Deny` immediately (even in auto-approve mode).

2. **Path safety** -- For file tools (read, write, edit, multiedit,
   apply_patch), analyze file paths. System paths like `/etc/passwd` are
   denied with `Critical` risk.

3. **Auto-approve check** -- If auto-approve is enabled and the command
   was not blocked in steps 1-2, return `Allow`.

4. **Session-approved check** -- If the tool has been approved for this
   session (user clicked "Allow for session"), return `Allow`.

5. **Blocked tools** -- If the tool is in the policy's `blocked_tools` list,
   return `Deny`.

6. **Allowed tools** -- If the tool is in the policy's `allowed_tools` list,
   return `Allow`.

7. **Blocked tags** -- If any of the tool's safety tags are in the policy's
   `blocked_tags`, return `Deny`.

8. **Risk threshold** -- If the tool's risk level is within the policy's
   `max_risk_level`, return `Allow`.

9. **Rule evaluation** -- Fall through to static and dynamic rule evaluation
   via `PermissionSystem::evaluate()`.

## Path Safety

The `analyze_path()` function (`crates/ava-permissions/src/path_safety.rs`)
checks file paths for:

- **System paths**: `/etc/`, `/boot/`, `/sys/`, `/proc/` -- always Critical
- **Outside workspace**: Paths that resolve outside the workspace root get
  elevated risk and an `Ask` action
- **Path traversal**: `..` components that escape the workspace are detected
  via path normalization

## Tool Approval Flow in the TUI

When a tool call requires approval (not auto-approved, not session-approved,
risk exceeds policy threshold):

1. The TUI receives `AgentEvent::ToolCall` in
   `App::handle_agent_event()` (`crates/ava-tui/src/app/event_handler.rs:99`)
2. If not auto-approve and not session-approved, an `ApprovalRequest` is
   enqueued in `state.permission`
3. The `ToolApproval` modal is shown (`crates/ava-tui/src/widgets/tool_approval.rs`)
4. User can:
   - **Allow once** -- Approve this specific call
   - **Allow for session** -- Add the tool name to `session_approved` set
   - **Deny** -- Reject the call

## Sandbox Integration

The `ava-sandbox` crate (`crates/ava-sandbox/src/lib.rs`) provides OS-level
command sandboxing:

### Backends

- **Linux**: Uses `bwrap` (bubblewrap) for filesystem and namespace isolation
  (`crates/ava-sandbox/src/linux.rs`)
- **macOS**: Uses `sandbox-exec` with Seatbelt profiles
  (`crates/ava-sandbox/src/macos.rs`)

### SandboxPolicy

Defines what the sandboxed process can access:

```rust
pub struct SandboxPolicy {
    pub read_paths: Vec<PathBuf>,     // Readable paths
    pub write_paths: Vec<PathBuf>,    // Writable paths
    pub network: bool,                // Network access allowed
    pub env_passthrough: Vec<String>, // Environment variables to forward
}
```

### Integration

The sandbox middleware (priority 3) intercepts tool calls classified as
install-class commands and routes them through the sandbox backend. The
sandbox builds a `SandboxPlan` (the actual command + arguments to execute
via bwrap or sandbox-exec) and `execute_plan()` runs it.

## Key Files

| File | Role |
|---|---|
| `crates/ava-permissions/src/tags.rs` | `SafetyTag`, `RiskLevel`, `ToolSafetyProfile`, `core_tool_profiles()` |
| `crates/ava-permissions/src/policy.rs` | `PermissionPolicy` -- permissive, standard, strict |
| `crates/ava-permissions/src/classifier/mod.rs` | `classify_bash_command()`, command splitting, risk classification |
| `crates/ava-permissions/src/classifier/rules.rs` | Blocked/safe/high/medium patterns |
| `crates/ava-permissions/src/inspector.rs` | `DefaultInspector`, 9-step evaluation |
| `crates/ava-permissions/src/path_safety.rs` | File path risk analysis |
| `crates/ava-permissions/src/lib.rs` | `PermissionSystem`, `Rule`, `Pattern`, static/dynamic evaluation |
| `crates/ava-sandbox/src/lib.rs` | `SandboxBackend` trait, platform selection |
| `crates/ava-tui/src/widgets/tool_approval.rs` | Approval modal widget |
