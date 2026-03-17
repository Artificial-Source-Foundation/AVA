# Reviewer Retry Loop

> Status: Idea (not implemented)
> Source: SWE-Agent
> Effort: Medium

## Summary
Runs an agent task multiple times, uses an LLM judge (or heuristic fallback) to score each solution attempt, and selects the best one. Useful for hard tasks where the first attempt may not be optimal. Supports early exit when a score exceeds the acceptance threshold.

## Key Design Points
- `ScoredSolution` pairs messages with a score (0.0-1.0), rationale, and attempt number
- `ReviewerConfig` controls max attempts (default 3) and acceptance threshold (default 0.85)
- `SolutionReviewer` trait for pluggable scoring (LLM-based or heuristic)
- `HeuristicReviewer` scores based on tool call count, error count, response substantiveness, and completion
- Early exit on first attempt exceeding threshold
- Best-scoring solution returned when all attempts complete

## Integration Notes
- Would wrap the agent loop's `run()` call, running it multiple times
- Needs a way to reset state between attempts (fresh tool execution context)
- Cost implications: N attempts * full agent run cost + N review calls
