# Sprint 60-02: Conversation Context, Sessions UI, Last Model, Scroll Fix

## Context

You are working on **AVA**, a Rust-first AI coding agent. Read `CLAUDE.md` and `AGENTS.md` first.

There are **4 critical UX bugs/features** in the TUI:

### Bug 1: Model has no memory of previous messages (CRITICAL)

When a user sends "hello", gets a response, then asks "what was my previous message", the model says it has no memory. The TUI *displays* chat history correctly, but the **agent never receives it**.

**Root cause**: Each call to `agent.start(goal)` creates a fresh `AgentLoop` with a fresh `ContextManager` (empty messages) and fresh `Session`. The previous conversation is lost.

**Code path**:
- `crates/ava-tui/src/app/event_handler.rs` line ~187: `submit_goal()` calls `agent.start()` with only the new goal
- `crates/ava-tui/src/state/agent.rs` line ~160: `AgentState.start()` calls `stack.run(&goal)`
- `crates/ava-agent/src/stack.rs` line ~457: Creates fresh `ContextManager::new_with_condenser()` with empty messages
- `crates/ava-agent/src/agent_loop/mod.rs` line ~201: Creates `Session::new()` with empty messages
- `crates/ava-context/src/manager.rs` line ~35: `ContextManager` initialized with `messages: Vec::new()`

The LLM only receives: `[system_prompt, current_user_message]` — no history.

### Bug 2: Scrolling shows previous input instead of scrolling chat

Scrolling (mouse wheel / trackpad scroll) in the chat area shows the previous typed message (input history recall) instead of scrolling up through the message list. The user expects to scroll through the conversation history but instead sees their old typed inputs cycling. There's no way to scroll up through the conversation.

### Feature 3: Session sidebar / session picker

Like OpenCode's session list: show past conversations on startup, let user continue a previous session or start new. The `ava-session` crate already has `SessionManager` with SQLite + FTS5 persistence, `session_list`, `session_search`, `session_load` tools. Just needs TUI UI.

### Feature 4: Remember last model on startup

When the user switches models (e.g., to `kimi-k2.5` on provider `alibaba`), that choice should persist. On next AVA startup, restore the last used `provider/model`.

---

## Phase 1: Fix Conversation Context (CRITICAL)

**Files to modify:**
- `crates/ava-agent/src/stack.rs`
- `crates/ava-agent/src/agent_loop/mod.rs`
- `crates/ava-tui/src/state/agent.rs`
- `crates/ava-context/src/manager.rs`

### 1a: Add conversation history to AgentStack.run()

The `AgentStack` needs to accept previous messages so the `ContextManager` starts with history instead of empty.

Option A (simpler): Add a `run_with_history()` method that takes `&[Message]`:
```rust
pub fn run_with_history(
    &self,
    goal: &str,
    history: Vec<Message>,
    event_tx: Option<mpsc::UnboundedSender<AgentEvent>>,
) -> impl Stream<Item = AgentEvent> + '_ {
    // ... same as run() but pass history to ContextManager
}
```

Option B (modify existing): Add `history: Vec<Message>` param to `run()` (less breaking — pass empty vec when no history).

### 1b: ContextManager accepts initial messages

In `crates/ava-context/src/manager.rs`, add a constructor or method:
```rust
pub fn with_messages(messages: Vec<Message>, condenser: Option<Box<dyn Condenser>>) -> Self {
    Self { messages, condenser, ..Default::default() }
}
```

### 1c: TUI passes conversation history

In `crates/ava-tui/src/state/agent.rs`, when `start()` is called:
1. Collect all previous messages from the current session (the UI message list has them)
2. Convert `UiMessage` entries to `ava_types::Message` (user messages → Role::User, assistant messages → Role::Assistant)
3. Pass these to `stack.run_with_history(goal, history, ...)`

### 1d: Don't duplicate the current goal

The agent loop's `run_streaming()` adds the goal as a User message (line ~206). Make sure the history passed in doesn't include the current goal, or skip adding it if it's already the last message.

### 1e: Context condensation

When conversation history grows large, the `ContextManager`'s condenser should kick in (it already exists — `HybridCondenser` from Sprint 26). Verify this works with pre-loaded history. The context window limit comes from the model registry.

*Before proceeding to Phase 2, invoke the Code Reviewer sub-agent to verify conversation context flows correctly end-to-end: TUI → AgentStack → AgentLoop → ContextManager → LLM provider.*

---

## Phase 2: Fix Chat Scrolling

**Files to modify:**
- `crates/ava-tui/src/app/event_handler.rs` — key handling
- `crates/ava-tui/src/widgets/message_list.rs` — scroll state
- `crates/ava-tui/src/state/messages.rs` — scroll offset

### 2a: Investigate current scroll behavior

Read the event handler to understand:
- What does mouse scroll / trackpad scroll do currently? (seems to trigger input history recall instead of scrolling messages)
- How are mouse scroll events handled? Are `MouseEvent::ScrollUp`/`ScrollDown` mapped?
- Is there a separate keybind for scrolling the message area?
- How does `scroll_offset` in `MessageState` get modified?

### 2b: Implement proper scrolling

**Mouse scroll / trackpad scroll** must scroll the message list, NOT cycle through input history. Fix the event handler so:
- **Mouse scroll up/down**: Scroll the message list area
- **Page Up / Page Down**: Scroll message area by page
- **Ctrl+Up / Ctrl+Down**: Scroll message area (keyboard alternative)
- Keep **Up/Down arrow in composer** for input history (that's expected behavior)

The key fix: mouse/trackpad scroll events should be routed to the message list widget's `scroll_offset`, not to the composer's input history.

Check how OpenCode handles this for reference — look at `docs/reference-code/` if available.

### 2c: Auto-scroll to bottom on new messages

When a new message arrives (assistant response, tool output), auto-scroll to the bottom. But if the user has manually scrolled up, stay at their position until they scroll back down.

*Before proceeding to Phase 3, invoke the Code Reviewer sub-agent to verify scrolling works correctly.*

---

## Phase 3: Session Sidebar / Picker

**Files to modify/create:**
- `crates/ava-tui/src/widgets/session_list.rs` — **NEW** session picker widget
- `crates/ava-tui/src/app/mod.rs` — integrate session selector
- `crates/ava-tui/src/state/agent.rs` — session loading

### 3a: Session list widget

Create a session picker using the existing `SelectListState<T>` widget (from Sprint 58). Display:
- Session title (first user message, truncated)
- Date/time
- Message count
- Model used

Load sessions from `SessionManager` (ava-session crate).

### 3b: Activation

- Show session list on startup (before first message)
- `/sessions` slash command to open it anytime
- `Ctrl+S` keybind to toggle
- "New Session" option at the top

### 3c: Session loading

When user selects a session:
1. Load messages from `SessionManager`
2. Populate `MessageState` (UI messages)
3. Set the conversation history for the next agent run (Phase 1's mechanism)
4. Restore the model/provider from the session metadata

### 3d: Session auto-save

After each agent turn completes, save the session automatically via `SessionManager`. The session should include:
- All messages (user, assistant, tool calls, tool results)
- Model and provider used
- Timestamps
- Session title (derived from first user message)

*Before proceeding to Phase 4, invoke the Code Reviewer sub-agent.*

---

## Phase 4: Remember Last Model

**Files to modify:**
- `crates/ava-config/src/lib.rs` — add `last_provider` and `last_model` to config
- `crates/ava-tui/src/state/agent.rs` — save on model switch, restore on startup
- `crates/ava-tui/src/widgets/model_selector.rs` — trigger save on selection

### 4a: Add to config

In `~/.ava/config.yaml`, add:
```yaml
last_provider: "anthropic"
last_model: "claude-sonnet-4-6"
```

Add fields to the `Config` struct in `ava-config`.

### 4b: Save on model switch

When the user switches model via:
- Model selector (Ctrl+M)
- `/model` command
- CLI `--provider`/`--model` flags

Persist to config.

### 4c: Restore on startup

When TUI starts, if no `--provider`/`--model` CLI flags are given, read `last_provider`/`last_model` from config and use them as defaults instead of hardcoded fallbacks.

*Before proceeding to Phase 5, invoke the Code Reviewer sub-agent.*

---

## Phase 5: Final Verification

```bash
cargo build --workspace 2>&1
cargo test --workspace 2>&1
cargo clippy --workspace 2>&1
```

### Manual verification checklist:
- [ ] Send "hello", get response, ask "what was my previous message" → model remembers
- [ ] Long conversations maintain full history (or condensed history within context window)
- [ ] Scroll up through chat messages works
- [ ] Auto-scroll to bottom on new messages
- [ ] Session list shows on startup
- [ ] Can continue a previous session
- [ ] New session starts fresh
- [ ] Model/provider persists across restarts
- [ ] Sessions auto-save

*Invoke the Code Reviewer sub-agent for a FINAL review of ALL changes.*

## Acceptance Criteria

1. `cargo test --workspace` passes
2. `cargo clippy --workspace` clean
3. Conversation history sent to LLM on each turn (model remembers previous messages)
4. Chat area scrollable (separate from input history)
5. Session sidebar shows past conversations
6. Last used model/provider restored on startup
7. Sessions auto-save after each turn
