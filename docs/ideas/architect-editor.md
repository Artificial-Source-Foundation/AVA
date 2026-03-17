# Architect-Editor Two-Phase Coding

> Status: Idea (not implemented)
> Source: Aider
> Effort: Medium

## Summary
A two-phase coding pattern where an "architect" model plans changes in natural language (producing a structured JSON plan with file paths, actions, and edit descriptions), then an "editor" model applies the edits by executing the corresponding tool calls. This separates planning from execution for potentially higher quality edits.

## Key Design Points
- Data types: `ArchitectPlan`, `EditStep`, `EditAction` (Create/Modify/Delete)
- `parse_architect_plan` extracts a plan from freeform LLM output (tries JSON code block, then raw JSON, then fallback single-step)
- `plan_to_tool_calls` converts a plan into executable `ToolCall`s (Modify -> edit, Create -> write, Delete -> bash rm)
- Configurable system prompt template for the architect phase
- Shell-safe path escaping for delete operations

## Integration Notes
- Would need to be wired into the agent loop as an alternative execution mode
- Requires two LLM provider instances (architect + editor) or could reuse the same provider
- The Praxis multi-agent system may already cover this use case with director/worker patterns
