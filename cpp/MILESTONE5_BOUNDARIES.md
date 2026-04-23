# C++ Milestone 5 Boundaries

This note records what is implemented for the C++ `ava_llm` foundation slice and what remains intentionally deferred.

## Implemented in Milestone 5

1. New `ava_llm` static library wired into `ava_runtime`.
2. Core provider infrastructure:
   - provider capabilities
   - provider error classification + retryability helpers
   - shared response DTO (`LlmResponse`)
   - provider interface + shared normalizing wrapper
   - retry mode / overload tracker / retry budget
   - circuit breaker
   - thinking config + resolved thinking config + fallback/support enums
   - provider kind + light message normalization helpers
   - simple token-estimation and pricing helpers
   - provider factory plumbing
3. Real `MockProvider` implementation.
4. Exactly one production provider implemented end-to-end for this milestone: `OpenAI`.
   - Blocking non-streaming request path
   - Blocking streaming request path that collects SSE chunks
   - Request/response JSON mapping and tool-call parsing
   - CPR-based transport path when `AVA_WITH_CPR=ON`

## Explicitly Deferred

1. Advanced routing (`ModelRouter`, route decisions/requirements/source).
2. Dynamic credential refresh and OAuth-refresh lifecycle management.
3. Long-tail provider parity (`copilot`, `openrouter`, `ollama`, `gemini`, `gateway`, etc.).
4. Async/coroutine streaming runtime.
5. Full Rust `ava-llm` feature parity.

Milestone 5 is intentionally partial and foundational: it activates real provider infrastructure and one production provider while keeping broader runtime parity and multi-provider breadth out of scope.
