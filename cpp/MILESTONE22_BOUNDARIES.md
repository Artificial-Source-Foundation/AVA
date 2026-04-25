# C++ Milestone 22 Boundaries

M22 is a scoped edit-tool parity slice in `cpp/src/tools/core_tools.cpp`. It keeps the existing workspace/path safety and deterministic failure semantics while expanding non-`replace_all` matching beyond exact-only behavior and bounding adjacent read/edit/backup file work that the expanded path relies on.

## In Scope

1. Keep existing `edit` required args (`path`, `old_text`, `new_text`) and `replace_all` contract intact while adding file-size, output-size, and replacement-count bounds.
2. Extend the non-`replace_all` cascade with bounded deterministic strategies:
   - `exact_match`
   - `quote_normalized_exact_match`
   - `occurrence_match` (when `occurrence` is provided)
   - `line_number` (when `line_number` is provided)
   - `block_anchor` (when both `before_anchor` and `after_anchor` are provided)
   - `line_trimmed`
   - `auto_block_anchor`
   - `ellipsis`
   - `flexible_whitespace`
3. Add optional schema fields for `occurrence`, `line_number`, `before_anchor`, and `after_anchor`.
4. Keep deterministic no-match error behavior and no-write immutability on failure.
5. Add read/edit/backup preflight hardening for oversized files and symlinked backup directories.
6. Add focused `ava_tools_tests` coverage for the scoped M22 cascade, immutability constraints, and hardening boundaries.

## Out of Scope

1. Weighted fuzzy matching and score-based candidate ranking.
2. Regex strategy parity.
3. Token-boundary strategy parity.
4. Full relative-indentation rewrite parity.
5. Three-way merge recovery and diff-match-patch recovery.
6. Broad Rust edit-engine parity claims beyond this bounded cascade.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-configure cpp-debug
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 just cpp-test cpp-debug -R ava_tools_unit
git diff --check
```

## Follow-Up Green-Fix Notes

- Flexible-whitespace fallback now fails closed when more than one candidate block matches, matching the deterministic uniqueness behavior of the other broad cascade strategies.
- Explicit anchor locators now reject empty anchors before attempting a bounded region match.
- `replace_all` input-size checks now run after CRLF-aware normalization so normalized match/replacement strings stay inside the declared bounds.
- `occurrence` and `line_number` locator values now reject invalid JSON types and non-positive values with deterministic tool errors instead of leaking JSON conversion diagnostics.
- Focused tests now cover non-`replace_all` old-text size preflight, explicit `occurrence = 1`, invalid locator value diagnostics, block-anchor partial failure modes, broad-cascade ambiguity rejection, ellipsis failure paths, non-`replace_all` deletion, and `replace_all` replacement-count/output-size bounds.

## Decision Point

M22 partially lifts the C++ exact-only edit limitation for practical headless/TUI workflows, but advanced merge/fuzzy recovery parity remains intentionally deferred.
