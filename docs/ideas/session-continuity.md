# Session Continuity for Sub-Agents

> Status: Idea (not implemented)
> Source: Zed
> Effort: Medium

## Summary
A registry of tracked sub-agent sessions that support follow-up messages without re-sending the full conversation context. Each session accumulates messages and can be completed, followed up on, or cleaned up based on age.

## Key Design Points
- `AgentSession` with UUID, message history, goal, active flag, and timestamps
- `SessionRegistry` backed by `Arc<RwLock<HashMap>>` for concurrent access
- `follow_up` appends a user message and reactivates a completed session
- `cleanup` removes old inactive sessions past a configurable max age
- `list_active` filters to only currently-active sessions

## Integration Notes
- Would integrate with the Praxis multi-agent system for persistent worker sessions
- The existing `ava-session` crate handles SQLite persistence; this would add in-memory session tracking
- Could enable "continue from where you left off" workflows for sub-agent tasks
