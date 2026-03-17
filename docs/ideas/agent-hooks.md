# Agent Lifecycle Hooks

> Status: Idea (not implemented)
> Source: Gemini CLI
> Effort: Medium

## Summary
A configurable hook infrastructure that fires at key agent lifecycle points (session start/end, before/after model calls, before/after tool execution). Hooks can observe, block, or modify agent behavior. Includes a `ShellHook` implementation that runs shell commands with exit-code-based outcome mapping.

## Key Design Points
- Six lifecycle events: SessionStart, BeforeModel, AfterModel, BeforeToolExecution, AfterToolExecution, SessionEnd
- Three outcomes: Continue, Block(reason), Modify(message)
- `HookRunner` runs all applicable hooks in parallel via tokio::spawn
- Priority-based aggregation: Block > Modify > Continue; multiple Modify messages concatenated
- `ShellHook` maps exit codes: 0=Continue, 1=Block (stderr as reason), 2=Modify (stdout as message)
- Environment variables passed to shell hooks: `AVA_HOOK_EVENT`, `AVA_HOOK_TOOL_NAME`, `AVA_HOOK_TOOL_ID`
- Erroring hooks treated as Continue (fail-open)

## Integration Notes
- Would wire into the agent loop at each lifecycle point
- Note: `ava-tui` has its own hooks system that IS used (different from this)
- Shell hooks would live in project directories (e.g., `.ava/hooks/`)
- Could complement the existing middleware system with user-configurable behavior
