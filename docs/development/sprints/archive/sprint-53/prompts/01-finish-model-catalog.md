# Sprint 53: Finish Dynamic Model Catalog

## Context

AVA is a Rust-first AI coding agent with a TUI (Ratatui). We're replacing hardcoded model lists with dynamic fetching from `https://models.dev/api.json`.

**Architecture**: ~21 Rust crates. All new code MUST be Rust. See `CLAUDE.md` for full conventions.

**Current state**: The catalog infrastructure is built and working:
- `crates/ava-config/src/model_catalog.rs` — fetches, caches, parses models.dev
- `from_raw()` scans ALL hosting providers (zenmux, fastrouter, io-net, etc.) for models by ID prefix
- Curated whitelist (`CURATED_MODELS`) filters to coding-focused models
- Model selector in TUI uses catalog via `from_catalog()`
- Tests pass: `cargo test -p ava-config`

**Problem**: The whitelist is missing several models that exist in models.dev, and model IDs from models.dev (dots: `claude-sonnet-4.6`) don't match what direct provider APIs expect (dashes: `claude-sonnet-4-6`).

## Task 1: Update the curated whitelist

Edit `crates/ava-config/src/model_catalog.rs`, the `CURATED_MODELS` constant.

**Add these OpenAI models** (confirmed in models.dev):
- `gpt-5.2-pro` — GPT-5.2 Pro
- `gpt-5.2` — GPT-5.2
- `gpt-5` — GPT-5
- `gpt-5.1-chat` — GPT-5.1 Chat

Note: GPT-5.3 and GPT-5.4 do NOT exist in models.dev as of now. If they appear later, the background refresh will pick them up once added to the whitelist.

**Current whitelist for reference:**
```rust
const CURATED_MODELS: &[(&str, &[&str])] = &[
    ("anthropic", &[
        "claude-opus-4.6",
        "claude-sonnet-4.6",
        "claude-sonnet-4.5",
        "claude-haiku-4.5",
    ]),
    ("openai", &[
        "gpt-5.2-codex",
        "gpt-5.1-codex",
        "gpt-5.1-codex-mini",
        "gpt-5.1",
        "gpt-5-codex",
        "gpt-4.1",
    ]),
    ("google", &[
        "gemini-2.5-pro",
        "gemini-2.5-flash",
        "gemini-3-pro-preview",
        "gemini-3-flash-preview",
    ]),
];
```

**Ordering**: Most capable/newest first within each provider.

## Task 2: Model ID mapping for direct API calls

models.dev uses dot-notation (`claude-sonnet-4.6`) but the direct Anthropic API expects dash-notation (`claude-sonnet-4-6`). OpenAI's API may accept dots. OpenRouter expects `provider/model` format which matches models.dev.

Add a method to `CatalogModel`:

```rust
/// Return the model ID suitable for the given AVA provider.
/// For OpenRouter, returns "provider/id" format.
/// For direct providers, maps models.dev IDs to API-expected IDs.
pub fn api_model_id(&self, ava_provider: &str) -> String {
    match ava_provider {
        "openrouter" => format!("{}/{}", self.provider_id, self.id),
        "anthropic" => {
            // models.dev uses dots, Anthropic API uses dashes
            // claude-sonnet-4.6 → claude-sonnet-4-6
            // claude-opus-4.6 → claude-opus-4-6
            // claude-haiku-4.5 → claude-haiku-4-5-20251001
            self.anthropic_api_id()
        }
        _ => self.id.clone(),
    }
}
```

Also add a helper:
```rust
fn anthropic_api_id(&self) -> String {
    // Map models.dev display IDs to actual Anthropic API model IDs
    match self.id.as_str() {
        "claude-opus-4.6" => "claude-opus-4-6".to_string(),
        "claude-sonnet-4.6" => "claude-sonnet-4-6".to_string(),
        "claude-sonnet-4.5" => "claude-sonnet-4-20250514".to_string(),
        "claude-haiku-4.5" => "claude-haiku-4-5-20251001".to_string(),
        other => other.replace('.', "-"),
    }
}
```

## Task 3: Update model selector to use `api_model_id()`

In `crates/ava-tui/src/widgets/model_selector.rs`, the `build_model_list_from_catalog()` function.

Currently the `ModelOption.model` field stores `cm.id` (models.dev ID). Change it to use `cm.api_model_id(ava_provider)` so when the user selects a model, the correct API ID is passed to the provider.

For direct providers:
```rust
model: cm.api_model_id(ava_provider),
```

For OpenRouter section:
```rust
model: cm.api_model_id("openrouter"),
```

## Task 4: Update fallback catalog IDs

The `fallback_catalog()` function in `model_catalog.rs` should use models.dev IDs (dots) since `api_model_id()` now handles the mapping. This is already the case — just verify consistency.

## Task 5: Tests

1. Add test for `api_model_id()` mapping:
   - anthropic provider: `claude-sonnet-4.6` → `claude-sonnet-4-6`
   - openrouter: returns `anthropic/claude-sonnet-4.6`
   - openai: returns ID as-is

2. Run `cargo test -p ava-config` — all must pass
3. Run `cargo test -p ava-tui` — all must pass
4. Run `cargo clippy --workspace` — no warnings

**Before proceeding to the next phase, invoke the Code Reviewer sub-agent to verify all changes from this phase are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Acceptance Criteria
- [ ] Whitelist includes all confirmed models.dev OpenAI models
- [ ] `api_model_id()` correctly maps for anthropic, openrouter, and pass-through for others
- [ ] Model selector passes correct API IDs to providers
- [ ] All existing tests pass
- [ ] New tests for ID mapping
- [ ] `cargo clippy --workspace` clean

## Final Code Review
After all changes, invoke the Code Reviewer sub-agent for a final pass over every modified file, checking:
- Correct Rust idioms and error handling
- No hardcoded model IDs leaked outside the mapping layer
- Consistent provider_id usage throughout
- Tests cover edge cases
