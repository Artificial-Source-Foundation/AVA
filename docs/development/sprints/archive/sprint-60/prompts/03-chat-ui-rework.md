# Sprint 60-03: Chat UI Rework — OpenCode-Style Layout

## Context

You are working on **AVA**, a Rust-first AI coding agent with a Ratatui TUI. The current chat layout uses a bare `> ` prompt at the bottom with a thin separator line above it, and messages are rendered as simple text with no visual containment. This sprint reworks the chat layout to match the polished UX of [OpenCode](https://github.com/opencode-ai/opencode), which uses a **bordered input box at the bottom**, a **scrollable message area** above it, messages styled with **thick left-border "bubble" indicators** distinguishing user vs assistant, and a **status bar at the very bottom** with model/token/diagnostics info.

### Key conventions
- Read `CLAUDE.md` and `AGENTS.md` at the project root before starting
- All new code is Rust — no TypeScript
- Run `cargo clippy --workspace` and `cargo test --workspace` after each phase
- TUI framework: Ratatui + Crossterm (NOT Bubbletea/lipgloss — those are Go)
- The Ratatui equivalents of OpenCode's patterns: `Block` with `Borders` for containers, `Layout` with `Constraint` for split panes, `Paragraph` for text, `Wrap` for line wrapping

### OpenCode's layout architecture (research summary)

OpenCode uses a **SplitPaneLayout** with three panels:

```
+------------------------------------------------------+
| Messages (left panel, 90% vertical)                  |
|   - Viewport-based scrolling                         |
|   - Welcome screen shown when messages are empty     |
|   - Messages use thick left-border styling:          |
|     - User messages: secondary color left border     |
|     - Assistant messages: primary color left border   |
|   - Tool calls: muted left border, indented          |
|   - "Working..." spinner + status below messages     |
|   - Keyboard hint line at very bottom of area        |
+------------------------------------------------------+
| Editor (bottom panel, 10% vertical)                  |
|   - Top border only (visual separator)               |
|   - ">" prompt prefix with primary color             |
|   - Multiline textarea, no line numbers              |
|   - Enter to send, backslash+enter for newline       |
+------------------------------------------------------+
| Status bar (outside split pane, always bottom)       |
|   - Help widget, token count, cost, model name       |
+------------------------------------------------------+
```

Key details from OpenCode source:
- **Editor container**: `layout.NewContainer(editor, layout.WithBorder(true, false, false, false))` — only top border, creating clean separation from messages
- **Messages container**: `layout.NewContainer(messages, layout.WithPadding(1, 1, 0, 1))` — padded, no border
- **Split ratio**: 90% messages / 10% editor (vertical), with left/right panels at 70/30 when sidebar is shown
- **Message styling**: `renderMessage()` uses `lipgloss.ThickBorder()` on the left side only, with `BorderForeground` set to `Primary` for assistant and `Secondary` for user
- **Welcome screen**: Rendered INSIDE the message area when `len(messages) == 0` — shows logo, repo URL, cwd, LSP config
- **Status bar**: Full-width bar at the very bottom with colored segments: help widget, token/cost info, diagnostics, model name
- **Sidebar**: Only appears AFTER a session is created (shows session info, modified files with +/- counts)

### AVA's current layout

**Files to modify:**

- `crates/ava-tui/src/ui/layout.rs` — `MainLayout` struct and `build_layout()` function
- `crates/ava-tui/src/ui/mod.rs` — main `render()` function that orchestrates all widgets
- `crates/ava-tui/src/widgets/composer.rs` — `render_composer()` and `render_separator()`
- `crates/ava-tui/src/widgets/message_list.rs` — `render_message_list()` and scroll logic
- `crates/ava-tui/src/widgets/message.rs` — `render_message()` delegation
- `crates/ava-tui/src/state/messages.rs` — `UiMessage::to_lines()` — where user/assistant styling is applied
- `crates/ava-tui/src/widgets/welcome.rs` — `render_welcome()` — ASCII art welcome screen
- `crates/ava-tui/src/ui/status_bar.rs` — `render_top()` and `render_context_bar()`

**Current layout structure** (from `layout.rs`):

```
top_bar       [1 row]     — status bar at top
messages      [flexible]  — message list
separator     [1 row]     — thin "───" line
composer      [1-8 rows]  — bare "> " prompt
context_bar   [1 row]     — activity/status at bottom
```

**Current composer** (from `composer.rs`): Renders a bare `❯ ` prefix followed by text buffer and a block cursor character. No border, no container, no placeholder text.

**Current messages** (from `state/messages.rs`): User messages show `"You: "` prefix with bold muted text. Assistant messages render markdown. Tool use shows tool name + collapsible content. No left-border styling, no visual containment.

---

## Phase 1: Bordered Input Box

Rework the composer from a bare `❯ ` prompt to a bordered input container, matching OpenCode's editor component.

### Task 1a: Update layout structure

In `crates/ava-tui/src/ui/layout.rs`:

1. Remove the `separator` row from `MainLayout` — the input box border replaces it
2. The composer area should include space for a top border (1 row) + content rows:
   - Change composer constraint from `Constraint::Length(composer_h)` to `Constraint::Length(composer_h + 2)` to account for top+bottom border
   - OR use `Constraint::Length(composer_h + 1)` if only using a top border (preferred — matches OpenCode)
3. Update `MainLayout` struct: remove `separator: Rect` field

Updated layout:
```
top_bar       [1 row]         — status/model info
messages      [flexible]      — message list
composer      [composer_h+1]  — bordered input box (top border acts as separator)
context_bar   [1 row]         — activity/hints at bottom
```

### Task 1b: Render bordered composer

In `crates/ava-tui/src/widgets/composer.rs`:

1. Remove `render_separator()` — no longer needed
2. Wrap the composer content in a `Block` with only a top border:
   ```rust
   let block = Block::default()
       .borders(Borders::TOP)
       .border_style(Style::default().fg(state.theme.border))
       .style(Style::default().bg(state.theme.bg));
   let inner = block.inner(area);
   frame.render_widget(block, area);
   // Render prompt content in `inner`
   ```
3. Add placeholder text when the buffer is empty and not recording:
   ```rust
   // When buffer is empty, show placeholder
   Span::styled(
       "Type a message... (Enter to send, \\ + Enter for newline)",
       Style::default().fg(state.theme.text_dimmed),
   )
   ```
4. Keep the `❯ ` prompt prefix but style it with `theme.primary` color (currently uses `theme.text_muted`)
5. Add a mode badge when YOLO/permissive mode is active — render a small `[YOLO]` tag in the top-right of the border area using the border row

### Task 1c: Update render calls

In `crates/ava-tui/src/ui/mod.rs`:

1. Remove the `render_separator(frame, split.separator, state)` call
2. Remove `use crate::widgets::composer::render_separator` import
3. The composer now handles its own visual separation via its top border

**Before proceeding to Phase 2, invoke the Code Reviewer sub-agent to verify: (1) the layout math is correct — total rows still sum to terminal height, (2) the bordered composer renders correctly at various terminal widths, (3) `render_separator` is fully removed with no dead code.**

---

## Phase 2: Message Styling with Left-Border Indicators

Restyle messages to use colored left-border indicators, matching OpenCode's `renderMessage()` pattern.

### Task 2a: Add left-border message rendering

In `crates/ava-tui/src/state/messages.rs`, update `UiMessage::to_lines()`:

1. For **user messages**: Add a thick left-border indicator using the `▎` character (U+258E) or `┃` (U+2503) in `theme.secondary` color as the first character of each line. Remove the "You: " prefix — the border color now identifies the sender.
   ```rust
   // Each line of a user message gets a colored left bar
   let bar = Span::styled("▎", Style::default().fg(theme.secondary));
   let content = Span::styled(&self.content, Style::default().fg(theme.text));
   vec![Line::from(vec![bar, Span::raw(" "), content])]
   ```

2. For **assistant messages**: Use the same left-border pattern but with `theme.primary` color. The markdown content lines each get the bar prepended.
   ```rust
   let bar = Span::styled("▎", Style::default().fg(theme.primary));
   // Prepend bar to each line returned by markdown_to_lines()
   ```

3. For **tool use messages**: Use `theme.text_muted` for the left bar, keeping the existing collapsible/summary behavior.

4. Add a **blank line between messages** of different roles for visual breathing room.

### Task 2b: Add message metadata line

After the content of assistant messages, add a metadata line showing:
- Model name (from `UiMessage` — may need to add a field)
- Response time if available
- Style: muted text, indented to align with content (past the left bar)

Example: `▎ claude-sonnet-4 (2.3s)`

### Task 2c: Style the working/thinking indicator

When the agent is actively generating, the current spinner shows in the message area. Ensure it also gets the primary-colored left bar to maintain visual consistency:
```rust
let bar = Span::styled("▎", Style::default().fg(theme.primary));
let spinner = Span::styled(spinner_frame(tick), Style::default().fg(theme.accent));
let label = Span::styled(" Thinking...", Style::default().fg(theme.text_muted));
Line::from(vec![bar, Span::raw(" "), spinner, label])
```

**Before proceeding to Phase 3, invoke the Code Reviewer sub-agent to verify: (1) left bars are consistently applied to ALL message types including tool calls, errors, and system messages, (2) markdown rendering still works correctly with the prepended bar characters, (3) line wrapping accounts for the extra 2 characters (bar + space) in width calculations.**

---

## Phase 3: Welcome Screen in Message Area

Ensure the welcome screen renders inside the message area (not as a separate mode) and matches the information density of OpenCode's initial screen.

### Task 3a: Refine welcome content

In `crates/ava-tui/src/widgets/welcome.rs`:

1. Keep the ASCII art logo (it is already good)
2. Add working directory display: `cwd: /path/to/project`
3. Add keyboard shortcuts section:
   ```
   Ctrl+K  Command palette     Ctrl+M  Switch model
   Ctrl+S  Switch session      Ctrl+V  Voice input
   Ctrl+N  New session         Ctrl+?  Help
   ```
4. Style shortcuts with `theme.text` for keys and `theme.text_muted` for descriptions
5. Center the entire welcome screen vertically and horizontally in the message area (the current implementation already does vertical centering via layout constraints — verify horizontal centering uses `Alignment::Center`)

### Task 3b: Ensure welcome respects message area bounds

The welcome screen is already rendered via `render_message_list` when `messages.is_empty()`. Verify that:
1. It renders within `split.messages` rect (not the full terminal)
2. The bordered composer below it is still visible and interactive
3. The welcome screen disappears as soon as the first message appears (current behavior — just verify)

**Before proceeding to Phase 4, invoke the Code Reviewer sub-agent to verify: (1) welcome screen keyboard shortcuts are accurate and match the actual keybindings in `state/keybinds.rs`, (2) welcome screen degrades gracefully on small terminals (< 80 columns, < 24 rows).**

---

## Phase 4: Status Bar Consolidation

Consolidate the top bar and context bar into a more informative layout matching OpenCode's single bottom status bar.

### Task 4a: Enhance the context bar (bottom)

In `crates/ava-tui/src/ui/status_bar.rs`, update `render_context_bar()`:

1. Add a **model name badge** on the right side with `theme.secondary` background:
   ```
   [...activity/status info...                    claude-sonnet-4]
   ```
2. Add **token count and cost** next to the model badge when a session is active:
   ```
   [...activity info...        Context: 12.5K, Cost: $0.03  claude-sonnet-4]
   ```
3. Add **keyboard hint** on the far left when the agent is NOT busy:
   ```
   [Ctrl+? help          ...status...        12.5K $0.03  claude-sonnet-4]
   ```
4. When the agent IS busy, replace the keyboard hint with the activity indicator (spinner + "Thinking..." / "Running tool...") — this is the current behavior, keep it

### Task 4b: Simplify the top bar

The top bar currently shows session/model info. Since the context bar now shows the model, simplify the top bar to show:
- Left: `AVA` branding + version
- Center: Session title (if any)
- Right: Permission mode badge (`YOLO` / `standard` / `strict`)

**Before proceeding to Phase 5, invoke the Code Reviewer sub-agent to verify: (1) status bar segments don't overflow on narrow terminals (< 80 columns), (2) token/cost formatting matches the existing `format_tokens()` function, (3) all status bar elements use theme colors consistently.**

---

## Phase 5: Visual Polish & Edge Cases

### Task 5a: Scrollbar indicator

Add a visual scroll position indicator to the message area:
1. When content exceeds the visible area, show a scroll position marker on the right edge
2. Use `▓` / `░` characters or Ratatui's `Scrollbar` widget
3. Only show when not at the bottom (when `auto_scroll` is false)

### Task 5b: Input focus indicator

Make the composer border color change based on focus state:
- When the input is focused (default state): `theme.primary` border color
- When a modal is open (input is not focused): `theme.border` (muted) border color
- This gives visual feedback about where keyboard input goes

### Task 5c: Multiline input improvements

Update `composer_height()` in `layout.rs` to account for the new border:
1. The height calculation should count actual newlines in the buffer (for explicit `\n` characters from backslash+enter)
2. Plus wrapped lines from long single lines
3. Minimum height: 1 line of content + 1 for top border = 2 total
4. Maximum height: 8 lines of content + 1 for top border = 9 total, but cap at 33% of terminal height

### Task 5d: Verify terminal resize

1. Test that the layout recalculates correctly on `Resize` events
2. The bordered composer should maintain its border at all reasonable terminal sizes (minimum 60x16)
3. Message left-bars should be visible even on very narrow terminals

**Before finalizing, invoke the Code Reviewer sub-agent to verify: (1) no panics on terminal sizes below 60x16, (2) scroll position indicator doesn't interfere with message content, (3) the overall visual appearance is cohesive with consistent use of theme colors.**

---

## Acceptance Criteria

1. **Bordered input box**: The composer has a visible top border separating it from messages. The `❯` prompt is styled with the primary color. Placeholder text appears when empty.
2. **Left-border message styling**: User messages have a `theme.secondary` colored left bar. Assistant messages have a `theme.primary` colored left bar. Tool messages have a `theme.text_muted` left bar.
3. **Welcome screen**: Renders inside the message area with ASCII logo, cwd, and keyboard shortcuts. Visible alongside the bordered composer below.
4. **Status bar**: Bottom context bar shows keyboard hints, token count, cost, and model name. Top bar shows AVA branding and permission mode.
5. **Scroll indicator**: Visible when message content overflows the viewport.
6. **Focus indicator**: Composer border color reflects input focus state.
7. **Multiline input**: Composer height adjusts for multiline content, capped at 33% terminal height.
8. **No regressions**: `cargo clippy --workspace` and `cargo test --workspace` pass. Existing keybindings and modals work unchanged. Voice input mode still displays correctly in the composer.

## Files Changed (Expected)

| File | Change |
|------|--------|
| `crates/ava-tui/src/ui/layout.rs` | Remove separator, adjust composer height for border |
| `crates/ava-tui/src/ui/mod.rs` | Remove separator render call, update imports |
| `crates/ava-tui/src/widgets/composer.rs` | Add Block border, placeholder text, focus indicator, remove `render_separator` |
| `crates/ava-tui/src/widgets/message_list.rs` | Add scroll indicator widget |
| `crates/ava-tui/src/widgets/message.rs` | May add metadata rendering |
| `crates/ava-tui/src/state/messages.rs` | Add left-border bars to `to_lines()`, add inter-message spacing |
| `crates/ava-tui/src/widgets/welcome.rs` | Add cwd display, keyboard shortcuts section |
| `crates/ava-tui/src/ui/status_bar.rs` | Add model badge, token/cost, keyboard hints to context bar; simplify top bar |
