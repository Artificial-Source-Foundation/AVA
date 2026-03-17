# Startup Context

> Status: Idea (not implemented)
> Source: Codex CLI
> Effort: Low

## Summary
Auto-injects recent work history and workspace directory tree into the first agent turn as background context. Builds a compact workspace tree (skipping noisy directories like node_modules, target, .git), collects recent session summaries, and formats everything into a character-limited startup message.

## Key Design Points
- Workspace tree builder walks up to `max_depth` levels, skipping 14 noisy directory names
- File counts shown inline with each directory entry
- Tree capped at 100 entries with truncation notice
- Session summaries include title, message count, and human-readable age ("30m ago", "3h ago")
- Total message capped at 2000 characters with line-boundary truncation
- Shell info: shell path, OS, arch, cwd

## Integration Notes
- Would inject into the system prompt or as a first-turn user message
- The `ava-codebase` indexer already provides deeper code analysis; this is a lighter-weight complement
- Could be configurable via `config.yaml` to enable/disable or adjust depth/limits
