# History Processors Pipeline

> Status: Idea (not implemented)
> Source: SWE-Agent
> Effort: Low

## Summary
A pluggable pipeline of processors that transform conversation history before sending to the model. Each processor can truncate, filter, or annotate messages. Processors are applied in registration order.

## Key Design Points
- `HistoryProcessor` trait: `name()` + `process(Vec<Message>) -> Vec<Message>`
- `HistoryPipeline` chains processors sequentially
- Four built-in processors:
  - `TruncateObservations`: caps tool result content at max_chars with truncation notice
  - `FilterContent`: keeps only specified roles (e.g., drop System messages)
  - `RemoveEmpty`: drops messages with no content, tool calls, or results
  - `KeepRecent`: retains only the last N messages

## Integration Notes
- Would run in the agent loop before each model call
- The existing `prune_old_tool_outputs` and condensation strategies cover some of this
- Could be user-configurable via config.yaml for custom processing pipelines
