# Build Race

> Status: Idea (not implemented)
> Source: Plandex
> Effort: Medium

## Summary
Run parallel competing build/edit strategies concurrently and use the first valid result. Losers are cancelled. Useful for racing a fast approximate approach against a thorough whole-file rewrite, returning whichever succeeds first.

## Key Design Points
- `BuildResult` captures strategy name, content, success/failure, error, and duration
- `race_builds` spawns all strategies as tokio tasks, returns first successful result, aborts remaining
- `race_fast_vs_thorough` helper for the common two-strategy case
- Failed attempts are tracked; if no strategy succeeds, the best failed attempt is returned
- Cancellation via tokio task abort

## Integration Notes
- Would plug into the edit pipeline as an alternative to single-strategy edit application
- Could be used with streaming edit vs. whole-file rewrite as the two competing strategies
- Needs careful handling of file system side effects (only the winner's changes should persist)
