# Sprint 16b: Interactive TUI — Ratatui Implementation Prompt

> For AI coding agent. Estimated: 8 features, mix M/L effort.
> **Prerequisite**: Sprint 16a (Rust Agent Stack) must be complete first.
> Run `cargo test --workspace && cargo clippy --workspace` after each feature.

---

## Role

You are implementing Sprint 16 (Interactive TUI) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, Rust-first architecture)
- `docs/research/tui-comparison-matrix.md` (competitor analysis — read the FULL document)
- `crates/ava-agent/src/lib.rs` (agent loop API)
- `crates/ava-llm/src/lib.rs` (LLM provider API)
- `crates/ava-tools/src/lib.rs` (tool registry API)
- `crates/ava-session/src/lib.rs` (session manager API)
- `crates/ava-memory/src/lib.rs` (memory system API)
- `crates/ava-permissions/src/lib.rs` (permission rules API)
- `crates/ava-config/src/lib.rs` (config manager API)
- `crates/ava-platform/src/lib.rs` (filesystem + shell API)

**IMPORTANT**: This is a **pure Rust TUI** using Ratatui + Crossterm + Tokio. It calls directly into existing AVA Rust crates (ava-agent, ava-llm, ava-tools, ava-session, etc.) — NO Node.js, NO IPC bridge, NO TypeScript. The TUI is a new binary crate that composes the existing library crates.

**Sprint 16a provides**: Core tools (read/write/edit/bash/glob/grep), wired Commander with real LLM providers, sandbox execution, and `AgentStack` — the unified entrypoint this TUI calls into.

---

## Pre-Implementation: Competitor Research Phase

**CRITICAL**: Before implementing each feature, you MUST read the relevant competitor reference code and extract best patterns.

For EACH feature:
1. **Read** the listed competitor reference files
2. **Extract** key patterns (architecture, state management, rendering tricks)
3. **Adapt** to AVA's Ratatui + Rust architecture
4. **Implement** (<300 lines/file, idiomatic Rust)
5. **Test** + verify

---

## Setup: Dependencies & Project Structure

### Step 0: Create the Crate

Create `crates/ava-tui/Cargo.toml`:
```toml
[package]
name = "ava-tui"
version = "0.1.0"
edition = "2021"

[[bin]]
name = "ava"
path = "src/main.rs"

[dependencies]
# TUI framework
ratatui = "0.29"
crossterm = { version = "0.28", features = ["event-stream"] }

# Async runtime
tokio = { version = "1", features = ["full"] }
tokio-stream = "0.1"
futures = "0.3"

# Markdown & syntax highlighting
pulldown-cmark = "0.12"
syntect = { version = "5", default-features = false, features = ["default-fancy"] }

# Diff rendering
similar = { version = "2", features = ["unicode"] }

# Fuzzy search
nucleo = "0.5"

# Serialization
serde = { version = "1", features = ["derive"] }
serde_json = "1"

# Workspace crates — direct in-process calls
ava-agent = { path = "../ava-agent" }
ava-llm = { path = "../ava-llm" }
ava-tools = { path = "../ava-tools" }
ava-session = { path = "../ava-session" }
ava-memory = { path = "../ava-memory" }
ava-permissions = { path = "../ava-permissions" }
ava-config = { path = "../ava-config" }
ava-platform = { path = "../ava-platform" }
ava-context = { path = "../ava-context" }
ava-types = { path = "../ava-types" }
ava-db = { path = "../ava-db" }
ava-codebase = { path = "../ava-codebase" }

# CLI argument parsing
clap = { version = "4", features = ["derive"] }

# Error handling
color-eyre = "0.6"

# Logging
tracing = "0.1"
tracing-subscriber = { version = "0.3", features = ["env-filter"] }

# Misc
uuid = { version = "1", features = ["v4"] }
chrono = "0.4"
dirs = "5"
unicode-width = "0.2"

[dev-dependencies]
insta = "1"
tempfile = "3"
```

Register in workspace `Cargo.toml`:
```toml
members = [
  # ... existing crates ...
  "crates/ava-tui",
]
```

### Step 0b: Create TUI Directory Structure

```
crates/ava-tui/
  src/
    main.rs                    # Entry point — CLI args + terminal setup
    app.rs                     # App state machine + main event loop
    event.rs                   # Event types (Key, Agent, Resize, Tick)
    ui/
      mod.rs                   # Root render function
      layout.rs                # Main layout: header + messages + input + footer
      status_bar.rs            # Top/bottom status bars
      sidebar.rs               # Optional info panel
    widgets/
      mod.rs                   # Widget re-exports
      message_list.rs          # Scrollable message history widget
      message.rs               # Single message renderer (user/assistant/tool/error)
      streaming_text.rs        # Real-time token streaming with markdown
      composer.rs              # Multi-line text input widget
      autocomplete.rs          # Fuzzy autocomplete popup
      command_palette.rs       # Ctrl+/ command search
      tool_approval.rs         # 3-stage approval modal overlay
      diff_preview.rs          # Colored unified diff display
      session_list.rs          # Session picker dialog
      dialog.rs                # Generic modal dialog
    rendering/
      mod.rs                   # Rendering re-exports
      markdown.rs              # pulldown-cmark → ratatui Spans conversion
      syntax.rs                # syntect → ratatui styled code blocks
      diff.rs                  # similar → colored diff lines
    state/
      mod.rs                   # State re-exports
      agent.rs                 # Agent execution state + event bridge
      messages.rs              # Message history + scroll position
      input.rs                 # Input buffer + cursor + history
      session.rs               # Session state (wraps ava-session)
      permission.rs            # Tool approval queue
      theme.rs                 # Terminal theme (colors, styles)
      keybinds.rs              # Configurable keyboard shortcuts
    config/
      mod.rs                   # Config re-exports
      cli.rs                   # clap CLI argument definitions
      keybindings.rs           # Keybind config file loader
      themes.rs                # Theme definitions (dracula, nord, default)
  tests/
    app_test.rs                # App state machine tests
    rendering_test.rs          # Markdown + syntax + diff rendering tests
    widgets_test.rs            # Widget unit tests
    integration_test.rs        # Full TUI integration test with mock agent
```

---

## Feature 1: Core App Shell & Event Loop

### Competitor Research
Read these files:
- `docs/reference-code/codex-cli/codex-rs/tui/src/app.rs` (lines 1-200) — App struct, event loop pattern
- `docs/reference-code/codex-cli/codex-rs/tui/src/tui.rs` — Terminal setup/teardown
- `docs/reference-code/codex-cli/codex-rs/tui/src/app_event.rs` — Event types

### What to Build
The foundational Ratatui app with terminal setup, event loop, and layout scaffolding.

**Files:**
- `crates/ava-tui/src/main.rs` — Entry point with clap
- `crates/ava-tui/src/app.rs` — App state machine
- `crates/ava-tui/src/event.rs` — Event enum + event reader task
- `crates/ava-tui/src/ui/mod.rs` — Root render function
- `crates/ava-tui/src/ui/layout.rs` — Main layout
- `crates/ava-tui/src/ui/status_bar.rs` — Status bars
- `crates/ava-tui/src/config/cli.rs` — CLI args
- `crates/ava-tui/src/state/theme.rs` — Theme state

**Implementation:**

Entry point (`main.rs`):
```rust
use clap::Parser;
use color_eyre::Result;

#[derive(Parser)]
#[command(name = "ava", about = "AVA — AI coding assistant")]
struct Cli {
    /// Goal to execute (if provided, submits immediately)
    goal: Option<String>,

    /// Resume last session
    #[arg(short = 'c', long = "continue")]
    resume: bool,

    /// Resume specific session
    #[arg(long)]
    session: Option<String>,

    /// Model to use
    #[arg(long, short)]
    model: Option<String>,

    /// Provider name
    #[arg(long)]
    provider: Option<String>,

    /// Max agent turns
    #[arg(long, default_value = "20")]
    max_turns: usize,

    /// Auto-approve all tool calls
    #[arg(long)]
    yolo: bool,

    /// Theme name
    #[arg(long, default_value = "default")]
    theme: String,
}

#[tokio::main]
async fn main() -> Result<()> {
    color_eyre::install()?;
    let cli = Cli::parse();
    let mut app = App::new(cli)?;
    app.run().await
}
```

App event loop pattern (from Codex CLI):
```rust
pub struct App {
    state: AppState,
    should_quit: bool,
}

impl App {
    pub async fn run(&mut self) -> Result<()> {
        let mut terminal = ratatui::init();
        crossterm::execute!(std::io::stdout(), crossterm::event::EnableBracketedPaste)?;

        let (event_tx, mut event_rx) = tokio::sync::mpsc::unbounded_channel();

        // Spawn terminal event reader
        spawn_event_reader(event_tx.clone());

        // Spawn tick timer (60fps for streaming, 4fps idle)
        spawn_tick_timer(event_tx.clone());

        loop {
            // Render
            terminal.draw(|frame| ui::render(frame, &self.state))?;

            // Handle next event
            if let Some(event) = event_rx.recv().await {
                self.handle_event(event)?;
            }

            if self.should_quit {
                break;
            }
        }

        ratatui::restore();
        Ok(())
    }
}
```

Layout structure:
```
+-----------------------------------------------+
| AVA v0.1.0  |  model: claude-sonnet  | 1.2k tok |  <- StatusBar (top)
+-----------------------------------------------+
|                                               |
|  [messages scroll area]                       |  <- MessageList
|                                               |
+-----------------------------------------------+
| > user input here...                          |  <- Composer
+-----------------------------------------------+
| session: abc123 | turn 3/20 | Ctrl+/ help     |  <- StatusBar (bottom)
+-----------------------------------------------+
```

Theme system:
- 3 built-in themes: `default` (auto dark/light), `dracula`, `nord`
- Colors stored as `ratatui::style::Color` values
- Theme struct: primary, secondary, accent, error, warning, text, text_muted, border, bg
- Diff colors: added (green), removed (red), context (gray), hunk_header (cyan)
- Auto dark/light detection via terminal background color query

**Integration:** The binary is `ava`. Running `ava` with no args launches the TUI. Running `ava "goal"` launches TUI and auto-submits the goal.

### Tests
- `tests/app_test.rs` — App initializes, handles quit event, state transitions
- Theme loading + color resolution

---

## Feature 2: Streaming Chat & Message Rendering

### Competitor Research
Read these files:
- `docs/reference-code/codex-cli/codex-rs/tui/src/chatwidget.rs` (lines 1-300) — Chat widget structure
- `docs/reference-code/codex-cli/codex-rs/tui/src/streaming/chunking.rs` — Adaptive streaming
- `docs/reference-code/codex-cli/codex-rs/tui/src/markdown_render.rs` — Markdown to ratatui Spans
- `docs/reference-code/codex-cli/codex-rs/tui/src/markdown_stream.rs` — Streaming markdown

### What to Build
Real-time streaming message display with adaptive batching and markdown rendering.

**Files:**
- `crates/ava-tui/src/state/messages.rs` — Message history + scroll state
- `crates/ava-tui/src/widgets/message_list.rs` — Scrollable message list widget
- `crates/ava-tui/src/widgets/message.rs` — Individual message renderer
- `crates/ava-tui/src/widgets/streaming_text.rs` — Live streaming text
- `crates/ava-tui/src/rendering/markdown.rs` — pulldown-cmark → ratatui Lines
- `crates/ava-tui/src/rendering/syntax.rs` — syntect code highlighting

**Implementation:**

Adaptive tick rate (from Codex CLI pattern):
```rust
/// During streaming: 60fps (16ms ticks) for smooth token display
/// During idle: 4fps (250ms ticks) to save CPU
fn tick_interval(is_streaming: bool) -> Duration {
    if is_streaming {
        Duration::from_millis(16) // 60fps
    } else {
        Duration::from_millis(250) // 4fps idle
    }
}
```

Message types to render:
- **User message** — plain text with `>` prefix, themed user color
- **Assistant message** — markdown rendered to styled ratatui Lines
- **Tool call** — tool name + args (collapsible) + result + duration
- **Thinking** — collapsible thinking block (dimmed/italic)
- **Error** — red bordered block
- **System** — info messages (turn markers, compression notices)

Markdown rendering with `pulldown-cmark` → `ratatui::text::Line`:
```rust
use pulldown_cmark::{Event, Parser, Tag};
use ratatui::text::{Line, Span};
use ratatui::style::{Color, Modifier, Style};

pub fn markdown_to_lines(md: &str, theme: &Theme) -> Vec<Line<'static>> {
    let parser = Parser::new(md);
    let mut lines = Vec::new();
    let mut current_spans: Vec<Span<'static>> = Vec::new();
    let mut style_stack: Vec<Style> = vec![Style::default()];

    for event in parser {
        match event {
            Event::Text(text) => {
                let style = *style_stack.last().unwrap_or(&Style::default());
                current_spans.push(Span::styled(text.to_string(), style));
            }
            Event::Code(code) => {
                current_spans.push(Span::styled(
                    format!("`{}`", code),
                    Style::default().fg(theme.accent),
                ));
            }
            Event::Start(Tag::Heading { level, .. }) => {
                style_stack.push(Style::default()
                    .fg(theme.primary)
                    .add_modifier(Modifier::BOLD));
            }
            Event::Start(Tag::Strong) => {
                style_stack.push(Style::default().add_modifier(Modifier::BOLD));
            }
            Event::Start(Tag::Emphasis) => {
                style_stack.push(Style::default().add_modifier(Modifier::ITALIC));
            }
            Event::Start(Tag::CodeBlock(kind)) => {
                // Flush current line, start code block
                // Use syntect for highlighting (see rendering/syntax.rs)
            }
            Event::End(_) => { style_stack.pop(); }
            Event::SoftBreak | Event::HardBreak => {
                lines.push(Line::from(std::mem::take(&mut current_spans)));
            }
            _ => {}
        }
    }
    if !current_spans.is_empty() {
        lines.push(Line::from(current_spans));
    }
    lines
}
```

Syntax highlighting with `syntect`:
```rust
use syntect::highlighting::ThemeSet;
use syntect::parsing::SyntaxSet;
use syntect::easy::HighlightLines;

lazy_static! {
    static ref SYNTAX_SET: SyntaxSet = SyntaxSet::load_defaults_newlines();
    static ref THEME_SET: ThemeSet = ThemeSet::load_defaults();
}

pub fn highlight_code(code: &str, language: &str) -> Vec<Line<'static>> {
    let syntax = SYNTAX_SET.find_syntax_by_token(language)
        .unwrap_or_else(|| SYNTAX_SET.find_syntax_plain_text());
    let theme = &THEME_SET.themes["base16-ocean.dark"];
    let mut h = HighlightLines::new(syntax, theme);

    code.lines().map(|line| {
        let ranges = h.highlight_line(line, &SYNTAX_SET).unwrap_or_default();
        let spans: Vec<Span> = ranges.into_iter().map(|(style, text)| {
            Span::styled(text.to_string(), syntect_to_ratatui_style(style))
        }).collect();
        Line::from(spans)
    }).collect()
}
```

Scrolling:
- Track `scroll_offset: usize` and `auto_scroll: bool`
- Page up/down moves by half terminal height
- Auto-scroll to bottom on new messages (unless user scrolled up)
- Show "New messages below" indicator when scrolled up
- Home/End to jump to top/bottom

### Tests
- `tests/rendering_test.rs` — Markdown to Lines, syntax highlighting, message rendering
- Message types all render correctly
- Scroll position tracking

---

## Feature 3: Composer Input & History

### Competitor Research
Read these files:
- `docs/reference-code/codex-cli/codex-rs/tui/src/chatwidget.rs` — Search for `input` / `cursor` / `compose`
- `docs/reference-code/codex-cli/codex-rs/tui/src/clipboard_paste.rs` — Paste handling
- `docs/reference-code/codex-cli/codex-rs/tui/src/insert_history.rs` — Input history

### What to Build
Multi-line text input with history navigation, slash commands, and @mentions.

**Files:**
- `crates/ava-tui/src/state/input.rs` — Input buffer state
- `crates/ava-tui/src/widgets/composer.rs` — Input widget
- `crates/ava-tui/src/widgets/autocomplete.rs` — Autocomplete popup

**Implementation:**

Input state:
```rust
pub struct InputState {
    /// Current input buffer (supports multi-line)
    pub buffer: String,
    /// Cursor position (byte offset)
    pub cursor: usize,
    /// Command history
    pub history: Vec<String>,
    /// History navigation index (None = current input)
    pub history_index: Option<usize>,
    /// Saved current input when navigating history
    pub saved_input: String,
    /// Autocomplete state
    pub autocomplete: Option<AutocompleteState>,
}
```

Composer features:
- Multi-line input (Shift+Enter or Alt+Enter for newline, Enter to submit)
- History navigation (Up/Down when cursor at line start/end)
- Persistent history saved to `~/.ava/cli-history.jsonl`
- Slash command detection: typing `/` shows autocomplete popup
- @mention detection: typing `@` shows file/agent autocomplete
- Bracketed paste handling for multi-line content
- Ctrl+C: cancel current input (or abort running agent)
- Ctrl+D: exit TUI
- Word navigation: Ctrl+Left/Right, Ctrl+Backspace (delete word)
- Ctrl+A / Ctrl+E: Home / End of line
- Ctrl+U: Clear line
- Ctrl+W: Delete word backward

Autocomplete popup (using `nucleo` fuzzy matcher):
```rust
use nucleo::Nucleo;

pub struct AutocompleteState {
    pub trigger: AutocompleteTrigger, // Slash or AtMention
    pub query: String,
    pub items: Vec<AutocompleteItem>,
    pub selected: usize,
    pub matcher: Nucleo<String>,
}
```

- Shows below input when typing `/` or `@`
- Fuzzy search with `nucleo` (same engine as Helix editor)
- Arrow keys to navigate, Tab/Enter to select, Escape to dismiss
- Slash commands: `/help`, `/model`, `/session`, `/clear`, `/compact`, `/recipe`, `/praxis`
- @mentions: `@file.ts` (reads file into context)

### Tests
- Input buffer operations (insert, delete, word nav, cursor movement)
- History navigation (up/down, save/restore)
- Autocomplete trigger detection and fuzzy matching

---

## Feature 4: Tool Approval System

### Competitor Research
Read these files:
- `docs/reference-code/codex-cli/codex-rs/tui/src/bottom_pane/approval_overlay.rs` — Modal approval overlay
- `docs/reference-code/codex-cli/codex-rs/tui/src/diff_render.rs` — Diff rendering
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/permission.tsx` — 3-stage approval

### What to Build
Interactive 3-stage tool approval flow as a modal overlay.

**Files:**
- `crates/ava-tui/src/state/permission.rs` — Permission state + queue
- `crates/ava-tui/src/widgets/tool_approval.rs` — Approval overlay widget
- `crates/ava-tui/src/widgets/diff_preview.rs` — Diff display widget
- `crates/ava-tui/src/rendering/diff.rs` — similar → colored Lines

**Implementation:**

3-stage approval flow (adapted from OpenCode + Codex):

**Stage 1 — Preview:**
- Shows tool name + icon (bash → `$`, edit → `~`, read → eye, etc.)
- Shows arguments formatted as key-value pairs
- For file edits: shows unified diff with +/- coloring
- For bash commands: shows command + working directory

**Stage 2 — Action Selection:**
- `[a] Allow once` — run this tool call
- `[s] Allow for session` — auto-approve this tool for rest of session
- `[r] Reject` — deny this call
- `[y] YOLO mode` — auto-approve everything
- Press shortcut key directly (no arrow navigation needed)

**Stage 3 — Rejection reason (optional):**
- If rejected, optional text input for rejection message sent back to agent
- Enter to confirm, Escape to skip

Permission queue:
```rust
pub struct PermissionState {
    pub queue: VecDeque<ApprovalRequest>,
    pub current_stage: ApprovalStage,
    pub session_approved: HashSet<String>, // tool names auto-approved
    pub yolo_mode: bool,
    pub rejection_input: String,
}

pub enum ApprovalStage {
    Preview,
    ActionSelect,
    RejectionReason,
}
```

Diff rendering with `similar`:
```rust
use similar::{ChangeTag, TextDiff};
use ratatui::text::{Line, Span};
use ratatui::style::{Color, Style};

pub fn render_diff(old: &str, new: &str, theme: &Theme) -> Vec<Line<'static>> {
    let diff = TextDiff::from_lines(old, new);
    let mut lines = Vec::new();

    for change in diff.iter_all_changes() {
        let (sign, color) = match change.tag() {
            ChangeTag::Delete => ("-", theme.diff_removed),
            ChangeTag::Insert => ("+", theme.diff_added),
            ChangeTag::Equal => (" ", theme.diff_context),
        };
        lines.push(Line::from(Span::styled(
            format!("{}{}", sign, change.value().trim_end_matches('\n')),
            Style::default().fg(color),
        )));
    }
    lines
}
```

The overlay renders on top of the main UI using ratatui's `Clear` + centered `Rect` pattern:
```rust
// In ui render function:
if let Some(approval) = &state.permission.queue.front() {
    let area = centered_rect(80, 60, frame.area());
    frame.render_widget(Clear, area);
    frame.render_widget(
        ToolApprovalWidget::new(approval, &state.permission, &state.theme),
        area,
    );
}
```

### Tests
- 3 approval stages render correctly
- Diff coloring (additions green, deletions red)
- Queue management (enqueue, dequeue, session approvals)
- YOLO mode bypasses all

---

## Feature 5: Keyboard Shortcuts & Command Palette

### Competitor Research
Read these files:
- `docs/reference-code/codex-cli/codex-rs/tui/src/app.rs` — Search for `KeyCode` / `handle_key`
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/keybind.tsx` — Keybind system

### What to Build
Configurable keyboard shortcuts with a fuzzy command palette.

**Files:**
- `crates/ava-tui/src/state/keybinds.rs` — Keybind state + matching
- `crates/ava-tui/src/config/keybindings.rs` — Keybind config file
- `crates/ava-tui/src/widgets/command_palette.rs` — Fuzzy command search

**Implementation:**

Default keybindings:
```rust
pub fn default_keybinds() -> HashMap<Action, Vec<KeyBinding>> {
    hashmap! {
        Action::CommandPalette => vec![kb(Ctrl, '/')],
        Action::NewSession => vec![kb(Ctrl, 'n')],
        Action::SessionList => vec![kb(Ctrl, 'k')],
        Action::ModelSwitch => vec![kb(Ctrl, 'm')],
        Action::ScrollUp => vec![kb(None, KeyCode::PageUp)],
        Action::ScrollDown => vec![kb(None, KeyCode::PageDown)],
        Action::ScrollTop => vec![kb(None, KeyCode::Home)],
        Action::ScrollBottom => vec![kb(None, KeyCode::End)],
        Action::ToggleSidebar => vec![kb(Ctrl, 's')],
        Action::ToggleThinking => vec![kb(Ctrl, 't')],
        Action::Cancel => vec![kb(Ctrl, 'c')],
        Action::Quit => vec![kb(Ctrl, 'd')],
        Action::YoloToggle => vec![kb(Ctrl, 'y')],
    }
}
```

Keybind config: `~/.ava/keybindings.json` (overrides defaults).

Command palette:
- Opens with Ctrl+/ (like VS Code)
- Fuzzy search all available commands using `nucleo`
- Shows command name + keybinding hint + category
- Categories: Session, Model, Navigation, View, Agent
- Enter to execute, Escape to close
- Recent commands shown first

Commands include:
- New Session, Switch Session, Rename Session, Delete Session
- Switch Model, Switch Provider
- Toggle Sidebar, Toggle Thinking Blocks, Toggle Tool Details
- Clear Chat, Export Session, Fork Session
- YOLO Mode Toggle
- Run Recipe, Switch Praxis Mode

### Tests
- Key matching against configured bindings
- Config file loading + override
- Fuzzy search ranking in command palette

---

## Feature 6: Session Management UI

### Competitor Research
Read these files:
- `docs/reference-code/codex-cli/codex-rs/tui/src/resume_picker.rs` — Session resume picker
- `docs/reference-code/codex-cli/codex-rs/tui/src/history_cell.rs` — History management
- `crates/ava-session/src/lib.rs` — AVA's existing session manager (USE THIS)

### What to Build
Session list dialog, switching, resume, and fork — wrapping existing `ava-session` crate.

**Files:**
- `crates/ava-tui/src/state/session.rs` — Session state (wraps ava_session::SessionManager)
- `crates/ava-tui/src/widgets/session_list.rs` — Session picker dialog

**Implementation:**

Session state wraps `ava_session::SessionManager` directly:
```rust
use ava_session::SessionManager;

pub struct SessionState {
    manager: SessionManager,
    pub current_session: Option<Session>,
    pub sessions: Vec<Session>, // cached list for UI
}

impl SessionState {
    pub fn new(db_path: &Path) -> Result<Self> {
        let manager = SessionManager::new(db_path)?;
        Ok(Self { manager, current_session: None, sessions: Vec::new() })
    }

    pub fn create_session(&mut self) -> Result<Session> {
        let session = self.manager.create()?;
        self.current_session = Some(session.clone());
        Ok(session)
    }

    pub fn switch_to(&mut self, id: Uuid) -> Result<()> {
        self.current_session = self.manager.get(id)?;
        Ok(())
    }

    pub fn fork_current(&mut self) -> Result<Session> {
        let current = self.current_session.as_ref().ok_or(eyre!("No session"))?;
        let forked = self.manager.fork(current)?;
        self.current_session = Some(forked.clone());
        Ok(forked)
    }

    pub fn list_recent(&self, limit: usize) -> Result<Vec<Session>> {
        self.manager.list_recent(limit)
    }

    pub fn search(&self, query: &str) -> Result<Vec<Session>> {
        self.manager.search(query)
    }
}
```

Session list dialog (Ctrl+K):
- Shows all sessions sorted by last updated
- Each row: session name, date, message count, model used
- Fuzzy search to filter with `nucleo`
- Enter to switch, `d` to delete
- `+` or `n` to create new session

Startup behavior:
- `ava` — new session (show composer prompt)
- `ava --continue` or `ava -c` — resume last session
- `ava --session <id>` — resume specific session
- `ava "goal"` — new session, auto-submit goal

### Tests
- Session create, switch, resume, fork via manager
- Session list rendering + fuzzy search
- Startup modes (new, continue, specific session, with goal)

---

## Feature 7: Sidebar & Info Panels

### Competitor Research
Read these files:
- `docs/reference-code/codex-cli/codex-rs/tui/src/chatwidget.rs` — Search for sidebar/panel/status sections
- `docs/reference-code/codex-cli/codex-rs/tui/src/status/` — Status display modules

### What to Build
Optional sidebar showing session metadata, diffs, and agent status.

**Files:**
- `crates/ava-tui/src/ui/sidebar.rs` — Sidebar render
- `crates/ava-tui/src/ui/layout.rs` — Update to include sidebar

**Implementation:**

Sidebar layout (shown when terminal width > 120 cols, toggle with Ctrl+S):
```
+------------------+
| Session          |
|   abc123 (5 min) |
+------------------+
| Model            |
|   claude-sonnet  |
|   tokens: 12.4k  |
+------------------+
| Files Changed    |
|   M src/app.rs   |
|   + src/util.rs  |
|   M Cargo.toml   |
+------------------+
| Agent Status     |
|   Turn 3/20      |
|   Running...     |
+------------------+
```

Sections:
- **Session**: name, duration, ID
- **Model**: current model, token usage (input/output), cost estimate
- **Files Changed**: list of files modified in this session (tracked from tool events)
- **Agent Status**: current turn, max turns, running/idle/waiting for approval
- **Errors**: count of errors in session (if any)

Responsive layout:
```rust
pub fn build_layout(area: Rect, show_sidebar: bool) -> (Rect, Option<Rect>) {
    if show_sidebar && area.width > 120 {
        let chunks = Layout::default()
            .direction(Direction::Horizontal)
            .constraints([
                Constraint::Min(60),           // Main content
                Constraint::Length(42),         // Sidebar
            ])
            .split(area);
        (chunks[0], Some(chunks[1]))
    } else {
        (area, None)
    }
}
```

### Tests
- Sidebar renders all sections
- Responsive show/hide at width threshold
- Sidebar toggle via keybind

---

## Feature 8: Agent Integration — Direct Rust Calls

### Competitor Research
Read these files:
- `docs/reference-code/codex-cli/codex-rs/tui/src/app.rs` — Search for `thread_manager` / `send_message`
- `crates/ava-agent/src/lib.rs` — AVA's AgentLoop API (USE THIS)
- `crates/ava-llm/src/lib.rs` — AVA's LLM provider API (USE THIS)
- `crates/ava-tools/src/lib.rs` — AVA's ToolRegistry API (USE THIS)

### What to Build
Bridge between the TUI and AVA's existing Rust crates — direct function calls, no IPC.

**Files:**
- `crates/ava-tui/src/state/agent.rs` — Agent execution state
- `crates/ava-tui/src/app.rs` — Update to wire agent events

**Implementation:**

Agent state — wraps `ava_agent::AgentLoop` directly:
```rust
use ava_agent::{AgentLoop, AgentConfig, AgentEvent};
use ava_llm::ModelRouter;
use ava_tools::ToolRegistry;
use ava_permissions::PermissionEngine;

pub struct AgentState {
    pub is_running: bool,
    pub current_turn: usize,
    pub max_turns: usize,
    pub tokens_used: TokenUsage,
    pub abort_handle: Option<tokio::task::AbortHandle>,
}

impl AgentState {
    pub fn start(
        &mut self,
        goal: String,
        config: AgentConfig,
        router: Arc<ModelRouter>,
        tools: Arc<ToolRegistry>,
        event_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let handle = tokio::spawn(async move {
            let agent = AgentLoop::new(config);
            let provider = router.route(RoutingTaskType::CodeGeneration);

            agent.run_streaming(goal, provider, tools, |event| {
                let _ = event_tx.send(AppEvent::Agent(event));
            }).await
        });

        self.abort_handle = Some(handle.abort_handle());
        self.is_running = true;
    }

    pub fn abort(&mut self) {
        if let Some(handle) = self.abort_handle.take() {
            handle.abort();
        }
        self.is_running = false;
    }
}
```

Event flow:
1. User types goal in Composer, presses Enter
2. `AgentState::start()` spawns tokio task running `AgentLoop::run_streaming()`
3. Agent events (`AgentEvent::Token`, `ToolCall`, `ToolResult`, `Complete`, `Error`) sent to event channel
4. App event loop receives `AppEvent::Agent(event)` → updates `MessageState`
5. Tool calls that need approval → pushed to `PermissionState` queue → overlay shown
6. User approves/rejects → result sent back to agent via oneshot channel
7. Agent completes → summary message added, `is_running = false`

Permission bridge:
```rust
// When agent requests tool approval:
AppEvent::Agent(AgentEvent::ToolCall(call)) => {
    if self.state.permission.yolo_mode
        || self.state.permission.session_approved.contains(&call.tool_name)
    {
        // Auto-approve
        call.approve_tx.send(ToolApproval::Allowed).ok();
    } else {
        // Queue for user approval
        self.state.permission.queue.push_back(ApprovalRequest {
            call,
            approve_tx,
        });
    }
}
```

Initialization — compose all crates at startup:
```rust
pub fn init_agent_stack(config: &CliConfig) -> Result<AgentStack> {
    // Load config
    let config_mgr = ava_config::ConfigManager::load()?;

    // Set up LLM providers
    let mut router = ModelRouter::new("default");
    // Register providers from config (Anthropic, OpenAI, etc.)

    // Set up tool registry
    let mut tools = ToolRegistry::new();
    // Register core tools (read, write, edit, bash, glob, grep)

    // Set up permissions
    let perms = ava_permissions::PermissionEngine::new();

    // Set up session
    let db_path = dirs::home_dir().unwrap().join(".ava/data.db");
    let session_mgr = ava_session::SessionManager::new(&db_path)?;

    // Set up memory
    let memory = ava_memory::MemorySystem::new(&db_path)?;

    Ok(AgentStack { router, tools, perms, session_mgr, memory })
}
```

**Key point**: This is the integration feature that makes the TUI a real agent. All previous features built the UI shell. This feature wires the UI to the agent execution pipeline using direct Rust function calls — no serialization, no IPC, no subprocess. The `ava-tui` binary is a single process containing the entire AVA agent stack.

### Tests
- Agent start, event flow, abort
- Permission bridge (auto-approve, queue, yolo)
- Full integration: mock LLM → agent loop → TUI events → message state

---

## Pre-Feature Smoke Test: Verify Rust Agent Stack

**Before starting on TUI features**, verify the Rust agent stack from Sprint 16a/16c is working end-to-end.

### Step 1: Build and test the workspace
```bash
cargo test --workspace
cargo clippy --workspace
```
If tests fail, fix before proceeding.

### Step 2: Create a minimal CLI smoke test binary

**File:** `crates/ava-tui/src/bin/smoke.rs` (temporary, delete after TUI is working)

```rust
//! Smoke test: verify AgentStack runs end-to-end with a mock provider
use ava_agent::stack::{AgentStack, AgentStackConfig, AgentEvent};
use ava_llm::providers::mock::MockProvider;
use std::sync::Arc;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

#[tokio::main]
async fn main() -> color_eyre::Result<()> {
    color_eyre::install()?;
    let temp_dir = tempfile::tempdir()?;

    let stack = AgentStack::new(AgentStackConfig {
        data_dir: temp_dir.path().to_path_buf(),
        injected_provider: Some(Arc::new(MockProvider::new())),
        max_turns: 3,
        ..Default::default()
    }).await?;

    let (tx, mut rx) = mpsc::unbounded_channel();
    let cancel = CancellationToken::new();

    println!("Running agent with mock provider...");
    let handle = {
        let cancel = cancel.clone();
        tokio::spawn(async move {
            stack.run("Say hello", 3, Some(tx), cancel).await
        })
    };

    // Print events as they arrive
    while let Some(event) = rx.recv().await {
        match &event {
            AgentEvent::Token(t) => print!("{t}"),
            AgentEvent::ToolCall(tc) => println!("\n[tool: {}]", tc),
            AgentEvent::ToolResult(tr) => println!("[result: {}]", tr),
            AgentEvent::Progress(p) => println!("[progress: {}]", p),
            AgentEvent::Complete(_) => { println!("\n[complete]"); break; }
            AgentEvent::Error(e) => { eprintln!("\n[error: {}]", e); break; }
        }
    }

    let result = handle.await??;
    println!("\nSmoke test result: success={}, turns={}", result.success, result.turns);
    Ok(())
}
```

Add to `Cargo.toml`:
```toml
[[bin]]
name = "ava-smoke"
path = "src/bin/smoke.rs"
```

Run it:
```bash
cargo run -p ava-tui --bin ava-smoke
```

If this prints events and completes successfully, the Rust agent stack is verified. Proceed to Feature 1.

### Step 3 (optional): Real provider smoke test

If credentials are configured at `~/.ava/credentials.json`, test with a real provider:
```bash
# Set in credentials.json: { "providers": { "openrouter": { "api_key": "..." } } }
cargo run -p ava-tui --bin ava-smoke -- --real --provider openrouter --model anthropic/claude-sonnet-4.6
```

This requires adding a `--real` flag to the smoke binary that skips the mock provider injection and lets `AgentStack` load credentials normally.

---

## Post-Implementation Verification

After ALL 8 features:

1. `cargo test --workspace` — all tests pass
2. `cargo clippy --workspace -- -D warnings` — no warnings
3. `cargo build --release -p ava-tui` — binary builds
4. Verify no files exceed 300 lines
5. Smoke test: `cargo run -p ava-tui --bin ava-smoke` — agent pipeline works
6. Manual test: `cargo run -p ava-tui` — TUI launches
7. Manual test: `cargo run -p ava-tui -- "hello"` — submits goal
8. Binary size check: `ls -lh target/release/ava` (should be ~10-20MB)
9. Delete `src/bin/smoke.rs` if TUI is fully working
10. Commit: `git commit -m "feat(sprint-16b): interactive TUI with ratatui"`

---

## File Change Summary

| Action | File |
|--------|------|
| CREATE | `crates/ava-tui/Cargo.toml` |
| CREATE | `crates/ava-tui/src/main.rs` |
| CREATE | `crates/ava-tui/src/app.rs` |
| CREATE | `crates/ava-tui/src/event.rs` |
| CREATE | `crates/ava-tui/src/ui/mod.rs` |
| CREATE | `crates/ava-tui/src/ui/layout.rs` |
| CREATE | `crates/ava-tui/src/ui/status_bar.rs` |
| CREATE | `crates/ava-tui/src/ui/sidebar.rs` |
| CREATE | `crates/ava-tui/src/widgets/mod.rs` |
| CREATE | `crates/ava-tui/src/widgets/message_list.rs` |
| CREATE | `crates/ava-tui/src/widgets/message.rs` |
| CREATE | `crates/ava-tui/src/widgets/streaming_text.rs` |
| CREATE | `crates/ava-tui/src/widgets/composer.rs` |
| CREATE | `crates/ava-tui/src/widgets/autocomplete.rs` |
| CREATE | `crates/ava-tui/src/widgets/command_palette.rs` |
| CREATE | `crates/ava-tui/src/widgets/tool_approval.rs` |
| CREATE | `crates/ava-tui/src/widgets/diff_preview.rs` |
| CREATE | `crates/ava-tui/src/widgets/session_list.rs` |
| CREATE | `crates/ava-tui/src/widgets/dialog.rs` |
| CREATE | `crates/ava-tui/src/rendering/mod.rs` |
| CREATE | `crates/ava-tui/src/rendering/markdown.rs` |
| CREATE | `crates/ava-tui/src/rendering/syntax.rs` |
| CREATE | `crates/ava-tui/src/rendering/diff.rs` |
| CREATE | `crates/ava-tui/src/state/mod.rs` |
| CREATE | `crates/ava-tui/src/state/agent.rs` |
| CREATE | `crates/ava-tui/src/state/messages.rs` |
| CREATE | `crates/ava-tui/src/state/input.rs` |
| CREATE | `crates/ava-tui/src/state/session.rs` |
| CREATE | `crates/ava-tui/src/state/permission.rs` |
| CREATE | `crates/ava-tui/src/state/theme.rs` |
| CREATE | `crates/ava-tui/src/state/keybinds.rs` |
| CREATE | `crates/ava-tui/src/config/mod.rs` |
| CREATE | `crates/ava-tui/src/config/cli.rs` |
| CREATE | `crates/ava-tui/src/config/keybindings.rs` |
| CREATE | `crates/ava-tui/src/config/themes.rs` |
| CREATE | `crates/ava-tui/tests/app_test.rs` |
| CREATE | `crates/ava-tui/tests/rendering_test.rs` |
| CREATE | `crates/ava-tui/tests/widgets_test.rs` |
| CREATE | `crates/ava-tui/tests/integration_test.rs` |
| MODIFY | `Cargo.toml` (add ava-tui to workspace members) |
