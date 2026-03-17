# Safety Checker Registry

> Status: Idea (not implemented)
> Source: Original
> Effort: Low

## Summary
A composable registry of safety checkers for tool invocations. Each checker implements a simple trait and returns Allow, Deny, or Ask. The registry evaluates all checkers on each tool call with short-circuit semantics (any Deny wins immediately).

## Key Design Points
- `SafetyChecker` trait: `name()` + `check(tool_name, args) -> CheckResult`
- `PathChecker`: validates file paths stay within workspace, handles path traversal (../) normalization
- `CommandChecker`: blocks dangerous bash commands against a configurable blocklist (rm -rf /, mkfs, fork bomb, etc.)
- `CheckerRegistry`: runs all checkers, Deny > Ask > Allow priority
- Path checker covers 7 file-operating tools, checks path/file_path/target/destination keys

## Integration Notes
- Would provide an alternative to the current `DefaultInspector` in ava-permissions
- The existing SafetyTag + RiskLevel + CommandClassifier system covers similar ground
- Could be useful as a more modular/composable version of the current permission system
