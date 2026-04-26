# C++ Milestone 37 Boundaries: Provider Breadth

Milestone 37 expands the C++ provider factory without adding new HTTP protocols. It wires recognized provider aliases that can safely reuse the existing OpenAI-compatible and Anthropic-compatible provider implementations.

## In Scope

1. Factory construction for OpenAI-compatible providers: `openrouter`, `inception`, and `zai`.
2. Factory construction for Anthropic-compatible providers: `alibaba`, `kimi`, and `minimax`.
3. Provider labels, provider kinds, default base URLs, and standard environment variable lookup needed for those providers.
4. Focused `ava_llm_tests` and `ava_config_tests` coverage for provider selection, deferred-provider inventory, aliases, and base URLs.

## Out of Scope

1. Native Gemini request/response protocol support.
2. GitHub Copilot OAuth/device-flow provider support.
3. Ollama local API behavior and non-keyed auth/header handling.
4. Responses API, ChatGPT subscription OAuth, LiteLLM-specific request shaping, provider-specific reasoning payloads/routing knobs, and plugin HTTP-header hooks.
5. Live network tests against external providers.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-build cpp-debug --target ava_llm_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_llm_tests "[ava_llm]"
ionice -c 3 nice -n 15 just cpp-build cpp-debug --target ava_config_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_config_tests "[ava_config]"
git diff --check
```

## Residual Risk

M37 increases constructible provider breadth, but it does not claim full Rust provider parity. The newly wired providers are protocol-compatible paths over existing C++ request builders; provider-specific live API behavior, OpenRouter routing/reasoning knobs, and ZAI/GLM thinking semantics still need separate validation before adoption.
