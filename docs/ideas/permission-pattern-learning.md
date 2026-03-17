# Permission Pattern Learning

> Status: Idea (not implemented)
> Source: Original
> Effort: Medium

## Summary
Extracts structural patterns from shell commands so the permission system can learn which commands the user regularly approves and auto-approve similar future commands. Supports subcommand-aware programs (cargo, npm, git, docker, etc.) and Python module patterns.

## Key Design Points
- `CommandPattern` with program name, optional subcommand, and generated regex
- 20 subcommand-aware programs: cargo, npm, npx, yarn, git, docker, kubectl, go, pip, etc.
- Python `-m module` pattern support (e.g., `python -m pytest`)
- `PatternStore` for accumulating approved patterns with deduplication by (program, subcommand)
- Shell word splitting respecting single/double quotes and backslash escaping
- Pattern matching: `cargo test --workspace` approval covers all `cargo test` variants

## Integration Notes
- Would integrate with the permission inspector to remember user approval decisions
- The `persistent.rs` module (which IS wired) may overlap with this functionality
- Patterns could be persisted to `.ava/approved_patterns.json` across sessions
