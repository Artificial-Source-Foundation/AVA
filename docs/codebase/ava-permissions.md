# ava-permissions

> Permission system with rule evaluation and risk classification.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `PermissionSystem` | Core permission evaluator with rules and workspace root |
| `PermissionSystem::load()` | Create with workspace root and rule set |
| `PermissionSystem::evaluate()` | Check tool+args against rules and dynamic checks |
| `Action` | Enum: Allow, Deny, Ask |
| `Rule` | Pattern-based rule with tool pattern, args pattern, action |
| `Pattern` | Enum: Any, Glob, Regex, Path |
| `PermissionInspector` | Trait for tool call inspection with risk metadata |
| `DefaultInspector` | 9-step inspector with bash classification, path safety, policies |
| `InspectionResult` | Action, reason, risk level, tags, warnings |
| `InspectionContext` | Workspace, auto-approve, session approvals, persistent rules, profiles |
| `ToolSource` | Enum: BuiltIn, MCP {server}, Custom {path} |
| `PermissionPolicy` | Risk level threshold, blocked tags, allowed/blocked tools |
| `PermissionPolicy::permissive()` | Max risk: High |
| `PermissionPolicy::standard()` | Max risk: Medium (default) |
| `PermissionPolicy::strict()` | Max risk: Safe, blocks Destructive/Privileged |
| `PersistentRules` | User-global rules stored in `~/.ava/permissions.toml` |
| `PersistentRules::load()` | Load from home directory |
| `PersistentRules::load_project()` | Load project-local blocklists only |
| `PersistentRules::load_merged()` | Combine global allows + project blocks |
| `classify_bash_command()` | Blocklist classifier for bash commands |
| `CommandClassification` | Risk level, tags, warnings, blocked flag |
| `PathRisk` | Risk assessment for file paths |
| `analyze_path()` | Check if path is inside workspace, system path, etc. |
| `RiskLevel` | Enum: Safe, Low, Medium, High, Critical (ordered) |
| `SafetyTag` | Enum: ReadOnly, WriteFile, NetworkAccess, Destructive, etc. |
| `ToolSafetyProfile` | Risk level and tags for a tool |
| `core_tool_profiles()` | Returns HashMap of 18 built-in tool profiles |
| `AuditLog` | Circular buffer of permission decisions |
| `AuditEntry` | Timestamp, tool, arguments, risk, tags, decision |
| `AuditDecision` | Enum: AutoApproved, UserApproved, UserDenied, Blocked, SessionApproved |
| `validate_outbound_url()` | Blocks localhost, private IPs, metadata endpoints |
| `resolve_public_socket_addrs()` | Async DNS resolution with safety checks |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Core PermissionSystem, Rule/Pattern types, pattern matching |
| `inspector.rs` | DefaultInspector 9-step inspection logic (941 lines) |
| `policy.rs` | PermissionPolicy with permissive/standard/strict presets |
| `persistent.rs` | PersistentRules with user-global and project-local loading |
| `classifier/mod.rs` | Bash command classification orchestration |
| `classifier/rules.rs` | Blocklist patterns (blocked and high-risk) |
| `classifier/parser.rs` | Tree-sitter and heuristic word extraction |
| `path_safety.rs` | Path risk analysis (workspace bounds, system paths) |
| `tags.rs` | RiskLevel, SafetyTag, ToolSafetyProfile definitions |
| `audit.rs` | AuditLog for tracking permission decisions |
| `outbound.rs` | Outbound URL validation (SSRF protection) |

## Dependencies

Uses: None (no internal AVA crate dependencies)

Used by:
- `ava-tui` — Permission checks in CLI/TUI
- `ava-tools` — Tool permission validation
- `ava-agent` — Agent permission enforcement
- `src-tauri` — Desktop permission integration

## Key Patterns

- **Blocklist classifier**: Commands default to Low risk; only dangerous patterns flagged as High/Critical
- **9-step inspection**: (0) Built-in check → (1) Bash classification → (2) Path safety → (2b) Workspace check → (3) Auto-approve → (4) Session approval → (5-7) Policy checks → (8) Risk threshold → (9) Static rules
- **Fail-closed**: Tool source must be EXPLICITLY BuiltIn to auto-approve internal tools
- **Security**: Project-local rules can only RESTRICT (block), never expand permissions
- **Persistent rules**: Stored in `~/.ava/permissions.toml` (user-global), merged with project blocklists
- **SSRF protection**: Blocks localhost, private ranges, cloud metadata endpoints
- **Path safety**: System paths (/etc, /usr, etc.) are Critical risk; /tmp is Low; home is Medium
- **Bash parsing**: Uses tree-sitter with heuristic fallback; handles pipes and chains
