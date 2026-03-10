# Sprint 59-05: Alibaba Coding Plan Hotfix + Subscription Label Fix

## Context

AVA is a Rust-first AI coding agent. Read `CLAUDE.md` and `AGENTS.md` first.

The Alibaba Cloud Model Studio "Coding Plan" provider was implemented in Sprint 55 but has **5 critical bugs** discovered during verification. **Sprint 59-04 (provider internals mega) has already been executed**, so the codebase now has:

- `registry.json` compiled via `include_str!` as the single source of truth for models
- `fallback_catalog()` generated from the registry (not hardcoded)
- Model name normalization via `registry().normalize()`
- `StreamChunk` rich streaming across all providers
- Circuit breaker wired into remote API providers

### The 5 bugs (all in Alibaba/coding plan layer):

1. **Wrong base URL** — using pay-as-you-go endpoint (`dashscope-intl.aliyuncs.com/compatible-mode/v1`) instead of coding plan endpoint (`coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1`)
2. **Wrong API format** — routing through `OpenAIProvider` but coding plan uses **Anthropic-compatible API**
3. **Wrong model list** — showing pay-as-you-go models (qwen3-coder-flash, qwen-turbo, qwen-vl-max) instead of coding plan models
4. **Incorrect "free" label** — subscription providers show "free" in the model selector instead of nothing
5. **Routing error** — "Could not route provider alibaba with model qwen3" because model names don't match catalog entries

## Phase 1: Research Alibaba Coding Plan API

**Goal**: Confirm the correct API format, endpoint, and model list from Alibaba's official docs.

### Steps

1. **Fetch the Coding Plan docs page** at `https://www.alibabacloud.com/help/en/model-studio/opencode-coding-plan`
   - Use WebFetch to retrieve the page content
   - Extract: base URL, API format (OpenAI vs Anthropic compatible), available models, authentication method

2. **Fetch the general Model Studio docs** at `https://modelstudio.console.alibabacloud.com/ap-southeast-1?tab=doc#/doc`
   - Look for the "Connect to model service" section
   - Extract: pay-as-you-go vs coding plan differences, API key format, model IDs

3. **Document findings** before proceeding. The expected findings based on prior research:
   - Coding Plan base URL: `https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1`
   - Coding Plan API format: **Anthropic-compatible** (NOT OpenAI-compatible)
   - Coding Plan models: Qwen3.5 Plus, Qwen3 Max, Qwen3 Coder Next, Qwen3 Coder Plus, MiniMax M2.5, GLM-5, GLM-4.7, Kimi K2.5
   - Auth: Same DashScope API key

*Before proceeding to Phase 2, invoke the Code Reviewer sub-agent to verify your research findings are consistent and complete.*

## Phase 2: Fix Alibaba Provider Routing

**File**: `crates/ava-llm/src/providers/mod.rs`

### 2a: Fix `base_url_for_provider`

Change the Alibaba URLs to the coding plan endpoints:

```rust
// BEFORE (wrong — pay-as-you-go):
"alibaba" => Some("https://dashscope-intl.aliyuncs.com/compatible-mode/v1"),
"alibaba-cn" => Some("https://dashscope.aliyuncs.com/compatible-mode/v1"),

// AFTER (correct — coding plan):
"alibaba" => Some("https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1"),
"alibaba-cn" => Some("https://coding.dashscope.aliyuncs.com/apps/anthropic/v1"),
```

### 2b: Move `"alibaba" | "alibaba-cn"` from OpenAI block to Anthropic block

The coding plan uses an **Anthropic-compatible API**. Move `"alibaba" | "alibaba-cn"` out of the OpenAI-compatible match arm and into the Anthropic-compatible one:

```rust
// OpenAI-compatible (REMOVE alibaba from here):
"zai-coding-plan" | "zhipuai-coding-plan" => {
    // ... OpenAIProvider with ThinkingFormat
}

// Anthropic-compatible (ADD alibaba here):
"kimi-for-coding" | "minimax-coding-plan" | "minimax-cn-coding-plan" | "alibaba" | "alibaba-cn" => {
    // ... AnthropicProvider::with_base_url(...)
    let default_url = match provider_name {
        "kimi-for-coding" => "https://api.kimi.com/coding/v1",
        "minimax-coding-plan" => "https://api.minimax.io/anthropic/v1",
        "minimax-cn-coding-plan" => "https://api.minimaxi.com/anthropic/v1",
        "alibaba" => "https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1",
        "alibaba-cn" => "https://coding.dashscope.aliyuncs.com/apps/anthropic/v1",
        _ => unreachable!(),
    };
    // ...
}
```

### 2c: Update tests

- Rename `alibaba_creates_openai_provider_with_correct_model` → `alibaba_creates_anthropic_compatible_provider`
- Update the test model name to match the new catalog (from Phase 3)
- Update `all_api_providers_create_successfully` with correct alibaba model names

*Before proceeding to Phase 3, invoke the Code Reviewer sub-agent to verify all routing changes are correct and consistent.*

## Phase 3: Fix Model List in Registry

**Sprint 04 introduced `registry.json` as the single source of truth.** Fix the Alibaba entries there.

**File**: `crates/ava-config/src/model_catalog/registry.json`

### 3a: Replace Alibaba models

Find the `"alibaba"` entries in `registry.json` and replace with the actual coding plan models discovered in Phase 1. Expected:

```json
{
  "id": "qwen3.5-plus",
  "provider": "alibaba",
  "name": "Qwen3.5 Plus",
  "aliases": ["qwen35-plus", "qwen3.5plus"],
  "capabilities": { "tool_call": true, "streaming": true },
  "limits": { "context_window": 131072, "max_output": null },
  "cost": { "input_per_million": 0.0, "output_per_million": 0.0 }
}
```

**IMPORTANT**: Use the exact model IDs from the Alibaba docs, NOT display names. The model IDs may use dashes/dots differently.

Replace the old wrong models (`qwen3-coder-flash`, `qwen-turbo`, `qwen-vl-max`) with the actual coding plan models.

Do the same for `"alibaba-cn"` entries. China endpoint may have additional models — keep `deepseek-r1` and `deepseek-v3` only if they're actually on the coding plan.

### 3b: Also update `CURATED_MODELS` in `fallback.rs`

Even though `fallback_catalog()` now delegates to the registry, the `CURATED_MODELS` const is still used by the dynamic catalog fetch (models.dev whitelist). Update the alibaba entries:

```rust
// BEFORE (wrong):
("alibaba", &["qwen3-coder-flash", "qwen-turbo", "qwen-vl-max"]),

// AFTER (correct — match registry.json):
("alibaba", &["qwen3.5-plus", "qwen3-max", "qwen3-coder-next", "qwen3-coder-plus"]),
```

### 3c: Fix subscription pricing

All coding plan provider entries in `registry.json` should have `"input_per_million": 0.0, "output_per_million": 0.0`. This is correct for the registry (they truly cost $0 per token with a subscription).

However, when `fallback_catalog()` converts these to `CatalogModel`, the cost fields become `Some(0.0)`, which makes `cost_display()` return `"free"`. To fix this, update `fallback_catalog()` (or wherever the conversion happens) so that for models with `0.0/0.0` cost from providers that are subscriptions, the `CatalogModel` gets `cost_input: None, cost_output: None` instead.

**Option A** (simpler): In the registry-to-catalog conversion, treat `0.0/0.0` as `None/None`:
```rust
cost_input: if model.cost.input_per_million == 0.0 && model.cost.output_per_million == 0.0 {
    None
} else {
    Some(model.cost.input_per_million)
},
```

This means `cost_display()` returns `""` (empty) instead of `"free"` for subscription models. Ollama models (local, truly free) are handled separately in the model selector and already show "free" via hardcoded `ItemStatus::Info("free")`.

**Option B** (more explicit): Add a `"subscription": true` field to registry entries and use that to control display. Only implement this if Option A causes issues with Ollama or other truly-free models.

*Before proceeding to Phase 4, invoke the Code Reviewer sub-agent to verify model list and pricing are correct.*

## Phase 4: Fix Section Labels in Model Selector

**File**: `crates/ava-tui/src/widgets/model_selector.rs`

### 4a: Remove "(free)" from subscription section labels

```rust
// BEFORE:
Self::Alibaba => "Alibaba Model Studio (free)".to_string(),
Self::ZAI => "ZAI / ZhipuAI Coding Plan (free)".to_string(),
Self::Kimi => "Kimi For Coding (free)".to_string(),
Self::MiniMax => "MiniMax Coding Plan (free)".to_string(),

// AFTER:
Self::Alibaba => "Alibaba Model Studio".to_string(),
Self::ZAI => "ZAI / ZhipuAI Coding Plan".to_string(),
Self::Kimi => "Kimi For Coding".to_string(),
Self::MiniMax => "MiniMax Coding Plan".to_string(),
```

These are subscription-included, not free. The label shouldn't claim "free".

*Before proceeding to Phase 5, invoke the Code Reviewer sub-agent to verify label changes.*

## Phase 5: Fix Auth Provider Metadata

**File**: `crates/ava-auth/src/lib.rs`

### 5a: Update Alibaba base URLs

```rust
// Alibaba (international) — line ~248
// BEFORE:
default_base_url: Some("https://dashscope-intl.aliyuncs.com/compatible-mode/v1"),
// AFTER:
default_base_url: Some("https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1"),

// Alibaba-CN — line ~257
// BEFORE:
default_base_url: Some("https://dashscope.aliyuncs.com/compatible-mode/v1"),
// AFTER:
default_base_url: Some("https://coding.dashscope.aliyuncs.com/apps/anthropic/v1"),
```

### 5b: Update description

```rust
description: "Alibaba Cloud Coding Plan (International)",
// and
description: "Alibaba Cloud Coding Plan (China mainland)",
```

*Before proceeding to Phase 6, invoke the Code Reviewer sub-agent to verify auth metadata is consistent with provider routing.*

## Phase 6: Verify & Test

### Compilation
```bash
cargo build --workspace 2>&1
```

### Tests
```bash
cargo test --workspace 2>&1
```

Pay special attention to:
- `ava-llm` provider creation tests (alibaba should now create Anthropic-compatible provider)
- `ava-config` registry/catalog tests (new model names should appear)
- `ava-auth` provider count test (still 23)

### Clippy
```bash
cargo clippy --workspace 2>&1
```

### Verification checklist
- [ ] `base_url_for_provider("alibaba")` returns `coding-intl.dashscope.aliyuncs.com` (not `dashscope-intl.aliyuncs.com`)
- [ ] `create_provider("alibaba", ...)` creates `AnthropicProvider` (not `OpenAIProvider`)
- [ ] Registry/catalog has coding plan models (not qwen3-coder-flash/qwen-turbo)
- [ ] `cost_display()` returns `""` for subscription models, not `"free"`
- [ ] Model selector labels don't say "(free)" for subscription providers
- [ ] Auth metadata URLs point to coding plan endpoints
- [ ] All 23 auth providers still present
- [ ] All 13 routable providers still have base URLs
- [ ] Model name normalization still works (aliases from registry)

*Invoke the Code Reviewer sub-agent for a FINAL pass over ALL changes. Verify consistency across ava-llm, ava-config, ava-auth, and ava-tui.*

## Acceptance Criteria

1. `cargo build --workspace` compiles cleanly
2. `cargo test --workspace` passes
3. `cargo clippy --workspace` is clean
4. Alibaba routes through `AnthropicProvider` with coding plan URL
5. Model list matches actual coding plan models from docs
6. No subscription provider shows "free" — display is empty for subscription-included
7. Section labels don't contain "(free)" for subscription providers
8. Auth metadata URLs match coding plan endpoints
