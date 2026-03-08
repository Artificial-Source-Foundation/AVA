# Sprint 34: TUI Parity with OpenCode & Codex CLI

> Informed by research in `docs/development/research/tui-implementation-research.md`

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in the "Key Files to Read" section
2. Read the TUI research: `docs/development/research/tui-implementation-research.md`
3. Read `CLAUDE.md` for project conventions
4. Read the bug backlog: `docs/development/benchmarks/sprint-32-bugs.md` (TUI gaps section)
5. Enter plan mode and produce a detailed implementation plan
6. Get the plan confirmed before proceeding

## Goal

Make AVA's TUI visually competitive with OpenCode. The TUI already has the foundation (Ratatui, streaming, tool approval modal, command palette skeleton, session list). This sprint fills the UX gaps.

## Key Files to Read

```
crates/ava-tui/src/app.rs                    # Main app state + event loop
crates/ava-tui/src/ui/mod.rs                 # Main render function
crates/ava-tui/src/ui/layout.rs              # Layout splitting
crates/ava-tui/src/ui/status_bar.rs          # Current status bar
crates/ava-tui/src/ui/sidebar.rs             # Sidebar
crates/ava-tui/src/widgets/command_palette.rs # Command palette (may be skeleton)
crates/ava-tui/src/widgets/composer.rs        # Input area
crates/ava-tui/src/widgets/message_list.rs    # Chat messages
crates/ava-tui/src/widgets/message.rs         # Single message rendering
crates/ava-tui/src/widgets/tool_approval.rs   # Tool approval modal
crates/ava-tui/src/widgets/session_list.rs    # Session list
crates/ava-tui/src/widgets/streaming_text.rs  # Streaming display
crates/ava-tui/src/widgets/token_buffer.rs    # Token buffer (60fps)
crates/ava-tui/src/widgets/dialog.rs          # Dialog/modal widget
crates/ava-tui/src/state/agent.rs             # Agent state (activity, model info)
crates/ava-tui/src/state/messages.rs          # Message state
crates/ava-tui/src/state/session.rs           # Session state
crates/ava-tui/src/state/theme.rs             # Theme
crates/ava-tui/src/config/cli.rs              # CLI args
crates/ava-tui/src/rendering/markdown.rs      # Markdown rendering
```

## What Already Exists (Don't Rebuild)

- Basic TUI with message list, composer, status bar
- Tool approval modal (`ModalType::ToolApproval`)
- Command palette skeleton (`CommandPaletteState`)
- Session list (`SessionListState`)
- Token buffer (60fps streaming)
- Markdown rendering with syntax highlighting
- Theme struct
- Keybind system (`KeybindState`, `Action`)

## Stories

### Story 1: Welcome Screen

When the message list is empty, show a welcome screen instead of blank space.

**Pattern from OpenCode:**
```
    ╔═══════════════════╗
    ║   AVA             ║
    ║   AI Coding Agent ║
    ╚═══════════════════╝

    Model: anthropic/claude-sonnet-4
    Provider: openrouter
    CWD: /home/user/project

    Type a message to start, or press Ctrl+P for commands.
```

**Implementation:**
- In the message list render function, check if messages is empty
- If empty, render the welcome view centered in the area
- Show: ASCII logo/name, model + provider info, current directory, hint text
- Use the theme colors for styling

**Acceptance criteria:**
- Welcome screen shows when no messages
- Displays model, provider, CWD
- Shows keyboard hint
- Disappears once first message is sent
- Looks polished (centered, styled)

### Story 2: Rich Status Bar

Upgrade the status bar from basic (turn count + activity) to rich (matching OpenCode).

**Target layout (left to right):**
```
[Ctrl+P cmds] [model badge] [tokens: 1.2K/200K] [cost: $0.03] [activity] [session: #3]
```

**Data to show:**
1. **Help hint** — `Ctrl+P` (muted)
2. **Model badge** — e.g., `claude-sonnet-4` with provider prefix
3. **Token count** — current / max with K/M suffix (e.g., `12.5K/200K`)
4. **Cost** — running cost in USD (e.g., `$0.03`)
5. **Activity** — Idle / Thinking / Executing: tool_name
6. **Session** — session ID or name

**Implementation:**
- Read token count from agent state (may need to pipe from agent loop)
- Read cost from agent state (may need to pipe from agent loop)
- Format with K/M suffix helper
- Use `Spans` with different styles for each section
- Separate sections with ` │ ` divider

**Acceptance criteria:**
- Status bar shows all 6 fields
- Token count updates during streaming
- Activity changes between Idle/Thinking/Executing
- Model badge is readable
- Looks clean, not cluttered

### Story 3: Context-Sensitive Keyboard Hints

Show relevant keyboard shortcuts below the input area, changing based on context.

**Hint lines by mode:**
- **Normal (idle):** `Enter send │ Ctrl+P commands │ Ctrl+M model │ Ctrl+Q quit`
- **Streaming:** `Ctrl+C cancel │ Ctrl+Q quit`
- **Tool approval:** `y approve │ n reject │ a always │ Ctrl+Q quit`
- **Command palette open:** `↑↓ navigate │ Enter select │ Esc close`

**Implementation:**
- Add a `render_hints()` function
- Determine current mode from app state (idle, streaming, modal open)
- Render as a single line below the composer
- Use muted/dim style so it doesn't compete with main content

**Acceptance criteria:**
- Hints change based on current mode
- At least 4 modes covered (normal, streaming, approval, palette)
- Styled with muted colors
- Doesn't take too much vertical space (1 line)

### Story 4: Command Palette (Full Implementation)

Flesh out the command palette with real commands and fuzzy search.

**Commands to register:**
| Command | Key | Action |
|---------|-----|--------|
| `/model` | Ctrl+M | Open model selector |
| `/session` | Ctrl+S | Open session list |
| `/clear` | — | Clear message history |
| `/help` | Ctrl+? | Show help |
| `/quit` | Ctrl+Q | Quit |
| `/yolo` | — | Toggle auto-approve tools |
| `/new` | — | New session |
| `/compact` | — | Force context compaction |

**Trigger:** `Ctrl+P` or typing `/` in empty composer

**Implementation:**
- `CommandPaletteState` should hold a `Vec<Command>` with name, description, keybind, action
- Fuzzy filter as user types (use `nucleo` crate if already in deps, otherwise simple `contains`)
- Render as centered overlay dialog (use existing `dialog.rs`)
- `Enter` executes selected command
- `Esc` closes

**Acceptance criteria:**
- Ctrl+P opens palette
- At least 8 commands registered
- Fuzzy search filters commands
- Enter executes, Esc closes
- `/` prefix in empty composer also opens palette

### Story 5: Model Selector

Quick model switching without restarting.

**Trigger:** `Ctrl+M` or `/model` command

**Implementation:**
- Show overlay dialog listing available models
- For OpenRouter: list common models (claude-sonnet, gpt-4o, etc.)
- Selecting a model calls `AgentState::switch_model()`
- Update status bar badge immediately
- Close dialog on selection

**Model list (hardcoded for now, later from config):**
```
anthropic/claude-sonnet-4
anthropic/claude-haiku-4-5
openai/gpt-4o
openai/gpt-4o-mini
moonshotai/kimi-k2.5
```

**Acceptance criteria:**
- Ctrl+M opens model selector
- Shows list of models
- Arrow keys navigate, Enter selects
- Model switches at runtime (next API call uses new model)
- Status bar updates immediately

### Story 6: `!` Shell Command Prefix

Typing `!` followed by a command in the composer should execute it directly via bash, without sending to the LLM.

**Example:** `!git status` → runs `git status` and shows output inline

**Implementation:**
- In the input handler, check if message starts with `!`
- If so, strip the `!`, execute via `tokio::process::Command`
- Display output as a system message in the chat
- Don't send to the agent

**Acceptance criteria:**
- `!command` executes shell command
- Output displayed in chat as system message
- Errors displayed with red styling
- Normal messages (without `!`) still go to agent

### Story 7: TTL Status Messages

Add a notification system for transient messages (info, warnings, errors) that auto-clear.

**Implementation:**
```rust
pub struct StatusMessage {
    pub text: String,
    pub level: StatusLevel,  // Info, Warn, Error
    pub expires_at: Instant,
}

pub enum StatusLevel {
    Info,    // dim/white
    Warn,   // yellow
    Error,  // red
}
```

- Add `status_message: Option<StatusMessage>` to `AppState`
- In the tick handler, clear expired messages
- Render in the status bar area (right side)
- Default TTL: 3 seconds for info, 5 seconds for errors

**Use cases:**
- "Model switched to claude-sonnet-4" (info, 3s)
- "Session saved" (info, 3s)
- "Context compacted" (info, 3s)
- "Provider error: rate limited" (error, 5s)

**Acceptance criteria:**
- Status messages appear and auto-clear
- Color-coded by level
- Don't block interaction
- At least 3 places in the app emit status messages

## Implementation Order

1. Story 1 (welcome screen) — quick win, visible impact
2. Story 2 (rich status bar) — foundation for other features
3. Story 3 (keyboard hints) — improves discoverability
4. Story 7 (TTL status messages) — needed by stories 4-5
5. Story 4 (command palette) — core UX feature
6. Story 5 (model selector) — key workflow
7. Story 6 (`!` shell prefix) — nice-to-have

## Constraints

- **Rust only**
- Build on existing code — don't rewrite working widgets
- Use existing theme system for colors
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Keep the TUI responsive — no blocking operations in render
- Test on 80x24 terminal minimum (handle small terminals gracefully)

## Validation

```bash
cargo test --workspace
cargo clippy --workspace

# Visual testing
cargo run --bin ava -- --provider openrouter --model anthropic/claude-sonnet-4
# Check: welcome screen visible on boot
# Check: status bar shows model + provider
# Check: Ctrl+P opens command palette
# Check: Ctrl+M opens model selector
# Check: keyboard hints visible below input
# Check: !echo hello runs shell command
```
