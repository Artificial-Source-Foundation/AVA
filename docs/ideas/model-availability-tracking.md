# Model Availability Tracking

> Status: Idea (not implemented)
> Source: Original
> Effort: Low

## Summary
Per-model health tracking based on success/failure patterns with automatic degradation and recovery. Three failures in 60 seconds triggers Degraded status; five triggers Unavailable. Auto-recovery to Available after 120 seconds with no failures. Includes a `FallbackChain` that selects the first available or degraded model from an ordered list.

## Key Design Points
- Three statuses: Available, Degraded(reason), Unavailable(reason)
- Failure window: 60 seconds (old failures pruned)
- Recovery period: 120 seconds of no failures auto-recovers
- `record_success` immediately resets all failure state
- `FallbackChain` prefers Available models, falls back to Degraded, returns None if all Unavailable
- Thread-safe via `Mutex<HashMap>`
- Independent tracking per model name

## Integration Notes
- Would complement the existing `CircuitBreaker` (which operates at the provider level, not model level)
- The `FallbackConfig` in ava-config handles provider failover; this adds model-level granularity
- Could feed into the model routing classifier for intelligent model selection
