# Sprint 53: Dynamic Model Catalog

## Goal
Replace hardcoded model lists with dynamic models.dev API fetching, curated to coding-focused models only.

## Status: In Progress

## What's Done
- [x] `CatalogState` with `Arc<RwLock<ModelCatalog>>`, 60-min background refresh, 10s timeout
- [x] Local cache at `~/.ava/cache/models.json`
- [x] Fallback catalog for offline use
- [x] `from_raw()` rewritten to scan across ALL hosting providers (zenmux, fastrouter, io-net, etc.)
- [x] Deduplication across hosting providers
- [x] Curated whitelist approach (CURATED_MODELS constant)
- [x] Model selector uses catalog (`from_catalog()`)
- [x] Ctrl+C quit behavior (OpenCode-style: clear input → quit if empty)
- [x] Model selector scrolling with `ensure_visible()`
- [x] Google → Gemini provider mapping
- [x] 8 unit tests passing

## What's Left
- [ ] Update whitelist: add missing OpenAI models (GPT-5.3 Codex Spark, GPT-5.4, GPT-5.2-Pro, GPT-5.2)
- [ ] Model ID mapping: models.dev uses dots (claude-sonnet-4.6) vs Anthropic API dashes (claude-sonnet-4-6)
- [ ] Cost display in model selector
- [ ] Commit all changes

## Files
- `crates/ava-config/src/model_catalog.rs` — catalog fetch, parse, cache, whitelist
- `crates/ava-config/src/lib.rs` — exports
- `crates/ava-tui/src/widgets/model_selector.rs` — UI using catalog
- `crates/ava-tui/src/app/mod.rs` — CatalogState in AppState, quit behavior
- `crates/ava-tui/src/app/modals.rs` — scroll handling
- `crates/ava-tui/src/app/commands.rs` — Ctrl+M uses catalog
- `crates/ava-tui/src/state/keybinds.rs` — removed Ctrl+D quit
