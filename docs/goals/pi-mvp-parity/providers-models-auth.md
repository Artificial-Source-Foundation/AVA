# Providers, Models, Auth, And Image Generation

## Goal Objective

Bring AVA provider/model/auth coverage to Pi MVP parity by implementing the selected high-leverage providers and model catalog behavior, or explicitly deferring every non-selected Pi provider with a documented reason, smoke plan, and revisit trigger.

Suggested Codex command:

```text
/goal Bring AVA provider/model/auth parity to documented closure using docs/goals/pi-mvp-parity/providers-models-auth.md. First reconcile Pi providers and AVA provider registry, then implement or defer every 100 percent criterion with tests, live-smoke criteria, and docs updates. Stop for product approval before adding unsupported OAuth flows or remote provider/plugin loading.
```

## Pi References To Inspect First

| Topic | Pi paths |
| --- | --- |
| Provider registry | `docs/reference-code/pi/packages/ai/src/providers/all.ts` |
| Provider files | `docs/reference-code/pi/packages/ai/src/providers/` |
| API protocols | `docs/reference-code/pi/packages/ai/src/api/` |
| Model types | `docs/reference-code/pi/packages/ai/src/types.ts`, `docs/reference-code/pi/packages/ai/src/models.ts` |
| Generated catalogs | `docs/reference-code/pi/packages/ai/src/models.generated.ts`, `docs/reference-code/pi/packages/ai/src/image-models.generated.ts` |
| Generation scripts | `docs/reference-code/pi/packages/ai/scripts/generate-models.ts`, `docs/reference-code/pi/packages/ai/scripts/generate-image-models.ts` |
| Auth | `docs/reference-code/pi/packages/ai/src/auth/`, `docs/reference-code/pi/packages/ai/src/env-api-keys.ts`, `docs/reference-code/pi/packages/coding-agent/src/core/auth-storage.ts` |
| OAuth | `docs/reference-code/pi/packages/ai/src/utils/oauth/` |
| User docs | `docs/reference-code/pi/packages/coding-agent/docs/providers.md`, `docs/reference-code/pi/packages/coding-agent/docs/models.md`, `docs/reference-code/pi/packages/coding-agent/docs/settings.md` |
| Provider tests | `docs/reference-code/pi/packages/ai/test/` |

## AVA References To Inspect First

| Topic | AVA paths |
| --- | --- |
| Provider registry | `src/ava/provider/registry.cpp`, `src/ava/provider/registry.h` |
| Provider contract | `src/ava/provider/provider.h` |
| OpenAI native | `src/ava/provider/openai_provider.cpp`, `src/ava/provider/openai_request.cpp`, `src/ava/provider/openai_response_parser.cpp` |
| Anthropic native | `src/ava/provider/anthropic_provider.cpp`, `src/ava/provider/anthropic_request.cpp` |
| OpenAI-compatible | `src/ava/provider/openai_compatible_provider.cpp`, `src/ava/provider/openai_compatible_request.cpp` |
| Auth store | `src/ava/config/auth.cpp`, `src/ava/config/auth_file_store.cpp`, `src/ava/config/openai_oauth.cpp` |
| Provider profiles | `src/ava/config/provider_profiles.cpp`, `src/ava/config/provider_profiles.h` |
| Model registry | `src/ava/config/model_config.cpp`, `src/ava/config/model_profiles.cpp`, `src/ava/config/model_config.h` |
| Reasoning | `src/ava/config/reasoning_profiles.h` |
| Tests | `tests/provider_openai_tests.cpp`, `tests/provider_anthropic_tests.cpp`, `tests/provider_gemini_tests.cpp`, `tests/config_context_auth_oauth_tests.cpp`, `tests/provider_live_smoke_tests.cpp` |

## Current Gap Summary

AVA already has OpenAI, Anthropic, DeepSeek, Gemini, Kimi, Moonshot, and OpenRouter through four provider implementations. Pi has 36 chat/image provider entries, 9 chat protocol families, and OpenRouter image generation. The main gap is not provider abstraction quality; it is remaining provider breadth, catalog generation, smoke evidence, and UX around provider/model selection.

## 100 Percent Criteria

| Criterion | Required AVA State |
| --- | --- |
| Provider disposition matrix | Every Pi provider is listed as implemented, OpenAI-compatible alias, deferred, or excluded. The matrix must live in this file or a linked doc. |
| Minimum Pi-like provider set | OpenAI, Anthropic, OpenRouter, Google/Gemini, and DeepSeek are implemented or explicitly rescoped. OpenRouter remains the high-leverage provider for broad model access. |
| Model catalog strategy | AVA either has generated catalog ingestion or a documented manually curated catalog policy with update command, validation tests, and owner workflow. |
| Image generation decision | OpenRouter image models are implemented or explicitly deferred with rationale. If implemented, include model metadata, auth, request/response handling, and tests. |
| Auth parity | API-key env/storage works for selected providers. OAuth is implemented only where supported and documented; unsupported third-party OAuth flows are not faked. |
| Reasoning mapping | Provider-specific reasoning/thinking formats are validated before model switch and documented in model metadata. |
| Provider/model UX | `/providers`, `/models`, `/model`, `/scoped-models`, Ctrl+P, and settings entries expose status, capabilities, auth source without secrets, compatibility quirks, pricing, context, modalities, and live-smoke status where known. |
| Live smoke matrix | `docs/operations/testing.md` or this file records per-provider smoke env vars, expected model, command, latest result, and skip reason when credentials are absent. |
| Tests | Provider request/parse tests, auth tests, model config tests, and credential-gated live smoke coverage exist for every implemented provider family. |

## Provider Disposition Matrix

| Pi provider / family | AVA MVP disposition | Rationale / next action |
| --- | --- | --- |
| `openai` / OpenAI Responses | Implemented | Native OpenAI Responses provider, OAuth/API-key auth, image-capable model metadata, request/parse tests, and live-smoke gate. |
| `anthropic` / Anthropic Messages | Implemented | Native Anthropic Messages provider, API-key plus stored/env OAuth bearer refresh, request/parse tests, and live-smoke gate. Interactive Anthropic OAuth remains deferred because no supported third-party flow is documented. |
| `openrouter` chat | Implemented | OpenAI-compatible runtime with curated model metadata and live-smoke gate; OpenRouter remains AVA's broad provider access path. |
| `deepseek` | Implemented for chat and reasoning-effort requests | Added direct OpenAI-compatible DeepSeek profile, `DEEPSEEK_API_KEY`, `DEEPSEEK_BASE_URL`, `deepseek-v4-flash`, `deepseek-v4-pro`, `reasoning_effort=high|max` mapping, request/error tests, pricing/context metadata, and live-smoke gate. AVA parses compatible `reasoning_content` but does not replay prior DeepSeek reasoning into future requests. |
| `kimi-coding`, `moonshotai`, `moonshotai-cn` | Implemented as AVA `kimi` / `moonshot` profiles | OpenAI-compatible shims with request/parse/profile tests, env/base URL overrides, and live-smoke gates. CN/regional variants remain custom config unless product priority requires first-class profiles. |
| `google` Gemini | Implemented as AVA `gemini` | Native Google Generative AI `generateContent`/SSE provider, `GEMINI_API_KEY`, `GEMINI_BASE_URL`, curated `gemini-2.5-pro` metadata, request/response/SSE tests, and live-smoke gate. Explicit AVA reasoning options remain rejected until Gemini-specific metadata/request semantics are specified. |
| `google-vertex` | Deferred | Requires Google Cloud/Vertex ambient credential and project/region semantics plus live-smoke design. |
| `amazon-bedrock` | Deferred | Requires AWS credential, region, model-id, and Bedrock Converse streaming semantics. |
| `azure-openai-responses`, `github-copilot` Azure-style routes | Deferred | Requires Azure/Copilot-specific auth and endpoint configuration without leaking enterprise credentials. |
| `openai-codex` | Deferred | AVA's OpenAI path covers normal OpenAI Responses. Codex-specific auth/cache/websocket behavior is not an AVA MVP requirement. |
| `mistral` | Deferred | Mistral conversations API is a separate protocol; current broad access can use OpenRouter or explicit custom config where compatible. |
| `groq`, `together`, `fireworks`, `xai`, `cerebras`, `huggingface`, `nvidia` | Deferred / covered through OpenRouter or custom compatible config | These are OpenAI-compatible provider-zoo profiles but still need auth, endpoint quirks, model metadata, pricing, and live smokes before first-class AVA support. |
| `vercel-ai-gateway` | Deferred metadata-only | AVA keeps a non-runtime provider profile for guidance; runtime registration waits for gateway-specific validation. |
| `cloudflare-ai-gateway`, `cloudflare-workers-ai` | Deferred | Requires Cloudflare account/auth/config and endpoint-specific model metadata; not MVP while OpenRouter covers broad routing. |
| `minimax`, `minimax-cn`, `zai`, `zai-coding-cn`, `xiaomi`, `xiaomi-token-plan-cn`, `xiaomi-token-plan-ams`, `xiaomi-token-plan-sgp`, `ant-ling` | Deferred | Low-priority regional/provider-zoo routes; add only with owned credentials, protocol quirks, model metadata, and smoke plans. |
| `opencode`, `opencode-go` | Excluded for MVP | OpenCode platform/provider compatibility is reference behavior, not an AVA local-terminal MVP provider target. |
| `openrouter-images` / OpenRouter image generation | Deferred | Needs image-generation request/response handling, safety review, model metadata, and export/session semantics beyond current image-input support. |

## Model Catalog Strategy

AVA uses a manually curated built-in catalog plus additive `$XDG_CONFIG_HOME/ava/models.json` overrides for MVP. This avoids build-time network dependencies and keeps provider/model metadata reviewable in C++ tests. A generated catalog is deferred until AVA has an owner workflow that pins source data, validates provider compatibility, and checks in generated output; live price/model changes should not silently alter release behavior.

## Checkpoint Plan

| Checkpoint | Status | Notes |
| --- | --- | --- |
| P1. Provider disposition matrix | Complete | This file now maps each Pi provider family to implemented, deferred, or excluded AVA disposition. |
| P2. Direct DeepSeek | Complete | Added DeepSeek through the existing OpenAI-compatible runtime, auth env, model metadata, docs, and tests. |
| P3. Google/Gemini protocol | Complete | Added native Google Generative AI `generateContent`/SSE protocol, API-key auth, model metadata, tests, docs, and live-smoke gate. Vertex remains separately deferred. |
| P4. OpenRouter catalog and images | Deferred | Chat path remains implemented; image generation waits for a safety/session/export design. |
| P5. Catalog generation | Deferred | Manual curated catalog is the MVP policy; generated sync is not introduced without a pinned owner workflow. |
| P6. UX closure | Complete for current providers | Existing `/providers`, `/models`, `/model`, `/scoped-models`, Ctrl+P, and settings rows now include DeepSeek through the shared provider/model registry. |

## Implementation Slices

| Slice | Work |
| --- | --- |
| P1. Provider disposition matrix | Add a table covering Pi's providers: OpenAI, Anthropic, OpenRouter, Google, DeepSeek, Bedrock, Vertex, Azure, Copilot, Mistral, Groq, Together, Fireworks, xAI, Cerebras, HuggingFace, Cloudflare, Vercel AI Gateway, Kimi, Moonshot, Minimax, Z.ai, Xiaomi, and others. |
| P2. Direct DeepSeek | Register a direct OpenAI-compatible `deepseek` provider profile, env var, model metadata, auth docs, and live smoke gate. |
| P3. Google/Gemini protocol | Implemented native Google Generative AI request/response/SSE parsing, auth profile, model metadata, tests, and live smoke gate. Vertex remains a separate future provider decision. |
| P4. OpenRouter catalog and images | Decide whether OpenRouter model/image catalog is generated, curated, or deferred. If implemented, add image model metadata and request path behind permissioned attachment/image safety. |
| P5. Catalog generation | Add a reproducible generation or sync script only if it does not introduce unsafe build-time network dependencies. Generated outputs must be checked in or explicitly documented. |
| P6. UX closure | Update `/providers`, `/models`, `/model`, and docs to show provider status and smoke coverage clearly. |

## Non-Goals Unless Approved

| Item | Reason |
| --- | --- |
| Unofficial Anthropic interactive OAuth | Existing docs do not expose a stable third-party authorization/device flow. Keep stored/env OAuth bearer refresh only unless Anthropic publishes support. |
| Remote provider plugins | Provider auth and model metadata are security-sensitive. Dynamic provider loading needs a strict plugin contract and trust policy first. |
| Bedrock/Vertex ambient auth in MVP | Valuable but requires cloud-specific credential semantics and smoke infrastructure. Defer unless selected by product priority. |

## Verification

Run targeted tests after each provider slice:

```sh
ctest --test-dir build -R 'ava_tests\.(provider_openai|provider_anthropic|provider_gemini|config_context_auth_oauth)$' --output-on-failure
```

Run live smokes only when credentials exist:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure
```

Before area completion:

```sh
cmake --build --preset dev
ctest --preset dev --output-on-failure
git --no-pager diff --check
```

## Progress Log

- 2026-07-03: Inspected Pi provider registry/catalog/auth/test references and AVA provider profiles, registry, model registry, auth resolution, provider tests, `/providers`/`/models` docs, and live-smoke harness.
- 2026-07-03: Implemented direct DeepSeek as an AVA-native OpenAI-compatible profile: `DEEPSEEK_API_KEY`, `DEEPSEEK_BASE_URL`, `/chat/completions`, curated `deepseek-v4-flash`/`deepseek-v4-pro` context/pricing/reasoning metadata, `reasoning_effort=high|max` request mapping, provider factory registration, compatible request/error tests, model metadata tests, and credential-gated live-smoke entry.
- 2026-07-03: Updated docs and product ledgers to include DeepSeek and to record deferrals for Bedrock, Vertex, Azure/Copilot, OpenRouter image generation, provider-zoo routes, generated catalogs, and DeepSeek reasoning-effort request mapping.
- 2026-07-04: Re-audited the provider closure against the current Pi reference tree. `docs/reference-code/pi/packages/ai/src/providers/all.ts` has 36 provider entries; every entry is now explicitly named or grouped with a concrete disposition in the Provider Disposition Matrix. Fixed stale Pi generation-script paths, updated the current-gap summary to include DeepSeek in AVA's implemented selected provider set, and linked this disposition matrix from the product coverage ledger.
- 2026-07-06: Implemented Gemini as AVA `gemini` with native Google Generative AI `generateContent`/SSE request handling, API-key auth, curated model metadata, local protocol tests, live-smoke matrix entry, and provider docs. Google Vertex remains deferred because it has separate cloud credential/project/region semantics.

## Changed Files

- `src/ava/config/provider_profiles.h`
- `src/ava/config/provider_profiles.cpp`
- `src/ava/config/model_profiles.cpp`
- `src/ava/provider/registry.cpp`
- `src/ava/provider/openai_compatible_provider.h`
- `src/ava/provider/openai_compatible_request.cpp`
- `src/ava/provider/gemini_provider.h`
- `src/ava/provider/gemini_provider.cpp`
- `src/ava/app/runtime_model.cpp`
- `tests/config_context_auth_oauth_tests.cpp`
- `tests/provider_openai_tests.cpp`
- `tests/provider_gemini_tests.cpp`
- `tests/provider_live_smoke_tests.cpp`
- `tests/app_runtime_tests.cpp`
- `docs/core/configuration.md`
- `docs/core/usage.md`
- `docs/operations/testing.md`
- `README.md`
- `docs/product/mvp-baseline.md`
- `docs/product/mvp-coverage-ledger.md`
- `docs/goals/pi-mvp-parity/providers-models-auth.md`

## Decisions And Deferrals

- DeepSeek chat is implemented through the existing compatible provider boundary rather than a new provider class. This keeps credential handling, cancellation, parsing, and error normalization in the established path.
- DeepSeek `reasoning_effort` is mapped only for AVA's `high` and `xhigh` levels (`high` and `max` on the wire). AVA parses DeepSeek `reasoning_content` but does not replay previous DeepSeek reasoning blocks because the provider rejects reasoning-content history.
- Google/Gemini is implemented as AVA `gemini`; Vertex, Bedrock, Azure/Copilot, Mistral, and provider-zoo profiles remain deferred because each needs provider-specific auth, metadata, compatibility quirks, and live-smoke criteria.
- Generated model catalogs and OpenRouter image generation are deferred to avoid build-time network dependency and to require image-generation safety/session/export design first.
- No unsupported Anthropic interactive OAuth or dynamic provider plugins were added.

## Validation Log

- `clang-format -i src/ava/config/provider_profiles.cpp src/ava/config/provider_profiles.h src/ava/provider/registry.cpp src/ava/config/model_profiles.cpp tests/config_context_auth_oauth_tests.cpp tests/provider_openai_tests.cpp tests/provider_live_smoke_tests.cpp tests/core_tests.cpp tests/support/test_harness.cpp tests/support/test_harness.h` — passed.
- `cmake --build --preset dev --target ava_tests` — passed.
- `ctest --test-dir build -R 'ava_tests\.(provider_openai|provider_anthropic|config_context_auth_oauth|provider_live_smoke|app_runtime|app_command_registry)$' --output-on-failure` — passed; provider live smoke skipped by CTest because `AVA_LIVE_PROVIDER_SMOKE` was not set.
- `git --no-pager diff --check` — passed.
- 2026-07-04 provider closure re-audit: `ctest --test-dir build -R 'ava_tests\.(provider_openai|provider_anthropic|config_context_auth_oauth|provider_live_smoke|app_runtime|app_command_registry)$' --output-on-failure` passed; `ava_tests.provider_live_smoke` skipped because the opt-in gate was not set. `cmake --preset dev`, `cmake --build --preset dev`, and `ctest --preset dev --output-on-failure` passed; default CTest skipped only `ava_tests.provider_live_smoke` plus the three gated TUI PTY smokes. `AVA_LIVE_PROVIDER_SMOKE=1 ctest --preset dev --output-on-failure -R provider_live` skipped as expected because none of the supported provider credential environment variables were set: `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, `ANTHROPIC_OAUTH_TOKEN`, `ANTHROPIC_AUTH_TOKEN`, `DEEPSEEK_API_KEY`, `GEMINI_API_KEY`, `KIMI_API_KEY`, `MOONSHOT_API_KEY`, or `OPENROUTER_API_KEY`.

## Review Findings

- API contract review found DeepSeek should not claim no reasoning while the endpoint can emit `reasoning_content`, and should not overstate context/usage metadata. Fixed by adding `reasoning_effort=high|max` mapping, setting DeepSeek context to 1,000,000 tokens, adding pricing, enabling stream usage, and documenting non-replayed `reasoning_content`.
- Security review found same-format `reasoning_content` could be replayed from DeepSeek into Kimi. Fixed by making model-switch reasoning replay provider-scoped and adding an app-runtime regression that rejects DeepSeek reasoning history when switching to Kimi.
- Final material review found no material findings after those fixes.
- 2026-07-04 material re-review found no new provider contract, auth-safety, architecture, or test-adequacy issues. The only findings were documentation consistency fixes: stale Pi generation-script paths, a current-gap summary that omitted implemented DeepSeek, and matrix wording that grouped `github-copilot`, `openrouter-images`, and Xiaomi token-plan providers without explicit names.

## Residual Risks

- DeepSeek live behavior is credential-gated and was not exercised without `DEEPSEEK_API_KEY`; release notes must classify live runs separately from local request-shape tests.
- Other selected providers also remain live-credential-gated in this environment. Full live provider evidence needs one or more of `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, `ANTHROPIC_OAUTH_TOKEN`, `ANTHROPIC_AUTH_TOKEN`, `DEEPSEEK_API_KEY`, `GEMINI_API_KEY`, `KIMI_API_KEY`, `MOONSHOT_API_KEY`, or `OPENROUTER_API_KEY`.
- Built-in DeepSeek model limits/pricing follow current public docs. If DeepSeek changes model names, context limits, or prices, update the curated catalog and tests rather than fetching metadata dynamically at runtime.
- DeepSeek reasoning-content replay remains intentionally disabled; future replay support would need provider confirmation that including prior reasoning in assistant messages is accepted.

## Pending Questions

- None. Deferred provider breadth items have explicit revisit criteria and do not block safe MVP progress.
