# AI Comment Watcher

> Status: Idea (not implemented)
> Source: Original
> Effort: Low

## Summary
Scans source files for special `// ai` and `# ai` comment prefixes that signal the developer wants the agent to pay attention to a specific location. Supports normal and urgent (`// ai!`) priorities. Can scan individual files or recursively scan directories filtered by file extension.

## Key Design Points
- `AiComment` with line number, comment text, and urgency (Normal/Urgent)
- Six prefix patterns: `// ai!`, `# ai!`, `/* ai!` (urgent), `// ai`, `# ai`, `/* ai` (normal)
- Case-insensitive matching on the trimmed, lowercased line
- `scan_directory` recursively walks directories filtered by extension list
- Urgent comments checked before normal to ensure correct priority assignment

## Integration Notes
- Could run at session start to auto-populate the agent's context with developer-flagged areas
- Would integrate with the startup context system or as a pre-session hook
- File watcher (e.g., notify crate) could make this reactive rather than poll-based
