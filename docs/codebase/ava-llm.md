# ava-llm

> Unified LLM provider interface with routing and circuit breaking.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `LLMProvider` | Core trait for LLM providers (generate, generate_stream, estimate_tokens, estimate_cost) |
| `LLMResponse` | Response struct with content, tool_calls, usage, thinking |
| `ProviderCapabilities` | Declarative capability surface (streaming, tool_use, thinking, images, etc.) |
| `ProviderErrorKind` | Structured error classification (RateLimit, AuthFailure, ContextWindowExceeded, etc.) |
| `ModelRouter` | Routes to providers with fallback and policy-based selection |
| `RouteDecision` | Routing result with provider, model, profile, source, and reasons |
| `RouteRequirements` | Requirements for routing (needs_vision, prefer_reasoning) |
| `RouteSource` | Source of routing decision (ConfigDefault, ManualOverride, PolicyTarget, etc.) |
| `ConnectionPool` | Session-scoped HTTP client pool keyed by base URL |
| `CircuitBreaker` | Circuit breaker pattern for resilience (5 failures / 30s cooldown default) |
| `RetryBudget` | Budget-aware retry with exponential backoff and jitter |
| `NormalizingProvider` | Message normalization wrapper for cross-provider compatibility |
| `SharedProvider` | Arc wrapper for sharing providers across consumers |
| `create_provider()` | Factory function for creating providers by name |
| `test_provider_credentials()` | Tests credentials for a provider/model combination |
| `ThinkingConfig` | Thinking level and budget configuration |
| `ResolvedThinkingConfig` | Provider-resolved thinking configuration with fallbacks |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Exports provider, router, pool, circuit_breaker, retry, providers, thinking |
| `provider.rs` | LLMProvider trait, LLMResponse, ProviderCapabilities, ProviderErrorKind, NormalizingProvider, SharedProvider |
| `router.rs` | ModelRouter with routing logic, RouteDecision, RouteRequirements, candidate selection |
| `pool.rs` | ConnectionPool for reusing reqwest::Client instances per base URL |
| `circuit_breaker.rs` | CircuitBreaker with CLOSED/OPEN/HALF_OPEN states and CAS transitions |
| `retry.rs` | RetryBudget with exponential backoff, jitter, and max delay capping |
| `providers/mod.rs` | Provider factory and base_url_for_provider function |
| `providers/anthropic.rs` | Anthropic API provider with Messages API |
| `providers/openai.rs` | OpenAI provider with ChatGPT/Responses API support |
| `providers/openrouter.rs` | OpenRouter multi-provider gateway |
| `providers/gemini.rs` | Google Gemini API provider |
| `providers/copilot.rs` | GitHub Copilot OAuth provider |
| `providers/ollama.rs` | Ollama local LLM provider |
| `providers/inception.rs` | Inception Labs provider |
| `providers/mock.rs` | Mock provider for testing |
| `providers/common/` | Shared parsing, message mapping, and pricing utilities |
| `message_transform.rs` | Cross-provider message normalization |
| `thinking.rs` | ThinkingConfig, ResolvedThinkingConfig, budget resolution |
| `credential_test.rs` | Credential testing utilities |
| `dynamic_provider.rs` | Dynamic credential refresh wrapper |

## Dependencies

Uses: ava-auth, ava-config, ava-types

Used by: ava-agent, ava-tui, src-tauri, ava-cli-providers, ava-praxis

## Key Patterns

- **Provider trait**: Async trait with generate, generate_stream, generate_with_tools, supports_tools, supports_thinking
- **Error classification**: ProviderErrorKind classifies errors for retry/circuit breaker decisions
- **Circuit breaker states**: CLOSED (normal), OPEN (blocking), HALF_OPEN (single probe)
- **Retry with jitter**: Exponential backoff with ±20% jitter and 60s max delay
- **Routing profiles**: Cheap (cost-optimized) vs Capable (quality-optimized) with configurable targets
- **Connection pooling**: Reuses HTTP clients per base URL with configurable timeouts
- **Thinking configuration**: Level (Off/Low/Medium/High/Max) with optional token budget
- **Cross-provider normalization**: NormalizingProvider adapts messages between provider formats
