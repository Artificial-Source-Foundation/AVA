# C++ Milestone 31 Boundaries — Provider Streaming Parity Narrow Slice

Milestone 31 is a narrow provider parity pass focused on Anthropic streaming transport in `ava_llm`, forward-compatible Anthropic stream-event parsing, and a small model-registry refresh for currently high-value provider/model combinations.

## In Scope

1. Anthropic provider streaming transport wiring (CPR-enabled builds only):
   - Implement SSE stream handling in `AnthropicProvider::stream_generate(...)` using the same CPR `WriteCallback` ingestion pattern used by OpenAI.
   - Respect sink cancellation (`on_chunk(...) == false`) as cooperative stream cancellation.
   - Keep non-CPR builds fail-closed with deterministic unsupported streaming behavior.

2. Anthropic provider streaming API parity surface:
   - `generate_stream(...)` delegates to `stream_generate(...)`.
   - `capabilities().supports_streaming` reports true only when CPR transport is compiled in.

3. Anthropic stream parser forward-compatibility hardening:
   - Parse chunk types needed by current runtime flow (`text_delta`, `thinking_delta`, `input_json_delta`, `tool_use` starts, `message_delta` usage, `message_stop`).
   - Ignore non-action events (`message_start`, `content_block_stop`, `ping`) and unknown event types.
   - Raise `ProviderException` for stream `error` events.

4. Focused test updates:
   - Add Anthropic stream parser chunk/error/ignore coverage.
   - Update Anthropic provider capability/deferred behavior assertions to match CPR-gated streaming support and `generate_stream` delegation behavior.
   - Cover Anthropic request-body `stream` flag emission.

5. Minimal model-registry expansion for current high-value provider slices:
   - Keep existing OpenAI/Anthropic core IDs.
   - Add minimal Gemini/Ollama/OpenRouter entries used by active config/model-selection paths.
   - Keep broad long-tail provider implementation parity deferred.

## Out Of Scope

1. Full provider implementation breadth parity (`gemini`, `openrouter`, `ollama`, `copilot`, `inception`, `alibaba`, `zai`, `kimi`, `minimax`) beyond catalog metadata updates.
2. Full retry/circuit-breaker parity tuning across provider-specific streaming and non-streaming failure modes.
3. OAuth/keychain/provider-connect parity work in C++.
4. Full Anthropic thinking/tool UX parity beyond the current stream-chunk contract used by runtime.

## Validation

```bash
git diff --check
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_llm_tests "[ava_llm]"
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_config_tests "[ava_config]"
```

## Decision Point

After this milestone, the next provider lane decision remains whether to prioritize long-tail provider implementation breadth or provider-specific retry/timeout/backoff parity hardening first. Both are explicitly deferred from this narrow slice.
