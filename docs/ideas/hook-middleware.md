# Hook Middleware

> Status: Idea (not implemented)
> Source: Gemini CLI
> Effort: Low

## Summary
Pre/post tool-use hooks that run shell scripts from `.ava/hooks/pre-tool/` and `.ava/hooks/post-tool/` directories. Pre-hook scripts returning exit code 1 cancel the tool call. Post-hooks are fire-and-forget.

## Key Design Points
- Scripts discovered from `.ava/hooks/pre-tool/*.sh` and `.ava/hooks/post-tool/*.sh`
- Scripts sorted alphabetically (e.g., `01_first.sh`, `02_second.sh`)
- Environment variables passed: `AVA_TOOL_NAME`, `AVA_TOOL_ARGS`, `AVA_HOOK_PHASE`
- Pre-hook exit code 1 cancels the tool call with stderr as the error message
- Post-hook errors are logged but don't fail the tool call
- Scripts run via `sh` with the tool arguments as JSON string

## Integration Notes
- Would register as middleware via `ToolRegistry::add_middleware()`
- Provides a user-extensible hook system without code changes
- The agent-level hooks module (also archived) covers lifecycle events; this covers per-tool hooks
