# Streaming Edit Engine

> Status: Partially implemented (StreamingDiff phase 1)
> Source: Plandex
> Effort: High (full streaming), Low (phase 1 - post-completion diffs)

## Summary
Incrementally applies edits as tokens stream from the model. The editor diffs accumulated buffer content against the original file using `similar::TextDiff`, finds anchored matching regions (>20 chars), and emits `EditChunk`s for changed spans. Enables real-time file updates during LLM streaming.

## Phase 1: Post-Completion Diffs (Implemented)

The `StreamingDiffTracker` in `crates/ava-agent/src/streaming_diff.rs` provides:

- **File snapshotting**: captures file content before write/edit tool execution
- **Diff computation**: unified diff via `similar::TextDiff` after tool completes
- **Event emission**: `AgentEvent::DiffPreview` with file path, diff text, additions, and deletions
- **Agent loop integration**: write/edit/multiedit/apply_patch tools are tracked automatically
- **Tool result enrichment**: edit and write tools now include unified diffs in their results
- **UI support**: TUI event handler, headless text mode, and JSON mode all handle `DiffPreview`

### Key Types
- `StreamingDiffTracker` — snapshots files and computes diffs
- `PendingEdit` — tracks original content for a file being edited
- `DiffEvent` — `EditStarted`, `EditComplete`, `DiffPreview`
- `compute_unified_diff()` — public helper for any diff computation

## Phase 2: True Streaming Diffs (Future)

The original Plandex-inspired design for real-time token-by-token diff application:

- `StreamingEditor` maintains original content, accumulated buffer, and processing positions
- `feed(chunk)` appends streamed tokens to the buffer
- `try_apply()` diffs buffer against original, finds anchored equal regions, emits edit chunks between anchors
- `finalize()` produces the complete edited file with addition/deletion statistics
- Minimum anchor length: 20 characters for reliable diff anchoring
- Char-to-byte offset conversion for correct Unicode handling
- `count_change_regions` counts distinct change regions in line-level diff

### Integration Notes
- Would replace the current whole-file-at-once edit application with incremental updates
- Requires streaming tool call content support from the LLM provider
- The existing `EditEngine` with 10 strategies handles non-streaming edits well
