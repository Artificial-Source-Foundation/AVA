# Read-Only Permission Judge

> Status: Idea (not implemented)
> Source: Original
> Effort: Low

## Summary
Heuristic classifier that determines whether a tool call is read-only or a write operation, enabling automatic approval of safe operations without user confirmation. Categorizes built-in tools by name and classifies bash commands against allow/deny pattern lists.

## Key Design Points
- Three results: ReadOnly, WriteOperation, Unknown
- Read-only tools: read, glob, grep, codebase_search, todo_read, diagnostics
- Write tools: write, edit, apply_patch, multiedit, todo_write
- 43 read-only bash commands (ls, cat, grep, git status, cargo test, etc.)
- 28 write-indicative bash patterns (rm, mv, chmod, git push, sudo, etc.)
- Write patterns checked first (higher priority) before read-only commands

## Integration Notes
- Would integrate with the permission system to auto-approve read-only operations
- The existing `PermissionLevel::AutoApprove` mode partially covers this use case
- Could be combined with the Guardian subagent for a layered approval system
