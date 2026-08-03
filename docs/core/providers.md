# Provider Status And Pi Parity

This document is the compact provider parity/status ledger for AVA. It records what
exists today, what is metadata-only, what is still missing relative to Pi, and how
to validate live provider paths. It is intentionally status-only: do not implement
providers from this file, and do not treat a listed deferral as runtime support.

Primary source files:

- Runtime provider factories: [`src/ava/provider/registry.cpp`](https://github.com/Artificial-Source/AVA/blob/develop/src/ava/provider/registry.cpp)
- Provider metadata: [`src/ava/config/provider_profiles.cpp`](https://github.com/Artificial-Source/AVA/blob/develop/src/ava/config/provider_profiles.cpp)
- Built-in model metadata: [`src/ava/config/model_profiles.cpp`](https://github.com/Artificial-Source/AVA/blob/develop/src/ava/config/model_profiles.cpp)
- Auth resolution: [`src/ava/config/auth.cpp`](https://github.com/Artificial-Source/AVA/blob/develop/src/ava/config/auth.cpp)
- Reasoning profiles: [`src/ava/config/reasoning_profiles.cpp`](https://github.com/Artificial-Source/AVA/blob/develop/src/ava/config/reasoning_profiles.cpp)
- Live provider smoke harness: [`tests/provider_live_smoke_tests.cpp`](https://github.com/Artificial-Source/AVA/blob/develop/tests/provider_live_smoke_tests.cpp)
- Pi provider registry reference: [`packages/ai/src/providers/all.ts`](https://github.com/earendil-works/pi/blob/main/packages/ai/src/providers/all.ts)

Related user docs: [`docs/core/configuration.md#auth`](configuration.md#auth),
[`docs/core/configuration.md#models`](configuration.md#models),
[`docs/operations/testing.md#provider-live-smoke-matrix`](../operations/testing.md#provider-live-smoke-matrix),
and [`docs/core/usage.md#current-limits`](usage.md#current-limits).

## Runtime Providers Implemented

These provider ids have a built-in runtime factory and can back model selection.

| Provider id | Runtime/API family | Built-in models | Auth modes | Reasoning support | Live smoke |
| --- | --- | --- | --- | --- | --- |
| `openai` | Native OpenAI Responses provider; default endpoint `https://api.openai.com/v1/responses`. OpenAI OAuth requests are sent to the ChatGPT Codex Responses endpoint after auth options are applied; AVA omits the public API's `max_output_tokens` field on that delegated route because the Codex endpoint rejects it. | `gpt-5.5`, `gpt-5.6-sol`, `gpt-5.6-terra`, `gpt-5.6-luna`, `gpt-4.1-mini`; all declare text and image input. The GPT-5.6 profiles use a conservative 272K short-context boundary and 128K maximum output. | OpenAI browser OAuth, headless device OAuth, stored API key, or `OPENAI_API_KEY`. OAuth refresh is attempted before use when a refresh token exists. | OpenAI Responses effort levels `low`, `medium`, `high`, `xhigh`; GPT-5.6 additionally exposes `minimal` (mapped to `low`) and `max`. Requests use `reasoning.effort` plus automatic summary for non-`none` efforts, and GPT-5.6 maps `off` to `none` for API-key/OAuth compatibility. `gpt-4.1-mini` is currently marked non-reasoning in built-in metadata. | `OPENAI_API_KEY`, default model `gpt-4.1-mini`, override `AVA_LIVE_OPENAI_MODEL`. |
| `anthropic` | Native Anthropic Messages provider; default endpoint `https://api.anthropic.com`, override `ANTHROPIC_BASE_URL`. | `claude-sonnet-4-5`; declares text and image input. | Stored API key, `ANTHROPIC_API_KEY`, stored OAuth bearer, `ANTHROPIC_OAUTH_TOKEN`, or Anthropic SDK-compatible `ANTHROPIC_AUTH_TOKEN`. Stored OAuth can refresh when `refresh_token` and `expires_at` are present. Interactive Anthropic OAuth is not implemented. | Native `anthropic_thinking`. Provider profile supports `enabled`/`adaptive`; the current built-in model lists `enabled`. `enabled` requires a budget at least 1024 tokens and below max output. Display may be `summarized` or `omitted`. | `ANTHROPIC_API_KEY`, `ANTHROPIC_OAUTH_TOKEN`, or `ANTHROPIC_AUTH_TOKEN`; default model `claude-sonnet-4-5`, override `AVA_LIVE_ANTHROPIC_MODEL`. |
| `deepseek` | OpenAI-compatible chat-completions shim; default endpoint `https://api.deepseek.com/chat/completions`, override `DEEPSEEK_BASE_URL`. | `deepseek-v4-flash`, `deepseek-v4-pro`. | Stored API key or `DEEPSEEK_API_KEY`. | DeepSeek-compatible reasoning effort. AVA maps `high` to `reasoning_effort=high` and `xhigh` to `reasoning_effort=max`, parses `reasoning_content`, and uses request-time portable projection: native reasoning is retained only for an exact-compatible target and stripped for incompatible targets. | `DEEPSEEK_API_KEY`; default model `deepseek-v4-flash`, override `AVA_LIVE_DEEPSEEK_MODEL`. |
| `gemini` | Native Google Generative AI `generateContent`/SSE runtime; default endpoint `https://generativelanguage.googleapis.com`, override `GEMINI_BASE_URL`. | `gemini-2.5-pro`; declares text and image input. | Stored API key or `GEMINI_API_KEY`. | No AVA reasoning controls today; Gemini rejects explicit reasoning options until model metadata and request semantics are specified. | `GEMINI_API_KEY`; default model `gemini-2.5-pro`, override `AVA_LIVE_GEMINI_MODEL`. |
| `kimi` | OpenAI-compatible chat-completions shim; default endpoint `https://api.kimi.com/coding/v1/chat/completions`, override `KIMI_BASE_URL`; sends `KimiCLI/1.5` user agent and default temperature `1.0`. | `kimi-k2-thinking`, `kimi-for-coding`. | Stored API key or `KIMI_API_KEY`. | OpenAI-compatible `reasoning_content` with provider-scoped preservation/replay when metadata declares the matching format and quirk. | `KIMI_API_KEY`; default model `kimi-k2-thinking`, override `AVA_LIVE_KIMI_MODEL`. |
| `moonshot` | OpenAI-compatible chat-completions shim; default endpoint `https://api.moonshot.ai/v1/chat/completions`, override `MOONSHOT_BASE_URL`. | `kimi-k2.6`. | Stored API key or `MOONSHOT_API_KEY`. | OpenAI-compatible `reasoning_content` level metadata. The built-in profile does not preserve prior reasoning content by default. | `MOONSHOT_API_KEY`; default model `kimi-k2.6`, override `AVA_LIVE_MOONSHOT_MODEL`. |
| `openrouter` | OpenAI-compatible chat-completions shim; default endpoint `https://openrouter.ai/api/v1/chat/completions`, override `OPENROUTER_BASE_URL`. | `moonshotai/kimi-k2.6`. | Stored API key or `OPENROUTER_API_KEY`. | Runtime can parse compatible `reasoning_content`, but the current built-in OpenRouter model is marked non-reasoning. Custom OpenRouter models must opt in with `api_family`, `reasoning_format`, and compatibility metadata. | `OPENROUTER_API_KEY`; default model `moonshotai/kimi-k2.6`, override `AVA_LIVE_OPENROUTER_MODEL`. |
| `zai` | OpenAI-compatible Coding Plan chat-completions shim; default endpoint `https://api.z.ai/api/coding/paas/v4/chat/completions`, override `ZAI_BASE_URL`. | `glm-4.5-air`, `glm-4.7`, `glm-5-turbo`, `glm-5.1`, `glm-5.2`, `glm-5v-turbo` (image-capable). | Stored API key or `ZAI_API_KEY`. | Z.AI `thinking` controls: enabled sends `thinking.type=enabled` with `clear_thinking=false` and preserves/replays `reasoning_content`; cleared reasoning sends `thinking.type=disabled`. Most GLM models stay level-only (`enabled`); glm-5.2 maps AVA `minimal` to enabled-without-effort, `low`/`medium`/`high` to `reasoning_effort=high`, and `xhigh` to `reasoning_effort=max`. Models that opt in send `tool_stream=true` only when tools are present. | Optional opt-in only; no required live-smoke gate. |
| `zai-coding-cn` | Same Coding Plan runtime as `zai` against the China endpoint `https://open.bigmodel.cn/api/coding/paas/v4/chat/completions`, override `ZAI_CODING_CN_BASE_URL`. | Same six GLM Coding Plan models as Global. | Stored API key or `ZAI_CODING_CN_API_KEY`. | Same Z.AI thinking/tool-stream/effort semantics as Global. | Optional opt-in only; no required live-smoke gate. |

## Metadata-Only Provider Entries

| Provider id | Status | Meaning |
| --- | --- | --- |
| `vercel` | Metadata-only, not runtime selectable | `ProviderProfile` exists for Vercel AI Gateway guidance, but there is no runtime provider factory, no built-in models, and no live smoke entry. `/providers` reports it as unavailable/not registered. Runtime support requires endpoint/auth validation, model metadata, request/response tests, and a live-smoke case. |
| `openai-compatible` | Internal reasoning fallback only | Not a built-in selectable provider id. It is used as an internal fallback when custom model metadata declares `api_family=openai_chat_completions` with `reasoning_format=reasoning_content`. It does not provide auth, endpoint configuration, or model selection by itself. |

## Auth Modes And Precedence

Credential resolution is provider-scoped and never prints secret values.

| Scope | Supported today |
| --- | --- |
| Stored auth file | `$XDG_CONFIG_HOME/ava/auth.json` or `~/.config/ava/auth.json`. Provider objects use `{"provider":{"type":"api_key","api_key":"..."}}`; OpenAI and Anthropic also accept OAuth-shaped objects. |
| OpenAI interactive auth | `ava connect openai` supports browser OAuth, headless device OAuth, and API-key setup. OpenAI OAuth entries are refreshed before use when possible. |
| Non-OpenAI interactive auth | `ava connect <provider> --api-key`, `/connect`, and `/login` store API keys for runtime provider ids. Non-OpenAI provider-specific OAuth is not faked. |
| Environment API keys | `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, `DEEPSEEK_API_KEY`, `GEMINI_API_KEY`, `KIMI_API_KEY`, `MOONSHOT_API_KEY`, `OPENROUTER_API_KEY`, `ZAI_API_KEY`, and `ZAI_CODING_CN_API_KEY`. Other provider ids use the generic uppercase `<PROVIDER_ID>_API_KEY` convention if a future runtime provider is added. |
| Anthropic OAuth bearer | `ANTHROPIC_OAUTH_TOKEN` is preferred over `ANTHROPIC_AUTH_TOKEN`; both are preferred over `ANTHROPIC_API_KEY` when no stored Anthropic credential exists. Stored Anthropic OAuth can refresh with `refresh_token` and `expires_at`, but AVA does not initiate Anthropic interactive OAuth because there is no documented third-party flow for AVA-style clients. |
| Base URL overrides | Implemented runtime overrides are `ANTHROPIC_BASE_URL`, `DEEPSEEK_BASE_URL`, `GEMINI_BASE_URL`, `KIMI_BASE_URL`, `MOONSHOT_BASE_URL`, `OPENROUTER_BASE_URL`, `ZAI_BASE_URL`, and `ZAI_CODING_CN_BASE_URL`. OpenAI's built-in runtime uses its default base URL. |

## Reasoning Support Summary

| API family / format | Providers and models | Supported request shape | Replay/storage note |
| --- | --- | --- | --- |
| `openai_responses` / `openai_responses` | `openai/gpt-5.5`, `openai/gpt-5.6-sol`, `openai/gpt-5.6-terra`, and `openai/gpt-5.6-luna`; custom OpenAI Responses models can opt in with matching metadata. | `request.reasoning.effort=<level>` and `request.reasoning.summary=auto` for non-`none` effort. | Reasoning events are surfaced through provider stream events; controls are effort-only. |
| `anthropic_messages` / `anthropic_thinking` | `anthropic/claude-sonnet-4-5`. | `request.thinking.type=<level>`; `enabled` requires `budget_tokens`, and manual budget must be below max output. | Native thinking signatures/redacted data remain provider-private and are replayed only through Anthropic-compatible content parts. |
| `openai_chat_completions` / `reasoning_content` | `kimi`, `moonshot`, `deepseek`, `zai`, `zai-coding-cn`, and custom compatible models that opt in. | Kimi/Moonshot-style profiles use level-only `request.thinking.type=<level>`. DeepSeek uses `reasoning_effort=high|max` for AVA `high`/`xhigh`. Z.AI profiles send `thinking.type=enabled` with `clear_thinking=false` when reasoning is on, `thinking.type=disabled` when cleared, optional glm-5.2 `reasoning_effort`, and opt-in `tool_stream` only when tools are present. | Exact-compatible native reasoning may replay natively. Every request otherwise uses a copy-only target-aware portable projection that strips incompatible private/native reasoning and metadata while preserving visible text and complete representable tool pairs; persisted sessions are not rewritten. The next request fails closed for malformed committed v4 output or a committed v4 function call without its exact bound result. |
| No reasoning metadata | `openai/gpt-4.1-mini`, `gemini/gemini-2.5-pro`, built-in `openrouter/moonshotai/kimi-k2.6`, and custom models that omit reasoning fields. | No reasoning request should be sent. | `/models` reports advisory diagnostics for missing or mismatched reasoning metadata; unregistered providers remain disabled. |

## Live-Smoke Environment Variables

The provider live smoke is opt-in and environment-only; it does not read
`auth.json` and should not be run accidentally in normal CI.

```sh
AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure
```

| Provider case | Required credential env | Default model | Model override env |
| --- | --- | --- | --- |
| OpenAI | `OPENAI_API_KEY` | `gpt-4.1-mini` | `AVA_LIVE_OPENAI_MODEL` |
| Anthropic API key | `ANTHROPIC_API_KEY` | `claude-sonnet-4-5` | `AVA_LIVE_ANTHROPIC_MODEL` |
| Anthropic OAuth bearer | `ANTHROPIC_OAUTH_TOKEN` or `ANTHROPIC_AUTH_TOKEN` | `claude-sonnet-4-5` | `AVA_LIVE_ANTHROPIC_MODEL` |
| DeepSeek | `DEEPSEEK_API_KEY` | `deepseek-v4-flash` | `AVA_LIVE_DEEPSEEK_MODEL` |
| Gemini | `GEMINI_API_KEY` | `gemini-2.5-pro` | `AVA_LIVE_GEMINI_MODEL` |
| Kimi | `KIMI_API_KEY` | `kimi-k2-thinking` | `AVA_LIVE_KIMI_MODEL` |
| Moonshot | `MOONSHOT_API_KEY` | `kimi-k2.6` | `AVA_LIVE_MOONSHOT_MODEL` |
| OpenRouter | `OPENROUTER_API_KEY` | `moonshotai/kimi-k2.6` | `AVA_LIVE_OPENROUTER_MODEL` |

Record live results as one of: `passed`, `skipped/no credential`,
`credential/auth-blocked`, `provider/rate-limited`, `network-blocked`,
`provider-behavior/inconclusive`, or `AVA regression`. A live failure is an AVA
regression only after credential validity, provider availability, model access,
and local network reachability are ruled out.

## Pi Provider Disposition

Pi's current chat provider registry contains 36 entries, plus OpenRouter image
generation as a separate images provider. AVA intentionally implements a smaller
runtime set and documents the rest as deferred or excluded.

| Pi provider / family | AVA status | Notes / required future work |
| --- | --- | --- |
| `openai` | Implemented | Native OpenAI Responses runtime, OAuth/API key auth, image-capable model metadata, tests, and live-smoke gate. |
| `anthropic` | Implemented | Native Messages runtime, API key and stored/env OAuth bearer support. Interactive Anthropic OAuth remains deferred. |
| `deepseek` | Implemented | Direct OpenAI-compatible runtime profile with DeepSeek reasoning-effort mapping and live-smoke gate. |
| `google` | Implemented as AVA `gemini` | Native Google Generative AI GenerateContent runtime, API-key auth, image-capable model metadata, tests, and live-smoke gate. |
| `openrouter` chat | Implemented | OpenAI-compatible runtime and curated chat model. OpenRouter remains the broad-access path for many provider-zoo models. |
| `kimi-coding` | Implemented as AVA `kimi` | Direct compatible Kimi coding endpoint. |
| `moonshotai` | Implemented as AVA `moonshot` | Direct compatible Moonshot endpoint. |
| `moonshotai-cn` | Deferred / custom config only | Needs regional endpoint/auth/model metadata and owned smoke credentials before first-class support. |
| `google-vertex` | Missing / deferred | Requires Google Cloud project/region/ambient credential semantics and Vertex-specific smoke design. |
| `amazon-bedrock` | Missing / deferred | Requires AWS credentials, region/model-id handling, Bedrock Converse streaming, tests, and live smoke. |
| `azure-openai-responses` | Missing / deferred | Requires Azure endpoint/deployment/auth configuration and enterprise credential safety. |
| `github-copilot` | Missing / deferred | Requires Copilot-specific auth and endpoint semantics; do not reuse OpenAI auth silently. |
| `openai-codex` | Missing / deferred | AVA's normal OpenAI path is implemented; Codex-specific protocol/cache/websocket behavior is not MVP scope. |
| `mistral` | Missing / deferred | Separate protocol and model metadata; broad access can use OpenRouter until first-class support is selected. |
| `groq`, `together`, `fireworks`, `xai`, `cerebras`, `huggingface`, `nvidia` | Missing / deferred or covered through OpenRouter/custom compatible config | Each needs endpoint quirks, auth, model catalog entries, pricing/context metadata, tests, and live-smoke credentials before first-class support. |
| `vercel-ai-gateway` | Metadata-only / deferred | AVA has a non-runtime `vercel` provider profile only. Runtime support waits for gateway-specific request/auth validation. |
| `cloudflare-ai-gateway`, `cloudflare-workers-ai` | Missing / deferred | Requires Cloudflare account/auth/config, endpoint-specific metadata, and live-smoke design. |
| `minimax`, `minimax-cn` | Missing / deferred | Regional/provider-zoo routes; add only with owned credentials, protocol quirks, catalog entries, and smokes. |
| `zai`, `zai-coding-cn` | Implemented as AVA `zai` / `zai-coding-cn` | OpenAI-compatible Coding Plan runtime with Z.AI thinking/clear_thinking, optional tool_stream, glm-5.2 effort mapping, preserved reasoning_content, curated GLM catalog, and offline request/profile tests. Live smoke remains optional. |
| `xiaomi`, `xiaomi-token-plan-cn`, `xiaomi-token-plan-ams`, `xiaomi-token-plan-sgp` | Missing / deferred | Same as above; token-plan variants need explicit auth/region semantics. |
| `ant-ling` | Missing / deferred | Regional/provider-zoo route; needs auth/protocol/metadata/smoke plan. |
| `opencode`, `opencode-go` | Excluded for MVP | OpenCode platform/provider compatibility is reference behavior, not an AVA local-terminal MVP provider target. |
| `openrouter-images` | Missing / deferred | Image generation requires request/response handling, safety review, session/export semantics, model metadata, and live-smoke coverage. Current AVA image support is image input to chat models, not image generation. |

## Future Implementation Order

1. Keep the current seven runtime providers release-grade: local request/parse tests,
   auth diagnostics, model metadata, and credential-gated live-smoke evidence stay
   current before broadening provider scope.
2. Add enterprise/cloud providers only with a concrete product need and credential
   owner: `google-vertex`, `amazon-bedrock`, `azure-openai-responses`, and
   `github-copilot` each need explicit auth and endpoint contracts.
3. Expand provider-zoo compatible routes only when OpenRouter/custom compatible
   config is insufficient and AVA has owned credentials for request-shape tests
   plus live smokes.
4. Revisit `openrouter-images` after image-generation safety, persistence, export,
   and UI semantics are designed; do not conflate it with existing image input.
5. Revisit generated model catalogs after AVA has a pinned source, owner workflow,
   validation tests, and checked-in generated output policy. Until then, built-in
   model metadata is manually maintained and pricing/context/max-output updates
   should include focused assertions against the selected reference values.
6. Revisit custom provider plugins last, after provider auth, model metadata,
   request mutation, trust, provenance, and rollback boundaries are specified.
