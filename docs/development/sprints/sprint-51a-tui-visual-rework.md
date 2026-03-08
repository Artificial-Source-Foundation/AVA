# Sprint 51a — TUI Visual Rework (colors, layout, rendering, streaming)

> Complete visual redesign. Research Codex CLI + OpenCode source code, then rebuild every visual layer.

**Parallel with Sprint 51b** (slash commands, model browser, provider auth). Zero file overlap — this sprint owns visual/rendering files, 51b owns command/modal/feature files.

## Files this sprint OWNS (do NOT modify files owned by 51b)

**Modify:**
- `crates/ava-tui/src/state/theme.rs` — color system
- `crates/ava-tui/src/ui/mod.rs` — main layout rendering
- `crates/ava-tui/src/ui/layout.rs` — layout structure (if exists)
- `crates/ava-tui/src/ui/status_bar.rs` — top/bottom bars
- `crates/ava-tui/src/ui/sidebar.rs` — sidebar
- `crates/ava-tui/src/widgets/message_list.rs` — message container
- `crates/ava-tui/src/widgets/composer.rs` — input area
- `crates/ava-tui/src/widgets/welcome.rs` — welcome screen
- `crates/ava-tui/src/widgets/tool_approval.rs` — approval UI styling
- `crates/ava-tui/src/state/messages.rs` — message type rendering (prefixes, colors)
- `crates/ava-tui/src/rendering/markdown.rs` — markdown rendering
- `crates/ava-tui/src/rendering/diff.rs` — diff rendering
- `crates/ava-tui/src/rendering/syntax.rs` — syntax highlighting (if exists)
- `crates/ava-tui/src/event.rs` — event loop (for animation ticks)

**Do NOT modify (owned by 51b):**
- `crates/ava-tui/src/app/commands.rs`
- `crates/ava-tui/src/app/mod.rs` (key handling for `/`)
- `crates/ava-tui/src/app/modals.rs`
- `crates/ava-tui/src/widgets/command_palette.rs`
- `crates/ava-tui/src/widgets/model_selector.rs`
- `crates/ava-tui/src/state/input.rs`
- `crates/ava-tui/src/state/keybinds.rs`
- `crates/ava-config/src/credential_commands.rs`
- `crates/ava-config/src/credentials.rs`

**May create new files in:**
- `crates/ava-tui/src/rendering/`
- `crates/ava-tui/src/state/`

## Phase 1: Deep Research (mandatory — do this BEFORE any code)

### Step 1 — Deep-dive Codex CLI TUI (PRIORITY — same Ratatui stack)

Read the ENTIRE Codex CLI TUI source in `docs/reference-code/codex-cli/codex-rs/tui/src/`:

1. **Color system** — `color.rs`, `terminal_palette.rs`, `style.rs`:
   - How they detect terminal color capabilities (TrueColor vs 256 vs 16)
   - How they adapt dark/light themes dynamically (luminance threshold Y > 128.0)
   - How they blend background colors (alpha blending: 12% white overlay on dark for user messages)
   - The exact RGB values they use for backgrounds, text, diffs

2. **Layout architecture** — `app.rs`, `bottom_pane/mod.rs`:
   - How they split the screen (upper chat viewport vs lower bottom pane)
   - How the composer sits relative to content
   - How modals/popups overlay
   - Adaptive layouts (side-by-side vs stacked based on terminal width)

3. **Message rendering** — `history_cell.rs`, `exec_cell/render.rs`:
   - User vs assistant message styling (background tints, no emoji prefixes)
   - Tool call display: `$` prefix, `└` output dimmed, max 5 lines, duration + exit code
   - The `Renderable` trait for composable rendering
   - Width-aware line wrapping

4. **Markdown rendering** — `markdown_render.rs`:
   - Heading hierarchy: h1=bold+underline, h2=bold, h3=bold+italic, h4-h6=italic
   - Code blocks with syntect highlighting
   - List formatting, blockquote styling (green), link display
   - Indent context stack for nested content

5. **Diff rendering** — `diff_render.rs`:
   - Three color depth tiers with different palettes per tier
   - Background colors for added/removed lines (dark green/red tints)
   - Syntax highlighting within diff hunks
   - Gutter (line number) styling
   - Color promotion for Windows Terminal

6. **Streaming** — `streaming/mod.rs`, `streaming/chunking.rs`, `streaming/controller.rs`:
   - Two-mode adaptive engine (Smooth ~120fps vs CatchUp batch)
   - Hysteresis-based mode switching
   - Line queue (`VecDeque`) with drain policies (step, drain_n, drain_all)
   - How the "typing" animation effect works (drain one line per tick)

7. **Status display** — `status_indicator_widget.rs`, `status/card.rs`:
   - Animated spinner with elapsed time
   - Interrupt hint ("Ctrl+C to interrupt")
   - Token usage, rate limits, model info display
   - Elapsed time formatting (0s → 1m 00s → 1h 00m 00s)

8. **Welcome/onboarding** — `onboarding/welcome.rs`:
   - ASCII art animation (frame-based, Ctrl+. cycles variants)
   - Sizing requirements (min 37H x 60W for animation)

9. **Approval UI** — `bottom_pane/approvals/`:
   - Risk level color coding
   - Command preview with syntax highlighting

10. **Theme picker** — `theme_picker.rs`:
    - Live preview with diff sample
    - Adaptive layout (wide: side-by-side, narrow: stacked)

### Step 2 — Study OpenCode TUI Visual Design

Read the OpenCode source in `docs/reference-code/opencode/`. Focus on visual patterns:

1. **Color palette** — dark theme: `#0a0a0a` bg, `#fab283` accent, `#5c9cf5` secondary, semantic colors
2. **Minimal borders** — `┃` pipes instead of full box borders, spacious layout
3. **Spinner animation** — braille frames `⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏` at 80ms
4. **Status bar** — what info shown, how formatted (tokens, cost, LSP/MCP indicators)
5. **Message styling** — how they differentiate user/assistant/tool/error messages
6. **Typography** — use of bold, italic, dim, underline for visual hierarchy
7. **Spacing** — generous whitespace, padding, visual grouping without borders

### Step 3 — Audit Current AVA TUI

Read every rendering file in `crates/ava-tui/src/` and document:
- What colors are hardcoded outside `theme.rs`
- What looks dated or harsh
- What border/layout patterns need replacing
- What's missing vs competitors

## Phase 2: Implementation

### Story 1 — New Color System & Theme Engine

Rebuild `state/theme.rs` from scratch.

1. **New default dark theme** with rich RGB values:
   - Background: `#0d1117` (GitHub dark style) or `#0a0a0a` (OpenCode style)
   - Panel/elevated bg: `#161b22`
   - Text: `#e6edf3` (soft white)
   - Text muted: `#7d8590`
   - Text dimmed: `#484f58` (for tool output)
   - Primary: `#58a6ff` (blue)
   - Accent: `#f0883e` (amber)
   - Success: `#3fb950`
   - Error: `#f85149`
   - Warning: `#d29922`
   - Border: `#30363d`
   - Border active: `#58a6ff`
   - Diff added bg: `#1a4721`
   - Diff removed bg: `#67060c`
   - Diff added fg: `#3fb950`
   - Diff removed fg: `#f85149`

2. **Terminal color detection** (new file `rendering/terminal_colors.rs` or similar):
   - Detect TrueColor / 256 / 16 support via `supports_color` crate
   - Downgrade gracefully (map RGB → nearest 256 index → named color)
   - Detect dark vs light background

3. **Eliminate ALL hardcoded colors** — grep for `Color::` in every widget file, move to theme. Especially:
   - Risk level colors in `tool_approval.rs`
   - Message prefix colors in `state/messages.rs`
   - Status bar colors in `status_bar.rs`

4. Update Dracula and Nord themes with improved values.

### Story 2 — Layout Rework

Redesign the screen layout. Kill the heavy borders.

**Target layout:**
```
 AVA │ claude-haiku-4.5 │ 12.3K tokens │ $0.02      ← slim top bar
─────────────────────────────────────────────────────  ← thin separator
                                                       ← clean message area
 You: describe the auth module                         ← user msg (tinted bg)

 The auth module handles...                            ← assistant (no prefix)

 $ cat src/auth.rs                                     ← tool call
 └ pub struct Auth { ... }                             ← output (dimmed)

 ⠹ Working... 3s  (Ctrl+C to interrupt)                ← animated status

─────────────────────────────────────────────────────  ← thin separator
 > type here...                                        ← minimal composer
 Ctrl+K commands  Ctrl+M model  Ctrl+B sidebar         ← muted hint bar
```

**Changes:**
- Remove `Block::default().title("Messages").borders(ALL)` — use borderless or just top/bottom lines
- Remove `Block::default().title("Composer").borders(ALL)` — just `> ` prompt prefix
- Top bar: single line, pipe-separated, information-dense
- Bottom: thin separator + prompt + hints (not a bordered box)
- Sidebar: only on Ctrl+B or width > 120, thin left border only (not full box)

### Story 3 — Message Rendering Overhaul

Rework `state/messages.rs` message formatting.

1. **User messages** — subtle background tint (Codex approach: blend 12% white on dark bg), prefix `You:` in muted text (or no prefix, just different bg)
2. **Assistant messages** — clean markdown rendering, no `❯` prefix, no emoji
3. **Tool calls** — Codex style:
   - `$ command` with syntax highlighting (use syntect for bash)
   - `└ output` in dimmed text, indented, max 5 lines + `... (N more lines)` truncation
   - Show duration and exit code in muted text
4. **Tool results** — dimmed, indented under their tool call (not separate message)
5. **Errors** — `✗ error message` in error color, no emoji
6. **System/info** — very muted, dim modifier
7. **Streaming cursor** — replace blinking `█` with braille spinner

### Story 4 — Markdown & Diff Rendering Improvements

**Markdown** (`rendering/markdown.rs`):
- h1: bold + underlined (currently just bold + primary)
- h2: bold
- h3: bold + italic
- h4-h6: italic
- Code blocks: better syntect theme colors matching new dark theme
- Inline code: accent color (not just backtick coloring)
- Links: primary color + underlined
- Blockquotes: green tint + `│` left border character
- Lists: proper indentation with muted bullets

**Diffs** (`rendering/diff.rs`):
- TrueColor: background tints for added/removed lines (dark green/red from theme)
- 256-color: indexed fallback
- 16-color: foreground only (skip backgrounds that clash)
- Syntax highlighting within diff hunks (if not already done)

### Story 5 — Streaming & Animation

1. **Braille spinner** — new spinner widget with frames `["⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"]` at 80ms intervals
2. **Elapsed time** — show `Working... 3s` then `Working... 1m 23s` during agent execution
3. **Interrupt hint** — `(Ctrl+C to interrupt)` in muted text next to spinner
4. **Smooth streaming** — if feasible, implement line-queue drain for typing effect (Codex's smooth mode). If too complex for this sprint, at least replace the blinking cursor with the braille spinner.
5. **Animation tick** — ensure event loop drives spinner frame advancement (check `event.rs` tick rate)

### Story 6 — Welcome Screen Redesign

1. Centered vertically and horizontally
2. Stylized "AVA" text (bold, primary color, larger if possible with ASCII art)
3. Info display:
   - Model: value in text color
   - Provider: value in text color
   - Working directory: value in text color
4. Quick hints: `Type a message to start │ / for commands │ Ctrl+M to switch model`
5. Adapt to small terminals (skip ASCII art if < 60 wide or < 20 tall)
6. Clean, minimal — no heavy borders

### Story 7 — Ctrl+C Exit Behavior

Implement smart Ctrl+C behavior (like Codex CLI):

1. **Agent is running** → first Ctrl+C interrupts/cancels the agent (already works). Second Ctrl+C within 2 seconds exits the app.
2. **Composer has text** → first Ctrl+C clears the composer input (delete everything typed). Second Ctrl+C within 2 seconds exits the app.
3. **Composer is empty, no agent running** → single Ctrl+C exits the app immediately.

Implementation:
- Track `last_ctrl_c: Option<Instant>` in app state
- On Ctrl+C: check if within 2s of last Ctrl+C → if yes, exit
- Otherwise: if agent running → cancel agent. If composer has text → clear input buffer. If both empty → exit.
- Show a brief hint in the status bar after first Ctrl+C: `"Press Ctrl+C again to exit"` (TTL 2 seconds)

### Story 8 — Tool Approval UI Refresh

1. Move hardcoded risk level colors to theme:
   - Safe → success color
   - Low → primary color
   - Medium → warning color
   - High → error color + bold
   - Critical → error color + bold + underline
2. Command preview with syntax highlighting (use syntect for bash)
3. Clean layout: tool name, arguments, risk level, then action keys at bottom
4. Keyboard hints in muted text at bottom of approval modal

## Validation

```bash
cargo test --workspace
cargo clippy --workspace

# Visual validation:
# 1. cargo run --bin ava
# 2. Dark theme: soft colors, no harsh cyan/yellow, subtle borders
# 3. Messages: clean, no emoji prefixes, tool calls show $ command + dimmed output
# 4. Markdown: headings use bold/underline hierarchy, code blocks well-themed
# 5. Spinner: braille animation during agent work with elapsed time
# 6. Welcome: centered, clean, informative
# 7. Tool approval: risk colors from theme, syntax-highlighted commands
# 8. Test on 80-column terminal — layout should still work
# 9. Test on 160-column terminal — should use space well
```

## Rules

- Phase 1 (research) MUST complete before Phase 2
- Read the ACTUAL source code in `docs/reference-code/` — don't guess at patterns
- Every color must come from `state/theme.rs` — ZERO hardcoded `Color::` in widget files
- Do NOT modify files owned by Sprint 51b (listed above)
- Do NOT break existing functionality — headless mode, tests, all tools must still work
- Test on both wide (120+) and narrow (80 col) terminals
- Conventional commit: `feat(tui): visual rework — colors, layout, messages, rendering`
