# Lead-Worker Provider

> Status: Idea (not implemented)
> Source: Original
> Effort: Medium

## Summary
A provider wrapper that routes the first N turns to an expensive "lead" model, then switches to a cheaper "worker" model. If the worker fails consecutively, the provider automatically promotes back to the lead for another N turns. Optimizes cost by using frontier models only for initial planning.

## Key Design Points
- Atomic counters for turn tracking, failure counting, and lead/worker state
- Configurable: lead turns (default 3), failure threshold (default 2)
- Auto-promotion: consecutive worker failures >= threshold triggers lead phase restart
- Worker success resets the consecutive failure counter
- Implements full `LLMProvider` trait including streaming, tool use, and thinking variants
- `model_name()` and `estimate_cost()` delegate to the currently active provider

## Integration Notes
- Would wrap two provider instances and present as a single provider to the agent
- The existing `FallbackConfig` in ava-config handles provider failover but not turn-based switching
- Could be configured via `config.yaml` with lead/worker model specifications
