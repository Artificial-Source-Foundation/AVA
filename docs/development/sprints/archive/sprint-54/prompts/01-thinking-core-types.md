# Sprint 54, Phase 1: Thinking/Reasoning Core Types + Provider Support

## Context

AVA is a Rust-first AI coding agent (~21 crates, Ratatui TUI, Tokio async). See `CLAUDE.md` for conventions.

We're adding thinking/reasoning mode support, modeled after OpenCode's variant system. Different providers use different APIs for controlling how much the model "thinks":

- **Anthropic** (Opus 4.6, Sonnet 4.6): `thinking: { type: "adaptive" }` + `effort` level
- **Anthropic** (older): `thinking: { type: "enabled", budgetTokens: N }`
- **OpenAI** (GPT-5.x, Codex): `reasoningEffort: "none|minimal|low|medium|high|xhigh"`
- **Google/Gemini 2.5**: `thinkingConfig: { includeThoughts: true, thinkingBudget: N }`
- **Google/Gemini 3.x**: `thinkingConfig: { includeThoughts: true, thinkingLevel: "low|high" }`
- **OpenRouter**: `reasoning: { effort: "low|medium|high" }` (proxy — wraps above)
- **Ollama**: Not supported (no thinking API)

## Task 0: Competitive Research — MUST DO FIRST

Before writing any code, study how competitors implement thinking modes. Read these files in `docs/reference-code/` and take notes on patterns, API formats, UX decisions, and edge cases:

### OpenCode (primary reference)
- `docs/reference-code/opencode/packages/opencode/src/provider/transform.ts` — **the `variants()` function** is the core. Shows exactly how thinking levels map to provider-specific options per SDK (Anthropic, OpenAI, Google, OpenRouter, Bedrock, etc.). Also study `options()` for default thinking params and `smallOptions()` for reduced-thinking mode.
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/local.tsx` — the `variant` object shows UX: `current()`, `list()`, `set()`, `cycle()`. Variants stored per `providerID/modelID` key. Cycle wraps around to undefined (off).
- `docs/reference-code/opencode/packages/opencode/src/provider/provider.ts` — model capabilities, how `reasoning: boolean` flag is used, model variant resolution.
- `docs/reference-code/opencode/packages/app/e2e/thinking-level.spec.ts` — E2E test for variant cycling UI.
- `docs/reference-code/opencode/packages/opencode/src/session/prompt.ts` — how thinking content appears in session messages.

### Claude Code / pi-mono
- `docs/reference-code/pi-mono/packages/coding-agent/src/modes/interactive/components/thinking-selector.ts` — thinking selector UI component.
- `docs/reference-code/pi-mono/packages/ai/test/interleaved-thinking.test.ts` — how interleaved thinking is tested.
- `docs/reference-code/pi-mono/packages/coding-agent/test/compaction-thinking-model.test.ts` — thinking + context compaction interaction.

### Goose
- `docs/reference-code/goose/crates/goose-cli/src/session/thinking.rs` — **Rust implementation** of thinking in a CLI agent. Most directly relevant to our architecture.

### Zed
- `docs/reference-code/zed/assets/icons/thinking_mode.svg` and `thinking_mode_off.svg` — UI indicator icons.

### Key questions to answer from research:
1. What thinking levels does each competitor expose? (OpenCode: none/minimal/low/medium/high/xhigh — varies by provider)
2. How is thinking state persisted? (OpenCode: per provider/model key in local store)
3. How is thinking content displayed? (Collapsed? Separate panel? Inline?)
4. What happens to thinking content during context compaction?
5. Are there default thinking levels per model? (OpenCode: GPT-5 defaults to "medium", Gemini 3 defaults to "high")
6. How does Goose do it in Rust specifically?

**Document findings as comments in your code or as brief notes before implementing. The research should directly inform implementation decisions.**

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify your research notes are accurate against the source files. Fix any misreadings before moving on.**

## Task 1: Add ThinkingLevel to ava-types

File: `crates/ava-types/src/lib.rs`

Add a new enum:

```rust
/// Thinking/reasoning effort level for models that support extended thinking.
/// Maps to provider-specific parameters (Anthropic adaptive thinking,
/// OpenAI reasoningEffort, Gemini thinkingConfig, etc.)
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize, Default)]
pub enum ThinkingLevel {
    /// No thinking (default behavior)
    #[default]
    Off,
    /// Minimal reasoning
    Low,
    /// Moderate reasoning
    Medium,
    /// Full reasoning
    High,
    /// Maximum reasoning budget
    Max,
}

impl ThinkingLevel {
    /// Cycle to next level: Off → Low → Medium → High → Max → Off
    pub fn cycle(self) -> Self {
        match self {
            Self::Off => Self::Low,
            Self::Low => Self::Medium,
            Self::Medium => Self::High,
            Self::High => Self::Max,
            Self::Max => Self::Off,
        }
    }

    /// Display label for status bar
    pub fn label(self) -> &'static str {
        match self {
            Self::Off => "off",
            Self::Low => "low",
            Self::Medium => "med",
            Self::High => "high",
            Self::Max => "max",
        }
    }

    /// Parse from string (for /think command)
    pub fn from_str_loose(s: &str) -> Option<Self> {
        match s.to_lowercase().as_str() {
            "off" | "none" | "0" => Some(Self::Off),
            "low" | "l" | "1" | "minimal" => Some(Self::Low),
            "medium" | "med" | "m" | "2" => Some(Self::Medium),
            "high" | "h" | "3" => Some(Self::High),
            "max" | "x" | "xhigh" | "4" => Some(Self::Max),
            _ => None,
        }
    }
}

impl std::fmt::Display for ThinkingLevel {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.label())
    }
}
```

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 2: Extend LLMProvider trait

File: `crates/ava-llm/src/provider.rs`

Add to `LLMResponse`:
```rust
pub struct LLMResponse {
    pub content: String,
    pub tool_calls: Vec<ToolCall>,
    pub usage: Option<TokenUsage>,
    /// Thinking/reasoning content from models that support extended thinking.
    /// This is the model's internal reasoning, separate from the main response.
    pub thinking: Option<String>,
}
```

Add to `LLMProvider` trait (with default implementations):
```rust
/// Whether this provider/model supports thinking/reasoning modes.
fn supports_thinking(&self) -> bool {
    false
}

/// Available thinking levels for the current model.
/// Returns empty slice if thinking not supported.
fn thinking_levels(&self) -> &[ThinkingLevel] {
    &[]
}

/// Generate with tools AND thinking level.
/// Default falls back to generate_with_tools ignoring thinking.
async fn generate_with_thinking(
    &self,
    messages: &[Message],
    tools: &[Tool],
    thinking: ThinkingLevel,
) -> Result<LLMResponse> {
    // Default: ignore thinking level
    self.generate_with_tools(messages, tools).await
}
```

Also update `SharedProvider` to delegate the new methods.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 3: Anthropic provider — adaptive thinking

File: `crates/ava-llm/src/providers/anthropic.rs`

The Anthropic API supports extended thinking for Opus 4.6 and Sonnet 4.6 via adaptive mode.

Read the current file first. Then:

1. Add a constant for models supporting adaptive thinking:
```rust
const ADAPTIVE_THINKING_MODELS: &[&str] = &[
    "claude-opus-4-6", "claude-opus-4.6",
    "claude-sonnet-4-6", "claude-sonnet-4.6",
];
```

2. Implement `supports_thinking()` — return true if model is in the list.

3. Implement `thinking_levels()` — return `&[Low, Medium, High, Max]` for adaptive models.

4. Implement `generate_with_thinking()`:
   - Build the request body same as `generate_with_tools()`
   - When thinking != Off, add to the request JSON:
     ```json
     {
       "thinking": { "type": "adaptive" },
       "temperature": 1,
       "budget_tokens": <mapped from level>
     }
     ```
   - Budget mapping: Low=4000, Medium=8000, High=16000, Max=32000
   - When thinking is enabled, `max_tokens` must be at least `budget_tokens + 1`
   - Parse thinking content from response: look for `content` blocks with `type: "thinking"`
   - Return thinking text in `LLMResponse.thinking`

5. For non-adaptive models (older Claude), fall back to standard `generate_with_tools()`.

**Important Anthropic API details:**
- When `thinking` is enabled, `temperature` MUST be set to `1`
- Response content blocks include `{"type": "thinking", "thinking": "..."}` blocks
- The thinking blocks come before the text/tool_use blocks

**CRITICAL: Invoke the Code Reviewer sub-agent now.** This is the most complex provider change — review the request body construction, the budget_tokens mapping, max_tokens adjustment, thinking block parsing, and verify the Anthropic API contract matches their latest docs. Cross-reference with OpenCode's `variants()` for `@ai-sdk/anthropic` in `docs/reference-code/opencode/packages/opencode/src/provider/transform.ts`. Fix any issues before moving on.

## Task 4: OpenAI provider — reasoning effort

File: `crates/ava-llm/src/providers/openai.rs`

Read the current file first. Then:

1. Add model detection for reasoning-capable models:
```rust
fn supports_reasoning(model: &str) -> bool {
    let m = model.to_lowercase();
    m.contains("gpt-5") || m.contains("codex") || m.starts_with("o3") || m.starts_with("o4")
}
```

2. Implement `supports_thinking()`, `thinking_levels()`.

3. Implement `generate_with_thinking()`:
   - Add `reasoning_effort` to request body when thinking != Off:
     ```json
     { "reasoning_effort": "low|medium|high" }
     ```
   - Level mapping: Low="low", Medium="medium", High="high", Max="high" (OpenAI max is "high" or "xhigh" for some models — use "high" as safe default)
   - Also consider: `reasoningSummary: "auto"` and `include: ["reasoning.encrypted_content"]` — check what OpenCode does in `variants()` for `@ai-sdk/openai`
   - Parse reasoning from response: OpenAI returns reasoning in message content or as separate field depending on model

**CRITICAL: Invoke the Code Reviewer sub-agent now.** Second-most complex provider. Verify the reasoning_effort values match OpenAI's API, cross-reference with OpenCode's `variants()` for `@ai-sdk/openai` and `options()` for default GPT-5 reasoning settings. Fix any issues before moving on.

## Task 5: Gemini provider — thinking config

File: `crates/ava-llm/src/providers/gemini.rs`

Read the current file first. Then:

1. Implement `supports_thinking()` — true for gemini-2.5-* and gemini-3-* models.

2. Implement `generate_with_thinking()`:
   - For Gemini 2.5: add `thinkingConfig: { includeThoughts: true, thinkingBudget: N }`
     - Low=4000, Medium=8000, High=16000, Max=24576
   - For Gemini 3.x: add `thinkingConfig: { includeThoughts: true, thinkingLevel: "low|high" }`
   - Parse thinking from response: Gemini returns `thought: true` on content parts

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 6: OpenRouter provider — reasoning proxy

File: `crates/ava-llm/src/providers/openrouter.rs`

Read the current file first. Then:

1. `supports_thinking()` — true if underlying model supports it (check model ID for claude/gpt-5/gemini patterns).

2. `generate_with_thinking()`:
   - Add `reasoning: { effort: "low|medium|high" }` to request `provider` options
   - OpenRouter passes this through to the underlying provider

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 7: Tests

1. `crates/ava-types/` — test ThinkingLevel::cycle(), from_str_loose(), Display
2. `crates/ava-llm/tests/` — test that providers return correct thinking_levels(), that request bodies include thinking params
3. Run: `cargo test --workspace` — all must pass
4. Run: `cargo clippy --workspace` — clean

## Acceptance Criteria
- [ ] `ThinkingLevel` enum in ava-types with cycle, parse, display
- [ ] `LLMResponse.thinking` field for thinking content
- [ ] `LLMProvider` trait has `supports_thinking()`, `thinking_levels()`, `generate_with_thinking()`
- [ ] Anthropic: adaptive thinking for Opus/Sonnet 4.6, budget tokens, parses thinking blocks
- [ ] OpenAI: reasoningEffort for GPT-5/Codex models
- [ ] Gemini: thinkingConfig with budget/level
- [ ] OpenRouter: reasoning.effort proxy
- [ ] All tests pass, clippy clean

## Final Code Review
After all changes, invoke the Code Reviewer sub-agent for a final pass over every modified file.
