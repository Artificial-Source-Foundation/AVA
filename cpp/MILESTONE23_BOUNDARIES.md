# C++ Milestone 23 Boundaries

M23 is a scoped provider/auth/config expansion slice for the C++ `ava_llm` tree. It adds Anthropic as the second production-provider implementation with a CPR-gated blocking Messages API path, focused request/response protocol coverage, and factory/CMake wiring while keeping broad auth/provider parity intentionally deferred.

## In Scope

1. Add Anthropic provider/protocol files under `cpp/include/ava/llm/providers/` and `cpp/src/llm/providers/`.
2. Wire `ava_llm` CMake sources and factory selection so `create_provider("anthropic", ...)` returns a real provider when credentials are configured.
3. Implement blocking Anthropic Messages API `generate(...)` when `AVA_WITH_CPR=ON` with:
   - `x-api-key` + `anthropic-version` headers
   - `model`, `max_tokens`, `messages`, optional `system`
   - optional tool schema/request translation and basic `tool_use` response parsing
   - usage parsing (`input_tokens`, `output_tokens`, cache read/create input tokens)
4. Keep transport claims honest for Anthropic in this slice: `generate(...)` fails explicitly in default builds without CPR, and streaming returns deterministic unsupported behavior.
5. Add focused `ava_llm_tests` coverage for Anthropic request construction, response parsing, provider factory selection, and deferred-provider stubs after Anthropic stub removal.

## Out of Scope

1. OAuth/device/browser auth parity in C++ provider flows.
2. Keychain-backed credential storage parity.
3. Full provider breadth parity (`gemini`, `openrouter`, `ollama`, `copilot`, `inception`, `alibaba`, `zai`, `kimi`, `minimax`, etc.).
4. Anthropic streaming transport/event parsing parity (SSE) and thinking/tool parity beyond this basic non-streaming slice.
5. Broader config-surface expansion beyond existing API-key/base-url/model wiring.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_llm_tests
git --no-pager diff --check -- cpp/include/ava/llm/providers/anthropic_protocol.hpp cpp/include/ava/llm/providers/anthropic_provider.hpp cpp/src/llm/providers/anthropic_protocol.cpp cpp/src/llm/providers/anthropic_provider.cpp cpp/src/llm/factory.cpp cpp/src/CMakeLists.txt cpp/tests/unit/llm_foundation.test.cpp cpp/MILESTONE23_BOUNDARIES.md cpp/README.md CHANGELOG.md docs/project/backlog.md
```

## Follow-Up Green-Fix Notes

- Anthropic response parsing errors now preserve structured `ProviderException` values instead of wrapping them as generic parse failures.
- Anthropic tool schemas now fall back to a minimal valid object schema when a registered tool has non-object parameters.
- Focused LLM coverage now exercises system-prompt concatenation, no-text tool-use requests, stringified/plain/error tool payloads, thinking blocks, malformed/defensive content shapes, zero/missing usage handling, and cache-creation token parsing.

## Decision Point

M23 intentionally promotes only a scoped Anthropic production-provider path in C++ (blocking non-streaming generate when `AVA_WITH_CPR=ON` + focused tool/use parsing) and keeps OAuth/keychain and long-tail provider/auth/config parity explicitly deferred.
