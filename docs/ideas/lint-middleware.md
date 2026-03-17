# Lint Middleware

> Status: Idea (not implemented)
> Source: Original (developer workflow)
> Effort: Medium

## Summary
Post-edit middleware that runs configurable lint commands on modified files after write/edit/apply_patch/multiedit tool calls, appending diagnostics to the tool result. Gives the agent immediate feedback to self-correct errors.

## Key Design Points
- `LintRule` matches file extensions and provides a command template with `{file}` substitution
- Default rules for 6 languages: Rust (rustfmt --check), Python (py_compile), JS (node --check), JSON, TOML, YAML
- Lint output truncated to 2000 chars to avoid flooding context
- Only triggers for edit tools (write, edit, apply_patch, multiedit)
- Diagnostics appended to tool result content with warning prefix
- File path extraction handles different tool argument shapes (including multiedit arrays)

## Integration Notes
- Would register as middleware via `ToolRegistry::add_middleware()`
- The existing extended `diagnostics` tool provides similar LSP-based validation
- Could complement the diagnostics tool with simpler, faster syntax checks
