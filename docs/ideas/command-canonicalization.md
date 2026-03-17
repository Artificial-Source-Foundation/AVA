# Command Canonicalization

> Status: Idea (not implemented)
> Source: Codex CLI
> Effort: Low

## Summary
Normalizes shell commands for approval caching so that equivalent invocations (with different wrappers, env vars, or binary paths) map to the same canonical form. Enables "approve once, apply everywhere" for semantically identical commands.

## Key Design Points
- Shell wrapper stripping: `bash -c "cargo test"` canonicalizes to `["cargo", "test"]`; handles nested wrappers
- Env var prefix removal: `RUST_LOG=debug cargo test` becomes `["cargo", "test"]`
- Binary path normalization: `/usr/bin/cargo` becomes `cargo` (8 known bin prefixes)
- Pipe and chain operators preserved as tokens: `|`, `&&`, `||`, `;`
- Tokenizer handles single/double quoting and backslash escaping
- `commands_equivalent(a, b)` and `canonical_key(cmd)` for lookup

## Integration Notes
- Would plug into the permission approval cache for faster re-approval
- Could be combined with the pattern learning system for a two-layer approval strategy
- The existing `DefaultInspector` does not currently canonicalize commands
