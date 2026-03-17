# Model Routing Classifier

> Status: Idea (not implemented)
> Source: Gemini CLI
> Effort: Medium

## Summary
Multi-strategy model classifier for per-request model selection. Evaluates pluggable strategies in priority order (lower number = higher priority) to select the optimal model based on task complexity. Includes override, complexity-based, and fallback strategies.

## Key Design Points
- `RoutingStrategy` trait: `name()`, `priority()`, `route(context) -> Option<RoutingDecision>`
- `OverrideStrategy` (priority 0): manual override from `/model` command, always wins
- `ComplexityClassifier` (priority 50): buckets messages into Simple/Medium/Complex tiers
  - Simple: greetings, short questions (<100 chars), question-word patterns
  - Complex: >500 chars, keywords (refactor, architect, migrate, rewrite, comprehensive)
  - Medium: code-related keywords (fix, bug, add, create, test, etc.)
- `FallbackStrategy` (priority 100): returns current model as safety net
- `ClassifierRouter`: sorted strategy list, first `Some` wins

## Integration Notes
- Would integrate with the agent stack's model selection
- The existing `RoutingConfig` in ava-config handles simple routing; this adds heuristic classification
- Could reduce costs by routing simple queries to cheaper models automatically
