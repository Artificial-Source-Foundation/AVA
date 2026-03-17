# Skill Hot Reload

> Status: Idea (not implemented)
> Source: Original
> Effort: Low

## Summary
Polls directories for changed TOML custom tool files using filesystem modification times. Detects new, modified, and (implicitly) removed tool definitions without requiring a restart.

## Key Design Points
- `SkillWatcher` tracks watched directories and last-seen modification times per file
- Polling-based (no `notify` crate dependency)
- First scan reports all existing TOML files as "changed" (initial registration)
- Subsequent scans only report files whose modification time has advanced
- Recursive directory scanning for nested TOML files
- Non-TOML files ignored

## Integration Notes
- Would run on a periodic timer in the agent loop or TUI event loop
- Would trigger `register_custom_tools()` re-registration on detected changes
- The existing custom tool system loads tools at startup but doesn't watch for changes
