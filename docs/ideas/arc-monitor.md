# ARC Monitor Integration

> Status: Idea (not implemented)
> Source: ARC safety research
> Effort: Medium

## Summary
Interface for an external ARC Monitor safety evaluation service. Currently uses heuristic fallback (keyword matching) to classify actions as safe, needing confirmation, or needing redirection. Designed so a real HTTP backend can be swapped in later.

## Key Design Points
- Three outcomes: Ok (safe), Ask(reason) (needs confirmation), Steer(reason) (suggest different approach)
- Heuristic checks for critical patterns: `rm -rf /`, `sudo rm`, `format disk`, `drop database` trigger Steer
- Moderate patterns: `delete`, `remove`, `overwrite`, `force push` trigger Ask
- `endpoint` field reserved for future HTTP API integration
- Simple `evaluate(action, context)` interface combining action and context strings

## Integration Notes
- Would hook into the permission system before tool execution
- The existing `CommandClassifier` and `DefaultInspector` cover similar ground
- A real ARC Monitor API endpoint would need authentication and rate limiting
