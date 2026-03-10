# Sprint 55: Coding Plan Providers

## Context

AVA is a Rust-first AI coding agent (~21 crates, Ratatui TUI, Tokio async). See `CLAUDE.md` for conventions.

**Goal**: Add 7 new "coding plan" providers — subscription-based APIs that offer free/reduced pricing for coding workloads. These providers use either OpenAI-compatible or Anthropic-compatible APIs.

**Prerequisites**: Sprints 53 (model catalog) and 54 (thinking support) are complete. The following already exist:
- `OpenAIProvider::with_base_url()` for OpenAI-compatible endpoints
- `AnthropicProvider` for Anthropic-compatible endpoints
- `ProviderInfo` in `crates/ava-auth/src/lib.rs` for provider registration
- `create_provider()` in `crates/ava-llm/src/providers/mod.rs` for provider instantiation
- `CredentialStore` in `crates/ava-config/src/credentials.rs` for API key storage
- `base_url_for_provider()` in `crates/ava-llm/src/router.rs` for connection pre-warming
- `fallback_catalog()` in `crates/ava-config/src/model_catalog.rs` for static model entries
- `ThinkingLevel` enum and `generate_with_thinking()` trait method

## Research Phase

Before writing any code, research how OpenCode handles these providers to ensure correctness.

### Research 1: Provider definitions
Read `docs/reference-code/opencode/packages/opencode/test/tool/fixtures/models-api.json` and extract the full provider blocks for:
- `alibaba` (international, lines ~2299+)
- `alibaba-cn` (China, lines ~9836+)
- `zai-coding-plan` (lines ~1742+)
- `zhipuai-coding-plan` (lines ~25599+)
- `kimi-for-coding` (lines ~14928+)
- `minimax-coding-plan` (lines ~29105+)
- `minimax-cn-coding-plan` (lines ~38319+)

Note the `npm` field — it tells you which SDK/API format each uses:
- `@ai-sdk/openai-compatible` → OpenAI-compatible API
- `@ai-sdk/anthropic` → Anthropic-compatible API

### Research 2: Thinking/reasoning config
Read `docs/reference-code/opencode/packages/opencode/src/provider/transform.ts` — the `options()` function (lines ~700-760). Key patterns:
- **ZAI/ZhipuAI** (OpenAI-compat): `thinking: { type: "enabled", clear_thinking: false }` (lines 713-726)
- **Alibaba-CN** (OpenAI-compat): `enable_thinking: true` for reasoning models, EXCEPT `kimi-k2-thinking` (lines 745-757)
- **Kimi K2.5** (Anthropic-compat): `thinking: { type: "enabled", budgetTokens: min(16000, output/2 - 1) }` (lines 733-743)
- **MiniMax**: No special thinking config mentioned

### Research 3: Existing provider patterns
Read these files to understand the registration pattern:
- `crates/ava-auth/src/lib.rs` — `all_providers()` function, `ProviderInfo` struct
- `crates/ava-llm/src/providers/mod.rs` — `create_provider()` match arms
- `crates/ava-llm/src/router.rs` — `base_url_for_provider()` function
- `crates/ava-config/src/model_catalog.rs` — `CURATED_MODELS` and `fallback_catalog()`

### Research 4: Anthropic-compatible provider pattern
Read `crates/ava-llm/src/providers/anthropic.rs` to understand how Anthropic API calls work (headers, body format). Kimi and MiniMax coding plans use Anthropic-compatible APIs — they need the same `x-api-key` header and message format, but with different base URLs.

**Document your findings before proceeding. Invoke the Code Reviewer sub-agent to verify your research is complete and accurate.**

## Task 1: Register providers in ava-auth

File: `crates/ava-auth/src/lib.rs`

Read the file. Find the `all_providers()` function. Add 7 new `ProviderInfo` entries:

```rust
// --- Coding Plan Providers ---
ProviderInfo {
    id: "alibaba",
    name: "Alibaba Model Studio",
    description: "Alibaba Cloud Model Studio (International)",
    auth_flows: &[AuthFlow::ApiKey],
    env_var: Some("DASHSCOPE_API_KEY"),
    default_base_url: Some("https://dashscope-intl.aliyuncs.com/compatible-mode/v1"),
    group: ProviderGroup::Other,
},
ProviderInfo {
    id: "alibaba-cn",
    name: "Alibaba (China)",
    description: "Alibaba DashScope (China mainland)",
    auth_flows: &[AuthFlow::ApiKey],
    env_var: Some("DASHSCOPE_API_KEY"),
    default_base_url: Some("https://dashscope.aliyuncs.com/compatible-mode/v1"),
    group: ProviderGroup::Other,
},
ProviderInfo {
    id: "zai-coding-plan",
    name: "Z.AI Coding Plan",
    description: "ZhipuAI coding subscription (z.ai)",
    auth_flows: &[AuthFlow::ApiKey],
    env_var: Some("ZHIPU_API_KEY"),
    default_base_url: Some("https://api.z.ai/api/coding/paas/v4"),
    group: ProviderGroup::Other,
},
ProviderInfo {
    id: "zhipuai-coding-plan",
    name: "ZhipuAI Coding Plan",
    description: "ZhipuAI coding subscription (bigmodel.cn)",
    auth_flows: &[AuthFlow::ApiKey],
    env_var: Some("ZHIPU_API_KEY"),
    default_base_url: Some("https://open.bigmodel.cn/api/coding/paas/v4"),
    group: ProviderGroup::Other,
},
ProviderInfo {
    id: "kimi-for-coding",
    name: "Kimi For Coding",
    description: "Moonshot Kimi coding subscription",
    auth_flows: &[AuthFlow::ApiKey],
    env_var: Some("KIMI_API_KEY"),
    default_base_url: Some("https://api.kimi.com/coding/v1"),
    group: ProviderGroup::Other,
},
ProviderInfo {
    id: "minimax-coding-plan",
    name: "MiniMax Coding Plan",
    description: "MiniMax coding subscription (minimax.io)",
    auth_flows: &[AuthFlow::ApiKey],
    env_var: Some("MINIMAX_API_KEY"),
    default_base_url: Some("https://api.minimax.io/anthropic/v1"),
    group: ProviderGroup::Other,
},
ProviderInfo {
    id: "minimax-cn-coding-plan",
    name: "MiniMax CN Coding Plan",
    description: "MiniMax coding subscription (minimaxi.com)",
    auth_flows: &[AuthFlow::ApiKey],
    env_var: Some("MINIMAX_API_KEY"),
    default_base_url: Some("https://api.minimaxi.com/anthropic/v1"),
    group: ProviderGroup::Other,
},
```

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 2: Add provider creation logic

File: `crates/ava-llm/src/providers/mod.rs`

Read the file. Find the `create_provider()` function's match statement. Add match arms for each new provider.

### OpenAI-compatible providers (alibaba, alibaba-cn, zai-coding-plan, zhipuai-coding-plan)

These reuse `OpenAIProvider::with_base_url()`:

```rust
"alibaba" | "alibaba-cn" | "zai-coding-plan" | "zhipuai-coding-plan" => {
    let entry = credential.ok_or_else(|| /* appropriate error */)?;
    let api_key = entry.effective_api_key()?;
    let default_url = match provider_name {
        "alibaba" => "https://dashscope-intl.aliyuncs.com/compatible-mode/v1",
        "alibaba-cn" => "https://dashscope.aliyuncs.com/compatible-mode/v1",
        "zai-coding-plan" => "https://api.z.ai/api/coding/paas/v4",
        "zhipuai-coding-plan" => "https://open.bigmodel.cn/api/coding/paas/v4",
        _ => unreachable!(),
    };
    let base_url = entry.base_url.as_deref().unwrap_or(default_url);
    Ok(Box::new(OpenAIProvider::with_base_url(pool, api_key, model, base_url)))
}
```

### Anthropic-compatible providers (kimi-for-coding, minimax-coding-plan, minimax-cn-coding-plan)

These need `AnthropicProvider` with a custom base URL. **Currently `AnthropicProvider` has a hardcoded `ANTHROPIC_BASE_URL` constant.** You need to:

1. Add a `with_base_url()` constructor to `AnthropicProvider` (in `crates/ava-llm/src/providers/anthropic.rs`):

```rust
pub fn with_base_url(
    pool: Arc<ConnectionPool>,
    api_key: impl Into<String>,
    model: impl Into<String>,
    base_url: impl Into<String>,
) -> Self {
    Self {
        pool,
        api_key: api_key.into(),
        model: model.into(),
        max_tokens: 4096,
        base_url: base_url.into(),
    }
}
```

This requires adding a `base_url: String` field to the `AnthropicProvider` struct and updating the existing `new()` to use `ANTHROPIC_BASE_URL.to_string()` as the default. Then change all uses of the `ANTHROPIC_BASE_URL` constant to `self.base_url` (in request URLs and `client()` method).

2. Add the match arm in `create_provider()`:

```rust
"kimi-for-coding" | "minimax-coding-plan" | "minimax-cn-coding-plan" => {
    let entry = credential.ok_or_else(|| /* appropriate error */)?;
    let api_key = entry.effective_api_key()?;
    let default_url = match provider_name {
        "kimi-for-coding" => "https://api.kimi.com/coding/v1",
        "minimax-coding-plan" => "https://api.minimax.io/anthropic/v1",
        "minimax-cn-coding-plan" => "https://api.minimaxi.com/anthropic/v1",
        _ => unreachable!(),
    };
    let base_url = entry.base_url.as_deref().unwrap_or(default_url);
    Ok(Box::new(AnthropicProvider::with_base_url(pool, api_key, model, base_url)))
}
```

**IMPORTANT**: The Anthropic-compatible providers (Kimi, MiniMax) use `x-api-key` header and Anthropic message format. They do NOT need `anthropic-version` or `anthropic-beta` headers — those are Anthropic-specific. Research whether Kimi/MiniMax accept or reject these headers. If they reject them, you may need a flag to skip Anthropic-specific headers for third-party providers. Check the `generate_with_tools()` and `generate_with_thinking()` methods for header usage.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 3: Provider-specific thinking/reasoning config

### 3a: Alibaba/DashScope `enable_thinking`

The OpenAI provider needs to know when to add `enable_thinking: true` to the request body. This is specific to DashScope (alibaba/alibaba-cn).

Option A (simple): Add a flag to `OpenAIProvider`:
```rust
pub struct OpenAIProvider {
    // ... existing fields ...
    /// Whether to add `enable_thinking: true` for reasoning models (DashScope)
    enable_thinking_field: bool,
}
```

Set this flag in the constructor or via a builder method. When building request body, if the flag is set AND thinking is not Off:
```rust
if self.enable_thinking_field {
    body["enable_thinking"] = json!(true);
}
```

Option B (cleaner): Create a `DashScopeProvider` wrapper similar to `OpenRouterProvider` that wraps `OpenAIProvider` and adds the extra field.

Choose whichever approach best fits the existing codebase patterns.

### 3b: ZAI/ZhipuAI thinking format

ZAI and ZhipuAI (OpenAI-compatible) use a different thinking format:
```json
{
    "thinking": {
        "type": "enabled",
        "clear_thinking": false
    }
}
```

This is different from OpenAI's `reasoning_effort` format. Similar to 3a, you need either a flag or a wrapper provider.

### 3c: Kimi K2.5 thinking (Anthropic-compatible)

Kimi K2.5 uses Anthropic's `thinking` parameter with budget tokens:
```json
{
    "thinking": {
        "type": "enabled",
        "budgetTokens": 16000
    }
}
```

Since Kimi uses `AnthropicProvider`, check if the existing `build_request_body_with_thinking()` method works correctly for Kimi models. The key difference:
- Anthropic native: `thinking: { type: "adaptive" }` + `output_config: { effort }`
- Kimi via Anthropic API: `thinking: { type: "enabled", budgetTokens: N }`

You may need to detect the provider context and use the appropriate format.

### 3d: MiniMax (Anthropic-compatible)

MiniMax appears to not need special thinking config. Verify this is correct.

**CRITICAL: Invoke the Code Reviewer sub-agent to verify ALL thinking configurations against `docs/reference-code/opencode/packages/opencode/src/provider/transform.ts`. Cross-reference the `options()` function (lines 700-760) for every provider. This is the most error-prone part — getting API contracts wrong means silent failures at runtime. Fix any issues before moving on.**

## Task 4: Connection pre-warming

File: `crates/ava-llm/src/router.rs`

Find the `base_url_for_provider()` function. Add entries for all new providers:

```rust
"alibaba" => Some("https://dashscope-intl.aliyuncs.com/compatible-mode/v1"),
"alibaba-cn" => Some("https://dashscope.aliyuncs.com/compatible-mode/v1"),
"zai-coding-plan" => Some("https://api.z.ai/api/coding/paas/v4"),
"zhipuai-coding-plan" => Some("https://open.bigmodel.cn/api/coding/paas/v4"),
"kimi-for-coding" => Some("https://api.kimi.com/coding/v1"),
"minimax-coding-plan" => Some("https://api.minimax.io/anthropic/v1"),
"minimax-cn-coding-plan" => Some("https://api.minimaxi.com/anthropic/v1"),
```

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 5: Model catalog entries

File: `crates/ava-config/src/model_catalog.rs`

### 5a: Add to CURATED_MODELS whitelist

Add entries for the key models from each coding plan provider. These are the models that should be fetched from models.dev API:

```rust
// In CURATED_MODELS array:
("zai-coding-plan", &["glm-4.7", "glm-4.5-flash", "glm-4.5"]),
("zhipuai-coding-plan", &["glm-4.7", "glm-4.5-flash", "glm-4.5"]),
("kimi-for-coding", &["k2p5", "kimi-k2-thinking"]),
("minimax-coding-plan", &["MiniMax-M2", "MiniMax-M2.1"]),
("minimax-cn-coding-plan", &["MiniMax-M2", "MiniMax-M2.1"]),
// alibaba and alibaba-cn have many models — curate the coding-relevant ones:
("alibaba", &["qwen3-coder-plus", "qwen3-coder"]),
("alibaba-cn", &["qwen3-coder-plus", "qwen3-coder", "kimi-k2.5"]),
```

**NOTE**: Verify the exact model IDs by reading the models-api.json fixture. The IDs above are approximations — use the exact `"id"` values from the JSON.

### 5b: Add to fallback catalog

Add fallback entries for key models so they appear even if the API fetch hasn't completed:

```rust
// In fallback_catalog():
providers.insert("zai-coding-plan".into(), vec![
    CatalogModel { id: "glm-4.7".into(), name: "GLM-4.7".into(), context_window: 204800, ..Default::default() },
    CatalogModel { id: "glm-4.5-flash".into(), name: "GLM-4.5 Flash".into(), context_window: 131072, ..Default::default() },
]);
// ... similar for other providers
```

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 6: Pricing data

File: `crates/ava-llm/src/providers/common.rs`

Find `model_pricing_usd_per_million()`. Add pricing entries for the new models. All coding plan models are **free** ($0/$0):

```rust
// Coding plan models (free tier)
id if id.starts_with("glm-") => (0.0, 0.0),
id if id.starts_with("MiniMax-") || id.starts_with("minimax-") => (0.0, 0.0),
id if id.starts_with("k2p5") || id.starts_with("kimi-k2") => (0.0, 0.0),
```

Be careful not to shadow existing pricing for these model families when accessed through non-coding-plan providers. Consider whether pricing should be provider-aware (model ID alone may not be sufficient).

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 7: Tests

1. **Provider registration test**: Verify all 7 new providers appear in `all_providers()`
2. **Provider creation test**: Verify `create_provider()` returns the correct provider type for each new ID
3. **Thinking config tests**: Verify provider-specific thinking body fields are correct:
   - DashScope: `enable_thinking: true`
   - ZAI: `thinking.type = "enabled"`
   - Kimi: `thinking.type = "enabled"` with `budgetTokens`
4. Run: `cargo test --workspace` — all must pass
5. Run: `cargo clippy --workspace` — **ZERO warnings** across ALL crates

## Task 8: CLI smoke test documentation

Update the CLI testing section to include new providers:

```bash
# ZAI Coding Plan (free tier)
cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider zai-coding-plan --model glm-4.5-flash --max-turns 3

# Kimi For Coding (free tier)
cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider kimi-for-coding --model k2p5 --max-turns 3
```

Add these to any test matrix or smoke test documentation.

## Acceptance Criteria

- [ ] 7 new providers registered in `all_providers()` (ava-auth)
- [ ] `create_provider()` handles all 7 provider IDs (ava-llm)
- [ ] `AnthropicProvider` supports custom base URLs for Kimi/MiniMax
- [ ] DashScope adds `enable_thinking: true` for reasoning models
- [ ] ZAI/ZhipuAI uses `thinking: { type: "enabled", clear_thinking: false }`
- [ ] Kimi uses `thinking: { type: "enabled", budgetTokens: N }`
- [ ] Connection pre-warming includes all 7 base URLs
- [ ] Model catalog has entries for key models per provider
- [ ] Fallback catalog has entries for key models
- [ ] Pricing data for coding plan models ($0/$0)
- [ ] All tests pass, clippy clean (0 warnings workspace-wide)
- [ ] Provider-specific headers handled correctly (no Anthropic headers sent to Kimi/MiniMax if they'd reject them)

## Final Code Review

After all changes, invoke the Code Reviewer sub-agent for a comprehensive review of ALL modifications across ALL files touched. Specifically verify:
1. **API format correctness**: OpenAI-compatible vs Anthropic-compatible providers use the right request/response format
2. **Header safety**: Anthropic-specific headers (`anthropic-version`, `anthropic-beta`) are NOT sent to third-party Anthropic-compatible providers if they'd cause errors
3. **Thinking config per provider**: Cross-reference EVERY provider against `docs/reference-code/opencode/packages/opencode/src/provider/transform.ts::options()` (lines 700-760)
4. **Credential isolation**: ZAI and ZhipuAI share `ZHIPU_API_KEY` — ensure both can be configured independently via `/connect`
5. **Model ID accuracy**: Verify model IDs match exactly what the API expects (check models-api.json)
6. **No regressions**: Existing providers (openai, anthropic, openrouter, gemini, ollama, groq, deepseek) continue to work unchanged
7. **Clippy**: `cargo clippy --workspace` must show 0 warnings total
