# Sprint 54, Phase 2: Thinking State + TUI Integration

## Context

AVA is a Rust-first AI coding agent (~21 crates, Ratatui TUI, Tokio async). See `CLAUDE.md` for conventions.

**Prerequisite**: Phase 1 (prompt 01) is complete. The following already exist:
- `ThinkingLevel` enum in `ava-types` with cycle/parse/display
- `LLMResponse.thinking` field
- `LLMProvider` trait: `supports_thinking()`, `thinking_levels()`, `generate_with_thinking()`
- Provider implementations for Anthropic, OpenAI, Gemini, OpenRouter

This phase wires thinking into the agent loop and TUI.

## Task 0: Phase 1 Fixes + Re-research Thinking Levels

**Phase 1 left issues that MUST be fixed before proceeding:**

### 0a: Fix clippy warning in model_catalog.rs
File: `crates/ava-config/src/model_catalog.rs`, line ~218.
The `from_raw()` method iterates with `for (_hosting_provider, provider_data) in &raw` — clippy warns about unused map key. Change to `for provider_data in raw.values()`.

### 0b: Re-research and fix OpenAI thinking levels
**Phase 1 excluded `Max` from OpenAI's `thinking_levels()` claiming OpenAI doesn't support "xhigh". This is WRONG.** OpenCode's source clearly shows xhigh IS supported for Codex 5.2+ and GPT-5.3+.

Research these files to get the correct levels per model:
- `docs/reference-code/opencode/packages/opencode/src/provider/transform.ts` — the `variants()` function, specifically the `@ai-sdk/openai` case (lines ~486-515). Key findings to look for:
  - Codex models: 5.2 and 5.3 get `[...WIDELY_SUPPORTED_EFFORTS, "xhigh"]` = low/medium/high/xhigh
  - Other codex: just `WIDELY_SUPPORTED_EFFORTS` = low/medium/high
  - GPT-5 (non-codex): may get "minimal" prepended, and "none" + "xhigh" based on `release_date`
  - GPT-5-pro: no reasoning variants at all
- Also check the `@openrouter/ai-sdk-provider` case — OpenRouter uses `OPENAI_EFFORTS` = `["none", "minimal", "low", "medium", "high", "xhigh"]` for GPT/Claude/Gemini-3

**Fix `crates/ava-llm/src/providers/openai.rs`:**
1. `thinking_levels()` should return different levels based on the model:
   - Codex 5.2+, GPT-5.3+: `[Low, Medium, High, Max]` where Max maps to "xhigh"
   - Other Codex/GPT-5: `[Low, Medium, High]`
2. `generate_with_thinking()`: Max should map to `"xhigh"` not `"high"` for models that support it
3. Add a helper like `fn max_reasoning_effort(&self) -> &str` that returns "xhigh" or "high" based on model

### 0c: Re-research Anthropic thinking API version
Phase 1 uses `anthropic-version: 2023-06-01` header. Verify this is correct for thinking mode. The adaptive thinking API may require a newer version header. Check:
- `docs/reference-code/opencode/packages/opencode/src/provider/provider.ts` — look for anthropic version or API setup
- Anthropic's current API docs require version headers for extended thinking

### 0d: Re-research OpenRouter reasoning format
Phase 1's OpenRouter implementation just delegates to `self.inner`. But OpenRouter has its OWN reasoning format:
- Check `docs/reference-code/opencode/packages/opencode/src/provider/transform.ts` — the `@openrouter/ai-sdk-provider` case in `variants()`
- OpenRouter uses `{ reasoning: { effort: "low|medium|high" } }` format, NOT the underlying provider's format
- The OpenRouter provider should add `reasoning.effort` to the request body, not delegate to inner

**Fix `crates/ava-llm/src/providers/openrouter.rs`** to use OpenRouter-specific reasoning format instead of delegating.

### 0e: Verify Gemini thinking is correct
Check `docs/reference-code/opencode/packages/opencode/src/provider/transform.ts`:
- `@ai-sdk/google` case for Gemini 2.5 vs 3.x
- OpenCode also sets `thinkingConfig.includeThoughts = true` by DEFAULT for all Google models in `options()` — should we do the same?

**After all fixes: run `cargo test --workspace` and `cargo clippy --workspace`. ALL must pass with 0 warnings in modified crates.**

**CRITICAL: Invoke the Code Reviewer sub-agent to verify ALL fixes in Task 0 are correct. Cross-reference every provider's thinking params against OpenCode's `transform.ts::variants()` function. This is the single most important review checkpoint — getting the API contracts wrong means silent failures at runtime. Fix any issues before moving on.**

## Task 0f: Competitive Research — UX patterns

Before writing UI code, study how competitors display and control thinking in their TUI/CLI:

### OpenCode
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/local.tsx` — variant cycling UX: `cycle()` wraps off→levels→off, stored per provider/model key
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/index.tsx` — how thinking level appears in session UI
- `docs/reference-code/opencode/packages/ui/src/components/session-turn.tsx` — thinking heading component, how thinking blocks are rendered in chat turns
- `docs/reference-code/opencode/packages/ui/src/components/thinking-heading.stories.tsx` — thinking heading visual design
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/component/prompt/index.tsx` — thinking level in prompt area
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/component/tips.tsx` — keybind hints for thinking

### Claude Code / pi-mono
- `docs/reference-code/pi-mono/packages/coding-agent/src/modes/interactive/components/thinking-selector.ts` — their selector UI for thinking modes

### Goose (Rust)
- `docs/reference-code/goose/crates/goose-cli/src/session/thinking.rs` — Rust thinking display in CLI session

### Key UX questions:
1. Where does the thinking level indicator appear? (status bar? prompt area? both?)
2. How is thinking content shown? (collapsed heading? expandable? dimmed? separate block?)
3. What keybind is used? (OpenCode uses a button/click cycle, we use Ctrl+T)
4. Is there a visual transition/animation when cycling?
5. How does Goose render thinking in a Rust TUI?

**Document your UX decisions before implementing. Invoke the Code Reviewer sub-agent to verify decisions align with competitor patterns and project conventions.**

## Task 1: Agent stack thinking state

File: `crates/ava-agent/src/stack.rs`

Read the current file first. Find the `AgentStack` struct.

Add:
```rust
/// Current thinking level (persisted per provider/model)
pub thinking_level: ThinkingLevel,
```

Add methods:
```rust
/// Set the thinking level for the current model
pub fn set_thinking_level(&mut self, level: ThinkingLevel) {
    self.thinking_level = level;
}

/// Cycle thinking: Off → Low → Medium → High → Max → Off
/// Returns the new level's label for status display
pub fn cycle_thinking(&mut self) -> &'static str {
    self.thinking_level = self.thinking_level.cycle();
    self.thinking_level.label()
}

/// Whether the current model supports thinking
pub fn supports_thinking(&self) -> bool {
    // Access the current provider and check
    // This will need to read the provider through the RwLock
    // Implementation depends on how the provider is stored
    // May need to be async or use block_in_place
    false // placeholder — wire to actual provider
}
```

Import `ThinkingLevel` from `ava_types`.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 2: Wire thinking into agent loop

File: `crates/ava-agent/src/loop.rs` (or wherever the agent execution loop lives)

Read the current file. Find where `generate_with_tools()` is called.

Change to use `generate_with_thinking()` when thinking is not Off:
```rust
let response = if self.thinking_level != ThinkingLevel::Off {
    provider.generate_with_thinking(messages, tools, self.thinking_level).await?
} else {
    provider.generate_with_tools(messages, tools).await?
};
```

If thinking content is returned (`response.thinking.is_some()`), emit it as an `AgentEvent`:
- Add `AgentEvent::Thinking(String)` variant if it doesn't exist
- The TUI will display this as collapsed thinking content

**CRITICAL: Invoke the Code Reviewer sub-agent now.** This is the core architectural change — the agent loop touches the hot path for every LLM call. Review: (1) thinking level is correctly threaded from AgentStack to the provider call, (2) the fallback to `generate_with_tools()` when Off is correct, (3) thinking events don't break streaming, (4) no performance regression when thinking is Off. Cross-reference with how OpenCode wires variant options into `providerOptions` in `transform.ts::options()`. Fix any issues before moving on.

## Task 3: /think slash command

File: `crates/ava-tui/src/app/commands.rs`

Read the current file. Add a new slash command after the existing commands in the match:

```rust
"/think" => {
    match arg {
        Some(level_str) => {
            if let Some(level) = ava_types::ThinkingLevel::from_str_loose(level_str) {
                self.state.agent.set_thinking_level(level);
                let label = level.label();
                self.set_status(format!("Thinking: {label}"), StatusLevel::Info);
                Some((MessageKind::System, format!("Thinking level set to {label}")))
            } else {
                Some((MessageKind::Error,
                    "Invalid level. Use: /think off|low|medium|high|max".to_string()
                ))
            }
        }
        None => {
            // Cycle to next level
            let label = self.state.agent.cycle_thinking();
            self.set_status(format!("Thinking: {label}"), StatusLevel::Info);
            Some((MessageKind::System, format!("Thinking level: {label}")))
        }
    }
}
```

Also update the `/help` text to include:
```
  /think [level]           — set thinking level (off/low/med/high/max)
```

And update the keyboard shortcuts section:
```
  Ctrl+T                   — cycle thinking level
```

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 4: Ctrl+T handler

File: `crates/ava-tui/src/app/commands.rs`

Find `execute_command_action()` and the `Action::ToggleThinking` arm (which currently does nothing or is in the `_ => {}` catch-all).

```rust
Action::ToggleThinking => {
    let label = self.state.agent.cycle_thinking();
    self.set_status(format!("Thinking: {label}"), StatusLevel::Info);
}
```

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 5: Status bar thinking indicator

File: `crates/ava-tui/src/widgets/status_bar.rs`

Read the current file. Find where status elements are rendered.

Add a thinking indicator when level is not Off. Display it near the model name:
- Format: `[think:high]` or similar compact format
- Color: use a distinct color (yellow/orange) to make it visible
- Only show when thinking != Off

The exact rendering depends on how the status bar is structured. Read the file and add the indicator in the appropriate location, pulling `self.state.agent.thinking_level` for the current level.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 6: Display thinking content in chat

File: `crates/ava-tui/src/app/mod.rs` (or wherever AgentEvent is handled)

When `AgentEvent::Thinking(text)` is received:
1. Add the thinking content as a collapsible message in the chat
2. Use `MessageKind::System` or add a new `MessageKind::Thinking` variant
3. In the message rendering, show thinking content with a distinct style (dimmed, italic, or with a "thinking..." prefix)

Keep it simple — just show the thinking text with a visual distinction. Full collapse/expand can come later.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 7: Tests

1. Test `cycle_thinking()` cycles through all levels
2. Test `/think` command parsing — valid levels, invalid input, no arg cycles
3. Test `Action::ToggleThinking` updates state
4. Run: `cargo test --workspace` — all must pass
5. Run: `cargo clippy --workspace` — **ZERO warnings** across ALL crates (not just modified ones)

## Acceptance Criteria
- [ ] Clippy warning in `model_catalog.rs` fixed (Task 0a)
- [ ] OpenAI thinking levels corrected — xhigh supported for Codex 5.2+/GPT-5.3+ (Task 0b)
- [ ] OpenRouter uses its own `reasoning.effort` format, not delegation (Task 0d)
- [ ] All provider thinking params verified against OpenCode's `transform.ts` (Task 0)
- [ ] `AgentStack` has `thinking_level` field, `set_thinking_level()`, `cycle_thinking()`
- [ ] Agent loop calls `generate_with_thinking()` when thinking enabled
- [ ] `/think [level]` command works (with and without argument)
- [ ] `Ctrl+T` cycles thinking level
- [ ] Status bar shows thinking level when not Off
- [ ] Thinking content displayed in chat
- [ ] `/help` updated with /think and Ctrl+T
- [ ] All tests pass, clippy clean (0 warnings workspace-wide)

## Final Code Review
After all changes, invoke the Code Reviewer sub-agent for a comprehensive review of ALL modifications across ALL files touched in this phase. Specifically verify:
1. **Phase 1 fixes**: OpenAI xhigh, OpenRouter reasoning format, Anthropic API version — cross-reference EVERY provider against `docs/reference-code/opencode/packages/opencode/src/provider/transform.ts::variants()`
2. **Thread safety**: thinking_level access from TUI thread vs agent loop (is RwLock/Mutex needed?)
3. **Consistency**: /think command, Ctrl+T, and status bar all read the same state
4. **Graceful degradation**: thinking Off produces identical behavior to pre-sprint code (zero regression)
5. **Event flow**: AgentEvent::Thinking properly handled in all code paths (headless mode too)
6. **Help text**: /help and keybind hints are updated and accurate
7. **Clippy**: `cargo clippy --workspace` must show 0 warnings total
8. Cross-reference key decisions against competitor implementations in `docs/reference-code/`
