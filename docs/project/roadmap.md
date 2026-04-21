---
title: "Roadmap"
description: "Product direction for AVA 0.6 and the path from the current build to V1."
order: 2
updated: "2026-04-18"
---

# AVA Roadmap

## Goal

AVA 0.6 keeps the product focused around a sharper core:

1. A practical solo-first coding agent.
2. A strong plugin architecture for optional advanced capability.
3. A smaller, more opinionated core UX.
4. Clear docs that work for both humans and AI agents.

## Product Position

AVA should sit between OpenCode and PI Code.

That means:

1. More practical and lightweight than a full team-orchestration product.
2. More extensible and customizable than a minimal single-mode coding agent.
3. Opinionated by default, expandable when needed.

## Core AVA 0.6

Core AVA should include:

1. Solo-first agent runtime.
2. A **headless-first authoritative proof lane** for core reliability, with TUI, desktop, and web as lighter parity lanes. The headless lane is the proof source for backend correctness under the current non-interactive exception; parity lanes do not replace that proof.
3. TUI, desktop, and web access to the core workflow.
4. The default 9-tool surface.
5. Permissions, trust, and shell safety.
6. Session persistence.
7. Official providers only.
8. Core customization centered on `MCPs`, `Commands`, and `Skills`.
9. A plugin system that is stable and first-class.

## Plugin-First Growth

New major capability should default to plugin form unless there is a strong reason for core inclusion.

Examples:

1. HQ becomes a plugin.
2. Long-tail providers become provider packs or plugins later.
3. Plugin-owned settings and UI load only when the plugin is installed.

## Locked Decisions

### 1. HQ Leaves Core

1. HQ is no longer part of core AVA product.
2. HQ becomes an installable plugin later, only after core AVA is solid on its own.
3. Board, Director, Lead, Worker, team mode, worktrees, HQ settings, and HQ screens move with it.

### 2. Settings Collapse

Core settings are reduced to:

1. `General`
2. `Models`
3. `Tools`
4. `Permissions`
5. `Appearance`
6. `Advanced`

Long-tail toggles move to advanced-only, config-file-only, or plugin-owned settings.

### 3. Official Provider Set

Core providers:

1. Anthropic
2. OpenAI
3. Google Gemini
4. Ollama
5. OpenRouter
6. GitHub Copilot
7. Inception
8. Alibaba
9. ZAI/ZhipuAI
10. Kimi
11. MiniMax

Provider variants are folded into one provider entry where appropriate:

1. `OpenAI` absorbs `ChatGPT`
2. `Alibaba` absorbs `Alibaba CN`
3. `MiniMax` absorbs `MiniMax CN`
4. `ZAI` and `ZhipuAI` should be unified if implementation confirms one provider surface

Providers leaving core:

1. Azure OpenAI
2. AWS Bedrock
3. xAI
4. Mistral
5. Groq
6. DeepSeek
7. Mock

### 4. Docs And Positioning Reset

1. AVA stops presenting itself as an "AI dev team" by default.
2. AVA is framed as a practical solo-first coding agent.
3. Internal implementation details move out of the main product narrative.
4. The docs tree is rewritten around AVA 0.6 instead of patched indefinitely.

### 5. Surface-Area Discipline

1. Full parity across every surface is not an automatic goal.
2. Desktop and web can stay close.
3. TUI stays lighter.
4. Advanced/plugin features do not automatically get equal treatment everywhere.

### 6. Onboarding Simplifies

1. Onboarding becomes optional.
2. Users land in the real UI first.
3. Guidance reuses the actual product surfaces instead of a separate flow.

## Workstreams

### 1. Docs Reset

1. Replace stale docs with 0.6 docs.
2. Rewrite `README.md`, `docs/README.md`, and `CLAUDE.md` product-facing sections.
3. Delete docs that do not match the repo or the new product shape.
4. Keep `docs/architecture/entrypoints.md`, `docs/architecture/crate-map.md`, and `docs/architecture/shared-backend-contract-m6.md` as the canonical architecture owner docs; keep milestone artifacts as historical context.

### 2. Core/Product Boundary

1. Define what lives in core vs plugin.
2. Remove HQ assumptions from core docs and UX.
3. Ensure the crate graph reflects plugin boundaries for non-core features.
4. Build plugin host seams for backend commands, web routes, events, and frontend mounts so HQ can become a real installable plugin.

### Deferred Future Track: HQ Plugin Boundary

HQ plugin work is now a future-roadmap item, not the active 0.6 execution track.

The current goal is to keep core AVA stable and focused first.

When that is ready, the HQ follow-up can resume using the existing notes in
`docs/project/backlog.md` and `docs/architecture/plugin-boundary.md` as groundwork.

General backend contract unification work is tracked separately in the architecture milestone chain:
`docs/architecture/cross-surface-runtime-map-m4.md` -> `docs/architecture/cross-surface-behavior-audit-m5.md` -> `docs/architecture/shared-backend-contract-m6.md` -> `docs/architecture/backend-correction-roadmap-m7.md`.

### 3. Provider Cleanup

1. Prune long-tail providers from core.
2. Unify region/routing variants into one provider surface.
3. Establish an official-provider quality bar with tests and prompt tuning.
4. Keep model metadata repo-owned and manually curated rather than fetched from third-party catalogs at runtime.

### 4. HQ Config/Storage Cleanup

This is future work, not part of the current core-only push.

1. Remove HQ config ownership from core crates.
2. Keep only compatibility baggage that cannot be deleted safely yet.

### 5. Final Core HQ Surface Cleanup

This is future work, not part of the current core-only push.

1. Remove the last default-core HQ assumptions from frontend, config, and startup surfaces.
2. Keep only the compatibility baggage that cannot yet be removed safely.
3. Any still-dormant HQ runtime helpers should stay isolated inside `ava-hq` and not leak back into default-core paths.

### 6. Settings Cleanup

1. Collapse settings navigation.
2. Move plugin settings behind plugin registration.
3. Remove niche toggles from default visible UI.

### 7. Extension Model Cleanup

1. Keep plugin infrastructure first-class.
2. Keep the default visible customization model small.
3. Ensure install/enable flows are clean and Obsidian-like.
4. Keep MCP, plugins, and native/WASM extension descriptors as distinct concepts with clear ownership.

### 8. Benchmark And Prompt-Tuning Infrastructure

1. Keep headless interactive testing in the benchmark system rather than a separate harness.
2. Maintain explicit benchmark lanes: `tool_reliability` for deterministic tool-use correctness and `normal_coding` for implementation quality.
3. Keep tool-failure and coding-quality signals separate so prompt changes can be attributed cleanly.
4. Keep runtime model metadata repo-owned (`list_models` / curated catalog) with no `models.dev` runtime dependency.
5. Keep prompt notes separated by family/provider files so tuning is data-driven and easy to diff/review.
6. Tune prompts primarily by model family, then only add provider-specific refinements when transport/API behavior materially differs.
7. Use OpenCode as the primary benchmark and automation-comparison reference, with Goose as secondary reference for execution-mode and automation-pattern checks.

### 9. Safety Policy And Credential Guidance

1. Keep a model-specific doom-loop policy layer (`nudge, nudge, stop` for loop-prone families) on top of baseline stuck detection.
2. Treat doom-loop policy quality as benchmark-visible behavior, including cooldown safety and escalation-reset correctness.
3. Keep credential storage guidance explicit: plaintext local credential files are supported for compatibility, but keychain/encrypted or env-based paths are preferred.

## Immediate Priority

Right now, success means core AVA works well without optional HQ/plugin expansion pressure.

Immediate focus:

1. Stable core runtime behavior.
2. Clean provider selection and routing.
3. Predictable settings and onboarding.
4. Good default desktop/web/TUI UX for solo coding work.

## AVA 0.6 -> V1

AVA 0.6 is the current stabilization cycle before V1.

The goal of this cycle is simple:

1. Make the desktop app feel complete and trustworthy.
2. Prove the backend can do real coding work end to end, with headless benchmarks as the primary proof source of truth for backend correctness.
3. Confirm limited parity/smoke behavior for interactive approval/question/plan and core journeys on TUI/desktop/web, while treating headless benchmarks as the authoritative proof of coding correctness.
4. Add enough automation that V1 quality is demonstrated, not guessed.

### V1 Checklist

1. Desktop app feels polished for daily use.
2. All core tools and agent actions have understandable UI.
3. Users can work in multiple chats without losing run state or mixing outputs.
4. Headless benchmark flows remain the authoritative proof source for real-coding correctness.
5. TUI, desktop, and web are validated with parity/smoke checks for the core journey (`prompt -> tools -> edit -> verify -> persist`), not full end-to-end real-coding proof.
6. AVA has a repeatable comparison path with OpenCode as the primary backend/runtime reference and Goose as secondary automation-reference.
7. Docs and release language consistently describe the current product as `0.6` on the way to V1.

## Success Criteria

AVA 0.6 is successful when:

1. A new user can understand the product in one pass.
2. Core AVA feels focused without losing extensibility.
3. HQ and similar advanced systems no longer distort the default product story.
4. Official providers feel actively supported rather than merely available.
5. Docs reflect the real system clearly enough for both humans and AI agents.
