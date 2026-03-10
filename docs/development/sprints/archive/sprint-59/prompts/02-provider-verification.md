# Sprint 59-02: Provider Verification & Model Selector Completeness

## Context

You are working on **AVA**, a Rust-first AI coding agent with a Ratatui TUI. The project has ~21 Rust crates under `crates/`. Read `CLAUDE.md` and `AGENTS.md` at the project root before starting.

AVA supports 12+ LLM providers in `create_provider()` (`crates/ava-llm/src/providers/mod.rs`) and 23 providers in the auth/connect system (`crates/ava-auth/src/lib.rs`). However the **model selector UI** (`crates/ava-tui/src/widgets/model_selector.rs`) only shows 4 provider sections + hardcoded Ollama models. This means 7 coding plan providers and several other configured providers are invisible to users — they can connect but never select models.

### The gap

| Provider | `create_provider()` | Model Catalog | Model Selector UI |
|----------|:---:|:---:|:---:|
| anthropic | Yes | Yes | Yes |
| openai | Yes | Yes | Yes |
| openrouter | Yes | Yes (via anthropic/openai/google) | Yes |
| gemini | Yes | Yes | Yes |
| ollama | Yes | Hardcoded in selector | Yes (hardcoded) |
| **alibaba** | Yes | Yes | **NO** |
| **alibaba-cn** | Yes | Yes | **NO** |
| **zai-coding-plan** | Yes | Yes | **NO** |
| **zhipuai-coding-plan** | Yes | Yes | **NO** |
| **kimi-for-coding** | Yes | Yes | **NO** |
| **minimax-coding-plan** | Yes | Yes | **NO** |
| **minimax-cn-coding-plan** | Yes | Yes | **NO** |
| copilot | **NO** (Sprint 59-01) | **NO** | **NO** |
| mistral | No | No | No |
| groq | No | No | No |
| xai | No | No | No |
| deepinfra | No | No | No |
| together | No | No | No |
| cerebras | No | No | No |
| perplexity | No | No | No |
| cohere | No | No | No |
| azure | No | No | No |
| bedrock | No | No | No |

**Problem**: 7 coding plan providers have full backend support (create_provider + model catalog) but are invisible in the model selector because they're not in `PROVIDER_SECTIONS`.

---

## Phase 1: Verify Alibaba Cloud Model Studio

### Task 1a: Research Alibaba Cloud Model Studio documentation

**IMPORTANT**: Before checking code, fetch and read the Alibaba documentation to understand the correct API behavior:

1. Fetch the **Alibaba Coding Plan for OpenCode** docs:
   - URL: `https://www.alibabacloud.com/help/en/model-studio/opencode-coding-plan`
   - This is the coding plan subscription mode (free/reduced pricing) — NOT pay-as-you-go
   - Note the exclusive base URL and API key requirements for Coding Plan users

2. Fetch the **Model Studio API docs** (pay-as-you-go reference):
   - URL: `https://modelstudio.console.alibabacloud.com/ap-southeast-1?tab=doc#/doc`
   - This shows the general DashScope API format

3. Fetch the **Connect to Model Service** page:
   - URL: `https://modelstudio.console.alibabacloud.com/ap-southeast-1?tab=doc#/doc/?type=model&url=3020782`
   - This shows how to connect and which endpoints/headers are required

From the docs, verify:
- The correct base URL for Coding Plan users (should be different from pay-as-you-go)
- Whether there are any special headers or parameters for Coding Plan mode
- Available models and their IDs for the coding plan
- Whether `enable_thinking: true` is the correct way to enable reasoning
- Rate limits or other constraints

### Task 1b: Read and understand the current Alibaba implementation

Read these files and cross-reference against the documentation fetched above:
- `crates/ava-llm/src/providers/mod.rs` — how alibaba/alibaba-cn are routed (OpenAI-compatible with DashScope thinking)
- `crates/ava-llm/src/providers/openai.rs` — OpenAIProvider with `ThinkingFormat::DashScope`
- `crates/ava-config/src/model_catalog/fallback.rs` — alibaba/alibaba-cn model entries
- `crates/ava-auth/src/lib.rs` — alibaba provider info (env var: `DASHSCOPE_API_KEY`)
- `crates/ava-config/src/credentials.rs` — how credentials are loaded

Cross-reference with docs and verify:
1. `create_provider("alibaba", "qwen3-coder-flash", ...)` creates an `OpenAIProvider` with base URL `https://dashscope-intl.aliyuncs.com/compatible-mode/v1` and `ThinkingFormat::DashScope`
2. `create_provider("alibaba-cn", ...)` uses `https://dashscope.aliyuncs.com/compatible-mode/v1`
3. **Coding Plan base URL**: Verify if the Coding Plan docs specify a DIFFERENT base URL than the pay-as-you-go URL. If so, our current URL may be wrong for coding plan users — fix it.
4. Fallback catalog has correct models for both variants — cross-reference with the actual models listed in the Coding Plan docs
5. `CURATED_MODELS` whitelist includes both alibaba and alibaba-cn entries
6. Connection pool pre-warming has both base URLs in `base_url_for_provider()`
7. Any special headers or parameters required by the Coding Plan mode are included

### Task 1b: Headless smoke test (if credentials available)

```bash
# Check if Alibaba credentials exist
cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider alibaba --model qwen3-coder-flash --max-turns 2
```

If no credentials, skip and note it as untestable without API key.

**Before proceeding to Phase 2, invoke the Code Reviewer sub-agent to verify Alibaba routing is correct.**

---

## Phase 2: Add All Missing Providers to Model Selector

The model selector at `crates/ava-tui/src/widgets/model_selector.rs` needs sections for all providers that have both `create_provider()` support and model catalog entries.

### Task 2a: Add ModelSection variants

Add new variants to the `ModelSection` enum:

```rust
pub enum ModelSection {
    Recent,
    Copilot,        // Added by Sprint 59-01
    Anthropic,
    OpenAI,
    OpenRouter,
    Gemini,
    Alibaba,        // NEW
    ZAI,            // NEW
    Kimi,           // NEW
    MiniMax,        // NEW
    Ollama,
}
```

Add labels:
```rust
Self::Alibaba => "Alibaba Model Studio (free)".to_string(),
Self::ZAI => "ZAI / ZhipuAI Coding Plan (free)".to_string(),
Self::Kimi => "Kimi For Coding (free)".to_string(),
Self::MiniMax => "MiniMax Coding Plan (free)".to_string(),
```

### Task 2b: Add to PROVIDER_SECTIONS

```rust
const PROVIDER_SECTIONS: &[ProviderEntry] = &[
    // Core providers
    ("copilot", "copilot", || ModelSection::Copilot),
    ("anthropic", "anthropic", || ModelSection::Anthropic),
    ("openai", "openai", || ModelSection::OpenAI),
    ("openrouter", "openrouter", || ModelSection::OpenRouter),
    ("google", "gemini", || ModelSection::Gemini),
    // Coding plan providers (free tier)
    ("alibaba", "alibaba", || ModelSection::Alibaba),
    ("alibaba-cn", "alibaba-cn", || ModelSection::Alibaba),  // Same section
    ("zai-coding-plan", "zai-coding-plan", || ModelSection::ZAI),
    ("zhipuai-coding-plan", "zhipuai-coding-plan", || ModelSection::ZAI),  // Same section
    ("kimi-for-coding", "kimi-for-coding", || ModelSection::Kimi),
    ("minimax-coding-plan", "minimax-coding-plan", || ModelSection::MiniMax),
    ("minimax-cn-coding-plan", "minimax-cn-coding-plan", || ModelSection::MiniMax),  // Same section
];
```

**Important**: Providers that share a section (alibaba/alibaba-cn, zai/zhipuai, minimax/minimax-cn) should be grouped under the same `ModelSection` variant but show models from whichever one the user has configured. If both variants are configured, show both sets of models under the same section header.

### Task 2c: Handle the catalog provider mapping

The `build_select_items()` function uses `catalog.models_for(ava_provider)` to look up models. The catalog key must match the `provider_id` in `fallback_catalog()`:
- `catalog.models_for("alibaba")` → alibaba models
- `catalog.models_for("alibaba-cn")` → alibaba-cn models
- `catalog.models_for("zai-coding-plan")` → ZAI models
- etc.

Verify that the first element of each `ProviderEntry` tuple (the catalog lookup key) matches the keys used in `fallback_catalog()`. Currently the first element is used for OpenRouter's cross-provider lookup via `cm_provider()` but for direct providers it should match `ava_provider`. Read the `build_select_items()` function carefully to understand how `_catalog_provider` is used vs `ava_provider`.

**Before proceeding to Phase 3, invoke the Code Reviewer sub-agent to verify:**
- All 7 coding plan providers appear in the selector when configured
- Section grouping is correct (alibaba/alibaba-cn share a section, etc.)
- Catalog lookup keys match fallback_catalog() keys
- No duplicate models shown

---

## Phase 3: Verify All Provider Routing

### Task 3a: Write a routing verification test

In `crates/ava-llm/src/providers/mod.rs`, add a comprehensive test:

```rust
#[test]
fn all_providers_create_successfully_with_credentials() {
    // Test that every provider in create_provider() can be instantiated
    // with mock credentials (we're not making API calls, just verifying routing)
    let pool = default_pool();

    // Build credentials with all providers
    let mut creds = CredentialStore::default();
    // ... insert test credentials for each provider ...

    // Verify each provider creates without error
    let providers = [
        ("anthropic", "claude-sonnet-4"),
        ("openai", "gpt-4.1"),
        ("openrouter", "anthropic/claude-sonnet-4"),
        ("gemini", "gemini-2.5-pro"),
        ("ollama", "llama3.3"),
        ("alibaba", "qwen3-coder-flash"),
        ("alibaba-cn", "qwen3-coder-flash"),
        ("zai-coding-plan", "glm-4.7"),
        ("zhipuai-coding-plan", "glm-4.7"),
        ("kimi-for-coding", "k2p5"),
        ("minimax-coding-plan", "MiniMax-M2"),
        ("minimax-cn-coding-plan", "MiniMax-M2"),
    ];

    for (provider, model) in providers {
        let result = create_provider(provider, model, &creds, pool.clone());
        assert!(result.is_ok(), "Failed to create provider {provider}: {:?}", result.err());
    }
}
```

### Task 3b: Verify base_url_for_provider completeness

Add a test that every provider in `create_provider()` has a corresponding entry in `base_url_for_provider()`:

```rust
#[test]
fn all_routable_providers_have_base_url() {
    let expected = [
        "anthropic", "openai", "openrouter", "gemini", "ollama",
        "alibaba", "alibaba-cn",
        "zai-coding-plan", "zhipuai-coding-plan",
        "kimi-for-coding",
        "minimax-coding-plan", "minimax-cn-coding-plan",
    ];
    for provider in expected {
        assert!(
            base_url_for_provider(provider).is_some(),
            "Missing base_url_for_provider entry: {provider}"
        );
    }
}
```

### Task 3c: Verify catalog-to-provider alignment

In `crates/ava-config/src/model_catalog/` tests, verify that every provider in `CURATED_MODELS` has models in `fallback_catalog()` and vice versa:

```rust
#[test]
fn curated_models_match_fallback_catalog() {
    let catalog = fallback_catalog();
    for (provider, models) in CURATED_MODELS {
        let catalog_models = catalog.models_for(provider);
        assert!(
            !catalog_models.is_empty(),
            "Provider {provider} in CURATED_MODELS but not in fallback_catalog()"
        );
        for model_id in *models {
            assert!(
                catalog_models.iter().any(|m| m.id == *model_id),
                "Model {model_id} in CURATED_MODELS[{provider}] but not in fallback_catalog()"
            );
        }
    }
}
```

**Before proceeding to Phase 4, invoke the Code Reviewer sub-agent to verify all routing tests pass and cover every provider.**

---

## Phase 4: Verify Thinking/Reasoning Config Per Provider

Each coding plan provider has a specific thinking/reasoning format. Verify the mapping is correct:

| Provider | ThinkingFormat | How it works |
|----------|---------------|-------------|
| alibaba / alibaba-cn | `DashScope` | `enable_thinking: true` field |
| zai-coding-plan / zhipuai-coding-plan | `Zhipu` | `thinking: { type: "enabled", clear_thinking: false }` |
| kimi-for-coding | N/A (Anthropic-compatible) | Uses AnthropicProvider with `with_base_url()` — thinking via Anthropic's format |
| minimax-coding-plan / minimax-cn-coding-plan | N/A (Anthropic-compatible) | Same as kimi |

### Task 4a: Add thinking format tests

```rust
#[test]
fn alibaba_uses_dashscope_thinking() {
    // Verify create_provider for alibaba creates OpenAIProvider with DashScope format
    // (May need to expose thinking_format or test indirectly via supports_reasoning)
}

#[test]
fn zai_uses_zhipu_thinking() {
    // Verify create_provider for zai-coding-plan creates OpenAIProvider with Zhipu format
}

#[test]
fn kimi_uses_anthropic_provider() {
    // Verify create_provider for kimi-for-coding creates AnthropicProvider (not OpenAI)
}
```

If `ThinkingFormat` is not publicly accessible for testing, consider either:
- Making it `pub` (preferred — it's a simple enum)
- Testing indirectly via `model_name()` output or provider behavior

**Before proceeding to Phase 5, invoke the Code Reviewer sub-agent to verify thinking configs are correctly applied per provider.**

---

## Phase 5: End-to-End Verification Matrix

### Task 5a: Create a verification checklist

For each provider, verify this chain is complete:

```
auth/connect → credentials → create_provider() → base_url → model_catalog → model_selector_UI
```

Write results to stdout as a table. Any broken chain = failure.

### Task 5b: Run all tests

```bash
cargo test --workspace
cargo clippy --workspace
```

All must pass with zero warnings.

### Verification

Run and confirm:
```bash
cargo test --workspace
cargo clippy --workspace
```

**Invoke the Code Reviewer sub-agent for a FINAL review of ALL changes. Verify:**
1. All 12 providers with `create_provider()` support have model selector UI sections
2. Alibaba routing is correct (OpenAI-compatible, DashScope thinking, correct base URLs)
3. All routing tests pass
4. Catalog-to-provider alignment is verified
5. No clippy warnings, all tests pass
6. Model selector correctly shows/hides sections based on configured credentials

---

## Files Modified (Expected)

| File | Change |
|------|--------|
| `crates/ava-tui/src/widgets/model_selector.rs` | Add Alibaba, ZAI, Kimi, MiniMax sections to enum + PROVIDER_SECTIONS |
| `crates/ava-llm/src/providers/mod.rs` | Add routing verification tests |
| `crates/ava-config/src/model_catalog/fallback.rs` | Add catalog alignment test |

## Acceptance Criteria

- [ ] `cargo test --workspace` passes
- [ ] `cargo clippy --workspace` clean
- [ ] All 7 coding plan providers appear in model selector when configured
- [ ] Section grouping works (alibaba/alibaba-cn share section, etc.)
- [ ] Routing test covers all 12 providers
- [ ] Catalog alignment test verifies CURATED_MODELS ↔ fallback_catalog() consistency
- [ ] base_url_for_provider() covers all routable providers
- [ ] Thinking format test verifies DashScope for alibaba, Zhipu for ZAI, Anthropic for kimi/minimax
