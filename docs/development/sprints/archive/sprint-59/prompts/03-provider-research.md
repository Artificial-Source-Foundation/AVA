# Sprint 59-03: Provider Logic Deep-Dive — OpenCode + Reference Projects

## Context

You are working on **AVA**, a Rust-first AI coding agent. Read `CLAUDE.md` and `AGENTS.md` first. This is a **read-only research sprint** — you will NOT modify any code. Your job is to deeply analyze how other tools handle LLM providers and produce a structured comparison report so we can improve AVA's provider layer.

AVA's provider implementation lives in:
- `crates/ava-llm/src/providers/` — LLM providers (anthropic, openai, gemini, ollama, openrouter)
- `crates/ava-llm/src/provider.rs` — LLMProvider trait
- `crates/ava-llm/src/providers/common.rs` — shared utilities (tool format conversion, usage parsing, retry)
- `crates/ava-llm/src/pool.rs` — connection pooling
- `crates/ava-llm/src/retry.rs` — RetryBudget
- `crates/ava-llm/src/circuit_breaker.rs` — CircuitBreaker
- `crates/ava-config/src/model_catalog/` — model catalog (fallback, fetch, types)
- `crates/ava-config/src/credentials.rs` — credential store

We have 12 reference codebases in `docs/reference-code/`. You will scrape the provider logic from each, focusing primarily on **OpenCode** (the most mature), then extracting key patterns from the others.

---

## Phase 1: OpenCode Provider Architecture (PRIMARY)

OpenCode is our primary reference. Read ALL these files thoroughly and take detailed notes:

### Core Provider Layer
1. **`docs/reference-code/opencode/packages/opencode/src/provider/provider.ts`** (~49KB) — Main provider implementation. Extract:
   - Provider abstraction design (how providers are registered, selected, configured)
   - Request/response transformation pipeline
   - Streaming implementation
   - Error handling and retry logic
   - Token counting / cost tracking
   - How thinking/reasoning is handled per provider
   - Rate limiting / backpressure

2. **`docs/reference-code/opencode/packages/opencode/src/provider/transform.ts`** (~32KB) — Data transformation. Extract:
   - Message format conversion (internal → provider-specific)
   - Tool call format differences between providers
   - How images/multimodal content is handled
   - How reasoning/thinking content is preserved across turns
   - Opaque signature re-submission logic

3. **`docs/reference-code/opencode/packages/opencode/src/provider/error.ts`** (~7KB) — Error handling. Extract:
   - Error classification (retryable vs fatal)
   - Rate limit handling (429 responses)
   - Provider-specific error parsing
   - How errors are surfaced to the user

4. **`docs/reference-code/opencode/packages/opencode/src/provider/auth.ts`** (~4KB) — Auth layer. Extract:
   - How auth is layered on top of providers
   - Token refresh logic
   - OAuth vs API key handling

5. **`docs/reference-code/opencode/packages/opencode/src/provider/models.ts`** (~4KB) — Model definitions. Extract:
   - How models are defined and configured
   - Cost information structure
   - Context window / output limit tracking
   - Model capability flags (tool_call, vision, reasoning)

### Copilot SDK (Already partially covered in Sprint 59-01, but go deeper)
6. **`docs/reference-code/opencode/packages/opencode/src/provider/sdk/copilot/copilot-provider.ts`** — Copilot provider
7. **`docs/reference-code/opencode/packages/opencode/src/provider/sdk/copilot/chat/openai-compatible-chat-language-model.ts`** (~27KB) — Chat model implementation. Extract:
   - How streaming SSE is parsed
   - How tool calls are assembled from streaming chunks
   - How reasoning_text and reasoning_opaque are handled
   - Finish reason mapping
8. **`docs/reference-code/opencode/packages/opencode/src/provider/sdk/copilot/responses/openai-responses-language-model.ts`** (~58KB) — OpenAI Responses API. Extract:
   - How the Responses API differs from Chat Completions
   - Tool preparation differences
   - Response format handling

### Plugin System
9. **`docs/reference-code/opencode/packages/opencode/src/plugin/copilot.ts`** — Copilot plugin (token exchange, headers, x-initiator)

### Provider Tests
10. **`docs/reference-code/opencode/packages/opencode/test/provider/provider.test.ts`** — Extract:
    - What they test (unit vs integration)
    - Mock strategies
    - Edge cases covered

**Before proceeding to Phase 2, invoke the Code Reviewer sub-agent to verify you've captured ALL key patterns from OpenCode's provider layer. The review should check that you haven't missed any important files or patterns.**

---

## Phase 2: Goose Provider Architecture (Rust Reference)

Goose is our closest Rust reference. Read these files:

1. **`docs/reference-code/goose/crates/goose/src/providers/provider_registry.rs`** — Extract:
   - How providers are registered and discovered
   - Provider selection logic
   - How it compares to our `create_provider()` factory

2. **`docs/reference-code/goose/crates/goose/src/providers/canonical/model.rs`** — Extract:
   - Model abstraction
   - How model capabilities are tracked

3. **`docs/reference-code/goose/crates/goose/src/providers/canonical/data/provider_metadata.json`** — Extract:
   - Provider metadata schema
   - What metadata is tracked per provider

4. **`docs/reference-code/goose/crates/goose/src/model.rs`** — Extract:
   - Model configuration
   - Provider-model relationship

5. **`docs/reference-code/goose/crates/goose/src/providers/githubcopilot.rs`** — Extract:
   - Copilot-specific patterns in Rust (already read in 59-01, but note any patterns we missed)

6. **`docs/reference-code/goose/crates/goose/tests/providers.rs`** — Extract:
   - Provider testing patterns

**Before proceeding to Phase 3, invoke the Code Reviewer sub-agent to verify Goose analysis is complete.**

---

## Phase 3: Other Reference Projects (Key Patterns Only)

For each of these, do a focused scan — don't read every line, just extract notable patterns that differ from or improve upon what OpenCode and Goose do.

### pi-mono (TypeScript)
- `docs/reference-code/pi-mono/packages/ai/src/models.generated.ts` — How model metadata is generated/managed
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/model-resolver.ts` — Model resolution logic
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/model-registry.ts` — Registry pattern
- `docs/reference-code/pi-mono/packages/ai/src/providers/github-copilot-headers.ts` — Header construction patterns

### continue (TypeScript)
- `docs/reference-code/continue/core/llm/llms/llm.ts` — LLM base class / abstraction
- `docs/reference-code/continue/gui/src/pages/AddNewModel/configs/providers.ts` — Provider configuration UI data
- `docs/reference-code/continue/packages/config-yaml/src/schemas/models.ts` — Model schema

### zed (Rust — second Rust reference)
- `docs/reference-code/zed/crates/language_models/src/provider.rs` — Provider trait in Rust
- `docs/reference-code/zed/crates/agent_ui/src/model_selector.rs` — Model selector UI (Rust)
- `docs/reference-code/zed/crates/bedrock/src/models.rs` — Bedrock model definitions

### aider (Python)
- `docs/reference-code/aider/aider/models.py` — Model configuration
- `docs/reference-code/aider/aider/resources/model-metadata.json` — Model metadata schema
- `docs/reference-code/aider/aider/resources/model-settings.yml` — Model settings

### gemini-cli (TypeScript)
- `docs/reference-code/gemini-cli/packages/core/src/routing/modelRouterService.ts` — Model routing
- `docs/reference-code/gemini-cli/packages/core/src/availability/modelAvailabilityService.ts` — Model availability checking
- `docs/reference-code/gemini-cli/packages/core/src/config/models.ts` — Model config

**Before proceeding to Phase 4, invoke the Code Reviewer sub-agent to verify you've extracted the most valuable patterns from each project.**

---

## Phase 4: Compare with AVA's Implementation

Now read AVA's provider layer in detail:
- `crates/ava-llm/src/provider.rs` — LLMProvider trait
- `crates/ava-llm/src/providers/anthropic.rs` — Anthropic provider
- `crates/ava-llm/src/providers/openai.rs` — OpenAI provider
- `crates/ava-llm/src/providers/openrouter.rs` — OpenRouter provider
- `crates/ava-llm/src/providers/gemini.rs` — Gemini provider
- `crates/ava-llm/src/providers/common.rs` — Shared utilities
- `crates/ava-llm/src/providers/mod.rs` — Provider factory
- `crates/ava-llm/src/pool.rs` — Connection pool
- `crates/ava-llm/src/retry.rs` — RetryBudget
- `crates/ava-llm/src/circuit_breaker.rs` — CircuitBreaker
- `crates/ava-config/src/model_catalog/` — All files

Produce a detailed comparison across these dimensions:

### Comparison Matrix

For each dimension, score AVA vs OpenCode (our primary target):

1. **Provider Abstraction**
   - Trait design (our LLMProvider trait vs their abstraction)
   - Method surface area (what we expose vs what they expose)
   - Extensibility (how easy to add a new provider)

2. **Request Pipeline**
   - Message format conversion
   - Tool format conversion
   - Thinking/reasoning format handling
   - Image/multimodal handling
   - Caching headers (prompt caching, etc.)

3. **Response Pipeline**
   - Streaming SSE parsing
   - Tool call assembly from streaming chunks
   - Usage/token extraction
   - Reasoning content handling (thinking blocks, opaque signatures)
   - Finish reason mapping

4. **Error Handling**
   - Error classification
   - Retry logic (our RetryBudget vs their approach)
   - Rate limit handling (429)
   - Provider-specific error parsing
   - Circuit breaker pattern (do they have one?)

5. **Connection Management**
   - Pooling strategy (our ConnectionPool vs their approach)
   - Pre-warming
   - Timeout handling

6. **Model Catalog**
   - Dynamic vs static model lists
   - Model capability tracking (tool_call, vision, reasoning)
   - Cost information
   - Context window / output limit management
   - How stale model data is handled

7. **Auth / Credentials**
   - OAuth flow handling
   - Token refresh
   - Multi-auth support (API key + OAuth on same provider)
   - Credential validation

8. **Provider-Specific Features**
   - Anthropic: prompt caching, extended thinking, system prompts
   - OpenAI: reasoning effort, Responses API, structured output
   - Copilot: sub-agent headers, token exchange
   - Gemini: safety settings, grounding
   - OpenRouter: model routing, fallback

9. **Testing**
   - Unit test coverage
   - Mock strategies
   - Integration test patterns

10. **Features We're Missing**
    - Things OpenCode or others have that we don't
    - Patterns that would improve our implementation

---

## Phase 5: Write Report

Write the full comparison report to `docs/development/sprints/sprint-59/results/03-provider-research.md`.

### Report Structure

```markdown
# Provider Logic Research Report

## Executive Summary
- 3-5 bullet points of the most impactful findings
- Top 3 improvements we should make

## 1. OpenCode Provider Architecture
### Overview
### Key Design Patterns
### Notable Code Snippets (with file paths)

## 2. Goose Provider Architecture
### Overview
### Key Design Patterns (Rust-specific)

## 3. Other Projects — Key Patterns
### pi-mono
### continue
### zed
### aider
### gemini-cli

## 4. Comparison Matrix
(The 10-dimension comparison from Phase 4, with scores and notes)

## 5. What AVA Does Better
(Things we already do well — connection pooling, circuit breaker, etc.)

## 6. What We Should Adopt
### P0 — Critical improvements
### P1 — High-value improvements
### P2 — Nice-to-have improvements

## 7. Recommended Sprint Work
(Concrete sprint suggestions with estimated scope)
```

Make the report thorough but actionable. Every finding should map to either "we already do this well" or "here's specifically what we should change and why."

**Invoke the Code Reviewer sub-agent for a FINAL review of the report. Verify:**
1. All key OpenCode provider files were analyzed
2. The comparison matrix is complete and fair
3. Recommendations are specific and actionable (not vague)
4. File paths are correct for all referenced code
5. No major patterns were missed from any reference project

---

## Output

Single deliverable: `docs/development/sprints/sprint-59/results/03-provider-research.md`

This is a **read-only research sprint**. Do NOT modify any source code.

## Acceptance Criteria

- [ ] Report written to `docs/development/sprints/sprint-59/results/03-provider-research.md`
- [ ] OpenCode's 5 core provider files fully analyzed
- [ ] OpenCode's Copilot SDK files analyzed
- [ ] Goose's Rust provider files analyzed
- [ ] At least 5 other reference projects scanned for key patterns
- [ ] 10-dimension comparison matrix complete
- [ ] Concrete P0/P1/P2 improvement recommendations with estimated scope
- [ ] All file paths in the report are valid
