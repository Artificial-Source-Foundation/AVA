# ava-permissions

> Permission and safety system -- risk classification, command analysis, path safety, policy enforcement, and audit logging.

**Crate path:** `crates/ava-permissions/`
**Primary modules:** `tags`, `classifier/`, `inspector`, `policy`, `path_safety`, `audit`

---

## Overview

The `ava-permissions` crate evaluates whether tool calls should be allowed, denied, or require user approval. It provides:

- **SafetyTag** -- 8 categories of safety concern
- **RiskLevel** -- 5-level risk classification (Safe through Critical)
- **CommandClassifier** -- classifies bash commands by risk using tree-sitter parsing and pattern matching
- **PathSafety** -- analyzes file path risk relative to workspace boundaries
- **PermissionPolicy** -- configurable policy modes (permissive/standard/strict)
- **DefaultInspector** -- 9-step permission evaluation pipeline
- **AuditLog** -- tracks all permission decisions for the session

The crate is consumed by `ava-tools` via `PermissionMiddleware`, which runs the inspector before every tool execution.

---

## SafetyTag

**File:** `crates/ava-permissions/src/tags.rs`, lines 5-15

8 safety tag types classify the kind of concern a tool operation raises:

```rust
pub enum SafetyTag {
    ReadOnly,             // No side effects (read, glob, grep)
    WriteFile,            // Creates or modifies files (write, edit, remember)
    DeleteFile,           // Removes files (rm)
    ExecuteCommand,       // Runs arbitrary commands (bash)
    NetworkAccess,        // Makes network requests (curl, wget, git push)
    SystemModification,   // Changes system state (chmod, kill)
    Destructive,          // Potentially irreversible (rm -rf, DROP TABLE)
    Privileged,           // Requires elevated access (sudo)
}
```

Tags are `Serialize`/`Deserialize` and can be stored in policy configs.

---

## RiskLevel

**File:** `crates/ava-permissions/src/tags.rs`, lines 17-24

5 ordered risk levels with `Ord` implementation for comparison:

```rust
pub enum RiskLevel {
    Safe,       // No side effects (read, glob, grep, diagnostics)
    Low,        // Minimal risk (write, edit, cargo test)
    Medium,     // Moderate risk (bash, rm single file, kill)
    High,       // Significant risk (rm -rf, curl, git push --force, DROP TABLE)
    Critical,   // Must be blocked (rm -rf /, sudo, fork bomb, dd)
}
```

`RiskLevel` implements `PartialOrd`/`Ord`, so `RiskLevel::Safe < RiskLevel::Critical`.

---

## ToolSafetyProfile

**File:** `crates/ava-permissions/src/tags.rs`, lines 26-48

Predefined safety metadata for each tool:

```rust
pub struct ToolSafetyProfile {
    pub tool_name: String,
    pub tags: HashSet<SafetyTag>,
    pub risk_level: RiskLevel,
    pub description: String,
}
```

### Core tool profiles

`core_tool_profiles()` returns 18 profiles (line 51-171):

| Tool | Risk Level | Tags |
|------|-----------|------|
| `read` | Safe | ReadOnly |
| `glob` | Safe | ReadOnly |
| `grep` | Safe | ReadOnly |
| `diagnostics` | Safe | ReadOnly |
| `codebase_search` | Safe | ReadOnly |
| `recall` | Safe | ReadOnly |
| `memory_search` | Safe | ReadOnly |
| `session_search` | Safe | ReadOnly |
| `session_list` | Safe | ReadOnly |
| `session_load` | Safe | ReadOnly |
| `remember` | Safe | WriteFile |
| `write` | Low | WriteFile |
| `edit` | Low | WriteFile |
| `multiedit` | Low | WriteFile |
| `test_runner` | Low | ExecuteCommand |
| `lint` | Low | ExecuteCommand |
| `apply_patch` | Medium | WriteFile |
| `bash` | Medium | ExecuteCommand |

---

## CommandClassifier

**File:** `crates/ava-permissions/src/classifier/mod.rs`

Classifies bash command strings into structured risk information.

### Entry point

```rust
pub fn classify_bash_command(command: &str) -> CommandClassification {
    // 1. Check whole-command blocked patterns (catches curl|sh, fork bombs)
    // 2. Split on pipes (|), chains (&&, ||), semicolons (;)
    // 3. Classify each part individually
    // 4. Return HIGHEST risk from all parts
}
```

### CommandClassification

```rust
pub struct CommandClassification {
    pub risk_level: RiskLevel,
    pub tags: Vec<SafetyTag>,
    pub warnings: Vec<String>,
    pub blocked: bool,
    pub reason: Option<String>,
}
```

### Classification pipeline (per command part)

**File:** `crates/ava-permissions/src/classifier/mod.rs`, lines 144-209

1. **Blocked patterns** (Critical, `blocked=true`) -- checked first
2. **Safe commands** (Safe, ReadOnly tag)
3. **Low-risk commands** (Low, ExecuteCommand tag) -- build/test tools
4. **High-risk patterns** (High, warns but doesn't block)
5. **Medium-risk patterns** (Medium)
6. **Network commands** (High, NetworkAccess tag)
7. **Default**: Medium risk for unrecognized commands

### Blocked patterns (Critical)

**File:** `crates/ava-permissions/src/classifier/rules.rs`, lines 6-59

These patterns are **always denied**, even in auto-approve mode:

| Pattern | Reason |
|---------|--------|
| `rm -rf /`, `rm -rf ~`, `rm -rf /*`, `rm -rf ~/*` | rm -rf on critical path |
| `sudo ...` | Requires elevated privileges |
| `curl ... \| sh`, `wget ... \| bash` | Piping downloaded content to shell |
| `dd if=...` | Can overwrite disk data |
| `mkfs...` | Will format a filesystem |
| `> /dev/...` | Writing to device files |
| `:(){ :\|:& };:` | Fork bomb |

### Safe commands (read-only)

**File:** `crates/ava-permissions/src/classifier/rules.rs`, lines 62-72

```
ls, cat, echo, grep, rg, find, head, tail, wc, pwd, date, which, whoami,
env, printenv, uname, id, file, stat, du, df, tree, less, more, sort,
uniq, diff, comm, cut, tr, basename, dirname, realpath, readlink, tee,
true, false, test, [, printf
```

### Safe git subcommands

**File:** `crates/ava-permissions/src/classifier/rules.rs`, lines 75-82

```
git status, git log, git diff, git branch, git show, git tag, git remote,
git stash list, git shortlog, git describe, git rev-parse, git ls-files, git blame
```

Exposed as `pub fn is_safe_git_command(lower: &str) -> bool` -- used by `git_read` tool.

### Low-risk commands (build/test tools)

**File:** `crates/ava-permissions/src/classifier/rules.rs`, lines 85-109

First word matches: `cargo`, `npm`, `npx`, `yarn`, `pnpm`, `bun`, `python`, `python3`, `node`, `deno`, `go`, `rustc`, `gcc`, `make`, `cmake`, `just`, `nix`

With safe subcommands: `test`, `build`, `clippy`, `check`, `run`, `install`, `fmt`, `lint`, `format`, `bench`, `doc`, `audit`, `outdated`

Plus standalone tools: `rustfmt`, `prettier`, `eslint`, `biome`, `tsc`, `esbuild`, `vite`, `webpack`

### High-risk patterns (warns, not blocked)

**File:** `crates/ava-permissions/src/classifier/rules.rs`, lines 112-207

| Pattern | Tags | Warning |
|---------|------|---------|
| `rm -rf {non-root}` | Destructive | "rm -rf can recursively delete files" |
| `git push --force` / `-f` | Destructive, NetworkAccess | "Force push can overwrite remote history" |
| `git reset --hard` | Destructive | "git reset --hard discards uncommitted changes" |
| `chmod 777` | SystemModification | "chmod 777 makes files world-writable" |
| `DROP TABLE` / `DROP DATABASE` | Destructive | "SQL DROP operation will permanently delete data" |
| `DELETE FROM ... (no WHERE)` | Destructive | "DELETE without WHERE clause affects all rows" |
| `TRUNCATE` | Destructive | "TRUNCATE will remove all data from the table" |

### Medium-risk patterns

**File:** `crates/ava-permissions/src/classifier/rules.rs`, lines 210-260

| Pattern | Tags |
|---------|------|
| `rm {file}` (no -rf) | DeleteFile |
| `kill -9` | SystemModification |
| `pkill` / `killall` | SystemModification |
| `git` (non-safe, non-high) | ExecuteCommand |

### Network commands (High)

**File:** `crates/ava-permissions/src/classifier/rules.rs`, lines 263-267

`curl`, `wget`, `nc`, `ncat`, `ssh`, `scp`, `rsync`, `ftp`, `sftp`, or any command containing `http://` or `https://`

### Command parsing

**File:** `crates/ava-permissions/src/classifier/parser.rs`

- **tree-sitter** (`extract_words_treesitter`): parses bash syntax tree, extracts `command_name` and `word` nodes
- **Heuristic fallback** (`extract_words_heuristic`): splits on whitespace, lowercases

### Chain/pipe splitting

**File:** `crates/ava-permissions/src/classifier/mod.rs`, lines 89-142

`split_command_parts()` splits on `|`, `||`, `&&`, `;` while respecting single/double quotes. Each part is classified independently; the highest risk wins.

---

## Path Safety

**File:** `crates/ava-permissions/src/path_safety.rs`

Analyzes the risk of file path access relative to the workspace.

```rust
pub fn analyze_path(path: &str, workspace_root: &Path) -> PathRisk
```

### PathRisk

```rust
pub struct PathRisk {
    pub risk_level: RiskLevel,
    pub outside_workspace: bool,
    pub system_path: bool,
    pub reason: Option<String>,
}
```

### Risk classification by path

| Path | Risk Level | Flags |
|------|-----------|-------|
| Inside workspace | Safe | `outside_workspace: false` |
| `/tmp/...` | Low | `outside_workspace: true` |
| Home directory (outside workspace) | Medium | `outside_workspace: true` |
| Other paths outside workspace | High | `outside_workspace: true` |
| System paths (`/etc`, `/usr`, `/bin`, `/sbin`, `/lib`, `/lib64`, `/boot`, `/sys`, `/proc`, `/var/run`) | Critical | `system_path: true` |
| Root `/` | Critical | `system_path: true` |

Relative paths are resolved against the workspace root. Parent traversal (`../../../etc/passwd`) is normalized and correctly classified.

---

## PermissionPolicy

**File:** `crates/ava-permissions/src/policy.rs`

Configurable policy that controls the risk threshold and tool blocking.

```rust
pub struct PermissionPolicy {
    pub name: String,
    pub max_risk_level: RiskLevel,     // Auto-allow up to this level
    pub blocked_tags: Vec<SafetyTag>,  // Always deny these tags
    pub allowed_tools: Vec<String>,    // Always allow these tools
    pub blocked_tools: Vec<String>,    // Always deny these tools
}
```

### Built-in policies

| Policy | `max_risk_level` | `blocked_tags` | Behavior |
|--------|-----------------|----------------|----------|
| `permissive()` | High | none | Allow everything except Critical |
| `standard()` | Low | Destructive | Auto-allow Safe+Low, ask for Medium+, deny Critical |
| `strict()` | Safe | Destructive, Privileged | Ask for everything except ReadOnly, deny Critical |

`PermissionPolicy` is `Serialize`/`Deserialize` for storage in config files.

---

## DefaultInspector

**File:** `crates/ava-permissions/src/inspector.rs`, lines 48-224

9-step permission evaluation pipeline:

```
Step 1: Bash command classification
        -> Blocked commands are DENIED regardless of auto-approve
        -> Risk level upgraded from classifier result

Step 2: File path safety analysis (read, write, edit, multiedit, apply_patch)
        -> System paths are DENIED regardless of auto-approve
        -> Outside-workspace paths upgrade risk level

Step 3: Auto-approve mode check
        -> If auto-approve enabled, ALLOW (blocked commands already caught)

Step 4: Session-approved tools
        -> If tool was approved for this session, ALLOW

Step 5: Policy blocked tools
        -> If tool is in blocked_tools list, DENY

Step 6: Policy allowed tools
        -> If tool is in allowed_tools list, ALLOW

Step 7: Policy blocked tags
        -> If any tool tag is in blocked_tags, DENY

Step 8: Risk level vs policy threshold
        -> If risk <= max_risk_level, ALLOW

Step 9: Static/dynamic rule evaluation
        -> Falls through to PermissionSystem.evaluate() for pattern/glob/regex rules
```

### InspectionResult

```rust
pub struct InspectionResult {
    pub action: Action,           // Allow, Deny, or Ask
    pub reason: String,           // Human-readable explanation
    pub risk_level: RiskLevel,    // Computed risk level
    pub tags: Vec<SafetyTag>,     // Applicable safety tags
    pub warnings: Vec<String>,    // Warning messages for the user
}
```

### InspectionContext

```rust
pub struct InspectionContext {
    pub workspace_root: PathBuf,
    pub auto_approve: bool,
    pub session_approved: HashSet<String>,
    pub safety_profiles: HashMap<String, ToolSafetyProfile>,
}
```

### PermissionInspector trait

```rust
pub trait PermissionInspector: Send + Sync {
    fn inspect(&self, tool_name: &str, arguments: &Value, context: &InspectionContext) -> InspectionResult;
}
```

---

## PermissionSystem (Rule-based)

**File:** `crates/ava-permissions/src/lib.rs`, lines 43-117

The `PermissionSystem` provides static rule matching and dynamic safety checks.

```rust
pub struct PermissionSystem {
    workspace_root: PathBuf,
    rules: Vec<Rule>,
}
```

### Rules

```rust
pub struct Rule {
    pub tool: Pattern,    // Match tool name
    pub args: Pattern,    // Match arguments
    pub action: Action,   // Allow, Deny, or Ask
}

pub enum Pattern {
    Any,              // Matches everything
    Glob(String),     // Glob pattern matching
    Regex(String),    // Regex pattern matching
    Path(String),     // Exact path comparison (normalized)
}
```

### Evaluation

`evaluate(tool, args)` combines static rule matching with dynamic checks:

1. **Static rules**: first matching rule's action is used (default: `Ask`)
2. **Dynamic checks**:
   - Null bytes in args -> `Deny`
   - Out-of-workspace paths -> `Ask`
   - Bash commands -> classified via `CommandClassifier`, blocked -> `Deny`, High+ -> `Ask`
   - Network tools (`webfetch`, `websearch`, `curl`, `wget`) -> `Ask`
3. **Combined**: most restrictive of static and dynamic result wins (`Deny > Ask > Allow`)

---

## Audit Log

**File:** `crates/ava-permissions/src/audit.rs`

Records every permission decision for the session.

### AuditEntry

```rust
pub struct AuditEntry {
    pub timestamp: DateTime<Utc>,
    pub tool_name: String,
    pub arguments_summary: String,   // Truncated to 200 chars
    pub risk_level: RiskLevel,
    pub tags: Vec<SafetyTag>,
    pub decision: AuditDecision,
}
```

### AuditDecision

```rust
pub enum AuditDecision {
    AutoApproved,
    UserApproved,
    UserDenied,
    Blocked,
    SessionApproved,
}
```

### AuditLog

- `record()` -- adds an entry (max 1000 entries, FIFO eviction)
- `recent(n)` -- returns the last N entries
- `summary()` -- returns `AuditSummary` with counts per decision type
- Arguments are truncated to 200 characters
