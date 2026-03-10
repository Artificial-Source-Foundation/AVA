# Sprint 54: Thinking/Reasoning Mode

## Goal
Add per-provider thinking/reasoning variant support with a TUI toggle, modeled after OpenCode's variant cycling system.

## Status: Planned

## Design (from OpenCode analysis)

### How OpenCode does it
- Each model has a `variants` map: `Record<string, Record<string, any>>` — key is effort level, value is provider options
- Variants are **cycled** with a button/keybind: off → low → medium → high → max → off
- Current variant is stored per `providerID/modelID` key
- The variant's provider options are merged into the API request

### Provider-specific thinking parameters

**Anthropic** (claude-opus-4.6, claude-sonnet-4.6):
- Adaptive thinking: `{ thinking: { type: "adaptive" }, effort: "low|medium|high|max" }`
- Older models: `{ thinking: { type: "enabled", budgetTokens: N } }`

**OpenAI** (GPT-5.x, Codex):
- `{ reasoningEffort: "none|minimal|low|medium|high|xhigh" }`
- Also: `reasoningSummary: "auto"`, `include: ["reasoning.encrypted_content"]`

**Google/Gemini**:
- Gemini 2.5: `{ thinkingConfig: { includeThoughts: true, thinkingBudget: N } }`
- Gemini 3.x: `{ thinkingConfig: { includeThoughts: true, thinkingLevel: "low|medium|high" } }`

**OpenRouter**:
- `{ reasoning: { effort: "none|minimal|low|medium|high|xhigh" } }`

## Implementation Plan

### Phase 1: Core types + provider support
- Add `ThinkingLevel` enum to `ava-types`: `Off, Low, Medium, High, Max`
- Add `thinking_options()` method to `LLMProvider` trait (returns available levels)
- Add `set_thinking_level()` to provider implementations
- Wire thinking params into request bodies for Anthropic, OpenAI, Gemini, OpenRouter

### Phase 2: State + persistence
- Add `ThinkingState` to `ava-agent` — current level per provider/model key
- Persist in session or config
- `AgentStack::set_thinking_level()` / `cycle_thinking()`

### Phase 3: TUI integration
- Wire `Action::ToggleThinking` (Ctrl+T already bound) to cycle levels
- Status bar shows current thinking level
- `/think [level]` slash command
- Display thinking content in chat (collapsed by default)

## Files in scope
- `crates/ava-types/src/lib.rs` — ThinkingLevel enum
- `crates/ava-llm/src/provider.rs` — LLMProvider trait additions
- `crates/ava-llm/src/providers/anthropic.rs` — adaptive thinking
- `crates/ava-llm/src/providers/openai.rs` — reasoningEffort
- `crates/ava-llm/src/providers/gemini.rs` — thinkingConfig
- `crates/ava-llm/src/providers/openrouter.rs` — reasoning.effort
- `crates/ava-agent/src/stack.rs` — ThinkingState, cycle
- `crates/ava-tui/src/app/commands.rs` — /think command
- `crates/ava-tui/src/app/mod.rs` — Ctrl+T handler
- `crates/ava-tui/src/widgets/status_bar.rs` — thinking indicator
