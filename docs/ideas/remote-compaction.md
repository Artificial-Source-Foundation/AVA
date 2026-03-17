# Remote Compaction

> Status: Idea (not implemented)
> Source: Codex CLI
> Effort: Medium

## Summary
Delegates context compaction to a remote LLM endpoint instead of local summarization. When context pressure is detected, sends the conversation history prefix to a summarizer and receives a compacted summary back. Falls back to pass-through on API failure.

## Key Design Points
- Splits messages into compactable prefix and protected recent suffix
- System messages preserved separately from compactable content
- Messages serialized to compact text format: `[role]: content`, `[tool_call]: name(id)`, `[tool_result]: preview`
- Tool results truncated to 200 chars in the serialization
- Maximum payload size: 100K chars (truncated with notice)
- Summary injected as a system message: `[Conversation summary]: ...`
- Graceful fallback: returns messages unchanged on API error

## Integration Notes
- Would plug into the existing `HybridCondenser` as an additional strategy
- Requires a `Summarizer` implementation backed by an LLM provider
- The existing local condensation strategies may be sufficient for most use cases
