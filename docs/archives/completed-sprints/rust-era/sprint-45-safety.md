# Sprint 45: Pre-Execution Validation & Safety

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Make AVA proactively safe. Instead of just asking "approve this tool call?", AVA should detect dangerous operations BEFORE executing them and explain the risk. After this sprint, AVA catches `rm -rf /`, dangerous git operations, and destructive database queries before they run.

## Key Files to Read

```
crates/ava-permissions/src/lib.rs          # Module structure
crates/ava-permissions/src/inspector.rs    # DefaultInspector (9-step flow)
crates/ava-permissions/src/tags.rs         # SafetyTag (8 variants), RiskLevel (5 levels), core_tool_profiles()
crates/ava-permissions/src/policy.rs       # PermissionPolicy (3 presets)
crates/ava-permissions/Cargo.toml

crates/ava-tools/src/core/bash.rs          # Bash tool — primary target for safety
crates/ava-tools/src/permission_middleware.rs  # Existing permission middleware
crates/ava-tools/src/registry.rs           # Middleware trait

crates/ava-agent/src/loop.rs               # Tool execution flow
crates/ava-tui/src/widgets/tool_approval.rs  # Approval UI
```

## What Already Exists

- **SafetyTag**: ReadOnly, WriteFile, DeleteFile, ExecuteCommand, NetworkAccess, SystemModification, Destructive, Privileged
- **RiskLevel**: Safe, Low, Medium, High, Critical
- **DefaultInspector**: 9-step inspection (yolo → session_approved → safety_profile → bash_classify → policy)
- **PermissionPolicy**: 3 presets (permissive, standard, strict)
- **core_tool_profiles()**: Maps 6 core tools to safety profiles
- **ToolApproval widget**: Modal for approve/reject

## Theme 1: Command Classification

### Story 1.1: Bash Command Classifier

Build a classifier that analyzes bash commands and assigns risk levels BEFORE execution.

**Implementation:**
- File: `crates/ava-permissions/src/classifier.rs` (NEW)
- `classify_command(command: &str) -> CommandClassification`

```rust
pub struct CommandClassification {
    pub risk_level: RiskLevel,
    pub tags: Vec<SafetyTag>,
    pub warnings: Vec<String>,
    pub blocked: bool,
    pub reason: Option<String>,
}
```

**Classification rules:**

| Pattern | Risk | Tags | Action |
|---------|------|------|--------|
| `rm -rf /` or `rm -rf ~` | Critical | Destructive | BLOCK |
| `rm -rf` (other paths) | High | Destructive, DeleteFile | Warn |
| `rm` (single file) | Medium | DeleteFile | Allow with approval |
| `git push --force` | High | Destructive | Warn |
| `git reset --hard` | High | Destructive | Warn |
| `chmod 777` | High | SystemModification | Warn |
| `sudo *` | Critical | Privileged | BLOCK |
| `curl * \| sh` | Critical | NetworkAccess, ExecuteCommand | BLOCK |
| `dd if=*` | Critical | Destructive | BLOCK |
| `mkfs.*` | Critical | Destructive | BLOCK |
| `> /dev/*` | Critical | Destructive | BLOCK |
| `DROP TABLE`, `DELETE FROM` (no WHERE) | High | Destructive | Warn |
| `kill -9` | Medium | SystemModification | Allow with approval |
| Read-only commands (ls, cat, echo, grep, find) | Safe | ReadOnly | Allow |
| `git status/log/diff/branch` | Safe | ReadOnly | Allow |
| `cargo test/build/clippy` | Low | ExecuteCommand | Allow |
| `npm test/build/install` | Low | ExecuteCommand | Allow |

**Implementation approach:**
- Parse command into parts (handle pipes, &&, ;)
- Check each part against patterns
- Return the HIGHEST risk from all parts
- Use regex patterns for flexibility

**Acceptance criteria:**
- Classifies 15+ command patterns correctly
- Handles pipes and chained commands
- Returns structured classification
- Blocked commands can never execute
- Add tests for each pattern category

### Story 1.2: Path Safety Analysis

Detect when a tool operates on dangerous paths.

**Implementation:**
- File: `crates/ava-permissions/src/path_safety.rs` (NEW)

```rust
pub fn analyze_path(path: &str, workspace_root: &Path) -> PathRisk {
    // ...
}

pub struct PathRisk {
    pub risk_level: RiskLevel,
    pub outside_workspace: bool,
    pub system_path: bool,
    pub reason: Option<String>,
}
```

**Rules:**
| Path | Risk |
|------|------|
| Inside workspace | Safe |
| `/tmp/*` | Low |
| Home directory files | Medium |
| Outside workspace (other) | High |
| System paths (`/etc/`, `/usr/`, `/bin/`) | Critical (BLOCK) |
| Root `/` | Critical (BLOCK) |

**Integration:**
- Call from write, edit, delete, and bash tools
- The `DefaultInspector` checks path risk as part of inspection

**Acceptance criteria:**
- Workspace-relative paths are safe
- System paths are blocked
- Outside-workspace paths get elevated risk
- Add tests

### Story 1.3: Integrate Classifier into Inspector

Wire the command classifier and path safety into the existing `DefaultInspector` flow.

**Implementation:**
- In `DefaultInspector::inspect()`, after step 4 (bash_classify):
  - For bash tools: run `classify_command()` on the command argument
  - For file tools (read, write, edit): run `analyze_path()` on the path argument
  - Merge classification results into the inspection result
  - If `blocked == true`, return `Action::Deny` with the reason

**Acceptance criteria:**
- Dangerous commands blocked before execution
- Dangerous paths blocked before execution
- Warnings shown in approval dialog
- Yolo mode still skips approval but NOT blocking (Critical stays blocked even in yolo)
- Add integration test: blocked command in yolo mode still blocked

## Theme 2: Enhanced Approval UI

### Story 2.1: Risk-Aware Approval Dialog

Upgrade the tool approval dialog to show risk information.

**Current state:** Simple approve/reject modal.

**New state:**
```
┌─ Tool Approval ──────────────────────────┐
│                                          │
│  Tool: bash                              │
│  Command: rm -rf ./build/                │
│                                          │
│  ⚠ Risk: HIGH                           │
│  Tags: Destructive, DeleteFile           │
│  Warning: Recursively deletes directory  │
│                                          │
│  [y] Approve  [n] Reject  [a] Always    │
└──────────────────────────────────────────┘
```

**Implementation:**
- Extend `ToolApproval` widget to receive `InspectionResult`
- Show risk level with color coding (Safe=green, Low=blue, Medium=yellow, High=orange, Critical=red)
- Show safety tags as badges
- Show warnings from classifier

**Acceptance criteria:**
- Risk level shown with color
- Safety tags displayed
- Warnings shown when present
- Critical risk has distinct scary styling
- Blocked tools show "BLOCKED" instead of approve/reject

### Story 2.2: Audit Log

Log all tool executions with their risk levels and approval decisions.

**Implementation:**
- File: `crates/ava-permissions/src/audit.rs` (NEW)
- `AuditLog` struct backed by a Vec (in-memory for now)
- Records: timestamp, tool_name, arguments (truncated), risk_level, decision (approved/denied/blocked), user

```rust
pub struct AuditEntry {
    pub timestamp: chrono::DateTime<chrono::Utc>,
    pub tool_name: String,
    pub arguments_summary: String,
    pub risk_level: RiskLevel,
    pub tags: Vec<SafetyTag>,
    pub decision: AuditDecision,
}

pub enum AuditDecision {
    AutoApproved,   // yolo or Safe risk
    UserApproved,
    UserDenied,
    Blocked,        // classifier blocked
    SessionApproved,
}
```

**Integration:**
- Record entries in the permission middleware (after each inspect/execute)
- Add `/audit` command to TUI palette — shows recent entries

**Acceptance criteria:**
- All tool executions logged
- Decision types tracked
- `/audit` command shows last N entries
- Add tests

## Theme 3: Safety Profiles for New Tools

### Story 3.1: Extended Tool Safety Profiles

Add safety profiles for the new tools from Sprint 37 and 41.

**New profiles to add in `core_tool_profiles()`:**

| Tool | Tags | Risk Level |
|------|------|-----------|
| multiedit | WriteFile | Low |
| apply_patch | WriteFile | Medium |
| test_runner | ExecuteCommand | Low |
| lint | ExecuteCommand | Low |
| diagnostics | ReadOnly | Safe |
| codebase_search | ReadOnly | Safe |
| remember | WriteFile | Safe |
| recall | ReadOnly | Safe |
| memory_search | ReadOnly | Safe |
| session_search | ReadOnly | Safe |
| session_list | ReadOnly | Safe |
| session_load | ReadOnly | Safe |

**Acceptance criteria:**
- All new tools have safety profiles
- Profiles match expected risk levels
- Add test verifying all registered tools have profiles

## Implementation Order

1. Story 1.1 (command classifier) — core safety logic
2. Story 1.2 (path safety) — complements classifier
3. Story 1.3 (integrate into inspector) — wires everything together
4. Story 3.1 (extended profiles) — quick, covers new tools
5. Story 2.1 (risk-aware approval UI) — visible improvement
6. Story 2.2 (audit log) — accountability
7. Story 2.3 and tests

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- NEVER allow Critical-risk operations even in yolo mode
- Don't break existing permission system
- Classifier patterns should be maintainable (not a giant regex)
- Audit log is in-memory only (no disk persistence this sprint)

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
cargo test -p ava-permissions -- --nocapture

# Manual safety check
cargo run --bin ava -- "Delete all files in /" --headless --yolo --provider openrouter --model anthropic/claude-sonnet-4
# Should BLOCK even with --yolo
```
