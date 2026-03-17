# Streaming Edit Engine

> Status: Idea (not implemented)
> Source: Plandex
> Effort: High

## Summary
Incrementally applies edits as tokens stream from the model. The editor diffs accumulated buffer content against the original file using `similar::TextDiff`, finds anchored matching regions (>20 chars), and emits `EditChunk`s for changed spans. Enables real-time file updates during LLM streaming.

## Key Design Points
- `StreamingEditor` maintains original content, accumulated buffer, and processing positions
- `feed(chunk)` appends streamed tokens to the buffer
- `try_apply()` diffs buffer against original, finds anchored equal regions, emits edit chunks between anchors
- `finalize()` produces the complete edited file with addition/deletion statistics
- Minimum anchor length: 20 characters for reliable diff anchoring
- Char-to-byte offset conversion for correct Unicode handling
- `count_change_regions` counts distinct change regions in line-level diff

## Integration Notes
- Would replace the current whole-file-at-once edit application with incremental updates
- Requires streaming tool call content support from the LLM provider
- The existing `EditEngine` with 10 strategies handles non-streaming edits well
