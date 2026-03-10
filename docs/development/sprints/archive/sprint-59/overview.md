# Sprint 59: Provider Mega — Copilot, Verification, Research, Internals

## Goal

Full provider layer overhaul: add GitHub Copilot, verify all existing providers, research competitors, then implement the top improvements (retry jitter, circuit breaker, rich streaming, compiled model registry).

## Prompts

| # | Name | Type | Status |
|---|------|------|--------|
| 03 | `03-provider-research.md` | Read-only research | **Complete** |
| 01 | `01-copilot-provider.md` | Implementation | **Complete** |
| 02 | `02-provider-verification.md` | Implementation + tests | **Complete** |
| 04 | `04-provider-internals.md` | Implementation (mega) | **Complete** |
| 05 | `05-alibaba-hotfix.md` | Hotfix (critical bugs) | **Complete** |

### Execution Order
1. **03** (research) → done
2. **01 + 02** (parallel) → done
3. **05** (Alibaba hotfix — critical, run before 04)
4. **04** (retry jitter + circuit breaker + model registry + rich streaming)

### What's in 04 (combined from old 04+05+06)

| Phase | Focus | Scope |
|-------|-------|-------|
| 1 | Retry jitter (±20%) | `retry.rs` — prevent thundering herd |
| 2 | Circuit breaker integration | Wire into 5 remote API providers |
| 3 | Compiled-in model registry | `registry.json` via `include_str!`, pricing delegation, name normalization, fallback catalog dedup |
| 4 | Rich streaming `StreamChunk` | Replace `Stream<Item=String>` with `Stream<Item=StreamChunk>` (content, tool calls, usage, thinking) across all 7 providers + consumers |

## Results

| # | File | Status |
|---|------|--------|
| 03 | `results/03-provider-research.md` (32KB) | Complete |

## Key Findings from Research

**What AVA does better**: Circuit breaker (unique), connection pooling, 4-format thinking support

**What to adopt** (all in prompt 04):
- P0: Retry jitter + circuit breaker wiring
- P1: Rich streaming with `StreamChunk`
- P1: Compiled-in model registry + name normalization

## Status: Complete (all 5 prompts done)
