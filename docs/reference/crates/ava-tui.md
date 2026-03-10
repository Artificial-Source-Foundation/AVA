# ava-tui Crate Reference

> Pure Rust TUI binary for AVA. Built on **Ratatui + Crossterm + Tokio**.
> This is the primary user interface for the CLI agent.

**Crate path:** `crates/ava-tui/`
**Binary:** `ava` (+ `ava-smoke` for mock smoke tests)
**Lines of code:** ~5,500+

---

## Table of Contents

- [Overview](#overview)
- [Entry Point](#entry-point)
- [App Struct](#app-struct)
- [State Management](#state-management)
  - [AgentState](#agentstate)
  - [MessageState](#messagestate)
  - [InputState](#inputstate)
  - [SessionState](#sessionstate)
  - [PermissionState](#permissionstate)
  - [KeybindState](#keybindstate)
  - [Theme](#theme)
  - [VoiceState](#voicestate)
- [Widgets](#widgets)
  - [SelectListState\<T\>](#selectliststate-t)
  - [MessageList](#messagelist)
  - [Composer](#composer)
  - [CommandPalette](#commandpalette)
  - [ModelSelector](#modelselector)
  - [SessionList](#sessionlist)
  - [ToolApproval](#toolapproval)
  - [ToolList](#toollist)
  - [ProviderConnect](#providerconnect)
  - [Welcome](#welcome)
  - [SlashMenu](#slashmenu)
  - [Autocomplete](#autocomplete)
  - [TokenBuffer](#tokenbuffer)
  - [Message](#message)
- [UI Layout](#ui-layout)
- [UI Components](#ui-components)
  - [Top Bar / Status Bar](#top-bar--status-bar)
  - [Context Bar](#context-bar)
  - [Sidebar](#sidebar)
- [Event System](#event-system)
- [Event Handler](#event-handler)
- [Slash Commands](#slash-commands)
- [ViewMode](#viewmode)
- [Modal System](#modal-system)
- [Headless Mode](#headless-mode)
- [CLI Flags](#cli-flags)
- [Auth Subcommand](#auth-subcommand)
- [Review Subcommand](#review-subcommand)
- [Voice Input](#voice-input)
- [Configuration](#configuration)

---

## Overview

`ava-tui` is a fully async terminal UI built with:

- **Ratatui** for widget rendering and layout
- **Crossterm** for terminal I/O (raw mode, mouse capture, alternate screen)
- **Tokio** for async runtime (agent tasks, event channels, timers)
- **syntect** for syntax highlighting in code blocks
- **pulldown-cmark** for Markdown parsing in assistant messages
- **nucleo** for fuzzy search in modal lists

The crate produces two binaries:
- `ava` — the main interactive TUI and headless runner
- `ava-smoke` — a mock-based smoke test binary

**File:** `src/lib.rs` (lines 1-30)
Module declarations gate `voice` and `audio` behind `feature = "voice"`.

---

## Entry Point

**File:** `src/main.rs`

The `main()` function routes to one of four modes:

1. **TUI mode** (default) — `App::new(args).run()`
2. **Headless mode** — `run_headless(args)` when `--headless` flag is set
3. **Review mode** — `run_review(args)` for `ava review` subcommand
4. **Auth mode** — `run_auth(args)` for `ava auth` subcommand

Logging is initialized with two layers:
- File layer: writes to `~/.ava/logs/ava.log` (DEBUG level)
- Stderr layer: only active in headless mode (WARN level)

---

## App Struct

**File:** `src/app/mod.rs`

The `App` struct is the top-level orchestrator:

```rust
pub struct App {
    pub state: AppState,
    should_quit: bool,
    pending_goal: Option<String>,
    is_streaming: bool,
    token_buffer: TokenBuffer,
    // voice fields (feature-gated)
    question_rx: Option<mpsc::UnboundedReceiver<QuestionRequest>>,
}
```

### AppState

All UI state lives in `AppState`, which is passed by `&mut` reference to every render and event handler:

```rust
pub struct AppState {
    pub theme: Theme,
    pub messages: MessageState,
    pub input: InputState,
    pub session: SessionState,
    pub permission: PermissionState,
    pub keybinds: KeybindState,
    pub agent: AgentState,
    pub agent_mode: AgentMode,
    pub show_sidebar: bool,
    pub command_palette: CommandPaletteState,
    pub session_list: SessionListState,
    pub model_selector: ModelSelectorState,
    pub tool_list: ToolListState,
    pub provider_connect: ProviderConnectState,
    pub theme_selector: SelectListState<String>,
    pub agent_list: SelectListState<usize>,
    pub active_modal: Option<ModalType>,
    pub view_mode: ViewMode,
    pub status_message: Option<StatusMessage>,
    pub voice: VoiceState,
    pub model_catalog: Option<Arc<ModelCatalog>>,
    pub todo_items: Vec<TodoItem>,
    pub question: Option<QuestionState>,
}
```

### App::new

`App::new(args: CliArgs)` initializes all state from CLI arguments:
- Resolves provider/model via `resolve_provider_model()` priority chain
- Creates `AgentStack` with resolved config
- Loads custom keybindings from `~/.ava/keybindings.json`
- Loads theme (from `--theme` flag or config)
- Sets up session manager and model catalog
- If `args.goal` is provided, stores it as `pending_goal`

### App::run

`App::run()` is the main event loop:

1. Sets up terminal (alternate screen, raw mode, mouse capture)
2. Enters a `loop` with `tokio::select!` on:
   - `event_rx.recv()` — terminal events (keys, mouse, paste, resize, tick)
   - `agent_rx.recv()` — agent events (tokens, tool calls, completion)
   - `question_rx.recv()` — agent questions requiring user input
3. On each iteration: renders frame, handles events, checks `should_quit`
4. On exit: restores terminal, saves session with metadata

**Key handling priority** (in `App::run`):
1. Modal-specific handlers (if `active_modal` is `Some`)
2. Global keybinds (Ctrl+C, Ctrl+K, etc.)
3. Slash autocomplete menu (if visible)
4. Tab for agent mode cycling
5. Normal input editing

### finish_run

Saves the current session with metadata including:
- `title` — generated from first user message
- `provider` / `model` — current provider and model names
- `agent_mode` — Code or Plan
- `thinking_level` — current thinking level

---

## State Management

### AgentState

**File:** `src/state/agent.rs`

Wraps `Option<Arc<AgentStack>>` and manages agent lifecycle:

```rust
pub struct AgentState {
    pub stack: Option<Arc<AgentStack>>,
    pub turn: u32,
    pub activity: AgentActivity,
    pub total_input_tokens: u64,
    pub total_output_tokens: u64,
    pub total_cost_usd: f64,
    pub cancel_token: CancellationToken,
    pub agent_task: Option<JoinHandle<()>>,
    pub sub_agents: Vec<SubAgentInfo>,
    pub thinking_level: ThinkingLevel,
}
```

**Key types:**

- `AgentActivity` — `Idle`, `Thinking`, `ExecutingTool(String)`
- `AgentMode` — `Code` (default), `Plan` (analysis-only suffix appended to prompts)
- `SubAgentInfo` — tracks spawned sub-agents with `id`, `description`, `accumulated_tokens`, `session_messages`
- `ThinkingLevel` — controls extended thinking in supported models

**Key methods:**

| Method | Description |
|--------|-------------|
| `start(goal, history, tx)` | Spawns tokio task running `agent.run()`, sends `AgentEvent`s via channel |
| `abort()` | Cancels via `cancel_token` + aborts the task handle |
| `finish()` | Sets activity to Idle, clears task handle |
| `switch_model(provider, model)` | Delegates to `AgentStack::switch_model()` with `RwLock` |
| `reload_tools()` | Re-registers tools from registry |
| `reload_mcp()` | Re-registers MCP servers |
| `list_tools_with_source()` | Returns `Vec<(String, ToolSource)>` |
| `set_thinking_level(level)` | Updates thinking level on the stack |
| `cycle_thinking()` | Rotates through Off -> Low -> Medium -> High -> Off |
| `lookup_context_window()` | Looks up context window size from compiled-in registry |

### MessageState

**File:** `src/state/messages.rs`

Manages the message list and scroll state:

```rust
pub struct MessageState {
    pub messages: Vec<UiMessage>,
    pub auto_scroll: bool,
    pub scroll_offset: u16,
    pub total_lines: u16,
    pub visible_height: u16,
    pub spinner_tick: usize,
}
```

**UiMessage:**

```rust
pub struct UiMessage {
    pub kind: MessageKind,
    pub content: String,
    pub timestamp: chrono::DateTime<chrono::Utc>,
}
```

**MessageKind variants:**

| Variant | Display |
|---------|---------|
| `User` | Right-aligned with primary color |
| `Assistant` | Markdown-rendered with syntax highlighting |
| `ToolCall` | Tool name + arguments with dimmed style |
| `ToolResult` | Tool output with left border |
| `Thinking` | Italic dimmed text |
| `Error` | Red text with error prefix |
| `System` | Dimmed informational text |
| `SubAgent { id, description }` | Collapsible sub-agent block |

**Scroll methods:**

| Method | Behavior |
|--------|----------|
| `scroll_up(n)` | Decrements offset, disables auto_scroll |
| `scroll_down(n)` | Increments offset, re-enables auto_scroll if at bottom |
| `scroll_to_top()` | Sets offset to 0 |
| `scroll_to_bottom()` | Re-enables auto_scroll |

The `to_lines()` method on `UiMessage` handles all rendering:
- **Assistant messages**: parsed as Markdown via `pulldown-cmark`, code blocks get `syntect` highlighting, text is word-wrapped to terminal width
- **Tool calls**: formatted with spinner animation during execution
- **Sub-agent messages**: collapsible with click-to-expand pattern

### InputState

**File:** `src/state/input.rs`

Multi-line text editor with history and paste handling:

```rust
pub struct InputState {
    pub lines: Vec<String>,
    pub cursor_row: usize,
    pub cursor_col: usize,
    pub history: Vec<Vec<String>>,
    pub history_index: Option<usize>,
    pub autocomplete: AutocompleteState,
}
```

**Key features:**

- **Multi-line editing**: Enter inserts newline, Ctrl+Enter (or similar) submits
- **Cursor navigation**: Left/Right/Up/Down/Home/End with line wrapping
- **History**: Up/Down in single-line mode recalls previous inputs
- **Paste collapsing**: Pastes exceeding 5 lines or 500 characters are collapsed into a placeholder showing line count; expanded on toggle

**Key methods:**

| Method | Description |
|--------|-------------|
| `insert_char(c)` | Insert at cursor position |
| `delete_backward()` | Backspace with line joining |
| `delete_forward()` | Delete with line joining |
| `move_left/right/up/down()` | Cursor movement |
| `move_home/end()` | Line start/end |
| `handle_paste(text)` | Inserts text, collapses if large |
| `expand_pastes()` | Expand all collapsed pastes inline |
| `toggle_paste_expansion(idx)` | Toggle specific paste |
| `submit()` | Returns content, resets state, saves to history |

### SessionState

**File:** `src/state/session.rs`

Wraps `SessionManager` (from `ava-session` crate) for SQLite persistence:

| Method | Description |
|--------|-------------|
| `create_session()` | Creates new session in DB |
| `switch_to(id)` | Loads session by UUID, restores messages |
| `fork_current()` | Creates a copy of current session |
| `list_recent(limit)` | Returns recent sessions ordered by update time |
| `search(query)` | FTS5 full-text search across session content |
| `save_session(messages, metadata)` | Persists current session state |

### PermissionState

**File:** `src/state/permission.rs`

Manages tool approval workflow:

```rust
pub struct PermissionState {
    pub level: PermissionLevel,
    pub approval_queue: VecDeque<ApprovalRequest>,
    pub stage: ApprovalStage,
}
```

- **PermissionLevel**: `Standard` (ask for each tool) or `AutoApprove` (YOLO mode, set via `--auto-approve`)
- **ApprovalStage**: `Preview` -> `ActionSelect` -> `RejectionReason`
- **ApprovalRequest**: contains `ToolCall` details + `oneshot::Sender<bool>` to respond

### KeybindState

**File:** `src/state/keybinds.rs`

Configurable keybindings with 18 action types:

```rust
pub enum Action {
    CommandPalette, NewSession, ModelSelector, ScrollUp, ScrollDown,
    ScrollTop, ScrollBottom, ToggleSidebar, CycleThinking, Cancel,
    SessionList, VoiceInput, CopyLastResponse, SwitchTheme,
    ToggleAutoApprove, ViewAgents, ViewTools, Help,
}
```

**Default keybindings:**

| Key | Action |
|-----|--------|
| `Ctrl+/`, `Ctrl+K` | Command Palette |
| `Ctrl+N` | New Session |
| `Ctrl+M` | Model Selector |
| `PageUp` | Scroll Up |
| `PageDown` | Scroll Down |
| `Home` | Scroll to Top |
| `End` | Scroll to Bottom |
| `Ctrl+S` | Toggle Sidebar |
| `Ctrl+T` | Cycle Thinking |
| `Ctrl+C` | Cancel / Quit |
| `Ctrl+L` | Session List |
| `Ctrl+V` | Voice Input |
| `Ctrl+Y` | Copy Last Response |

Custom overrides loaded from `~/.ava/keybindings.json`.

### Theme

**File:** `src/state/theme.rs`

33 color fields organized into categories:

```rust
pub struct Theme {
    // Core palette
    pub primary: Color,
    pub accent: Color,
    pub success: Color,
    pub warning: Color,
    pub error: Color,
    // Text hierarchy
    pub text: Color,
    pub text_secondary: Color,
    pub text_dimmed: Color,
    pub text_inverse: Color,
    // Surfaces
    pub background: Color,
    pub surface: Color,
    pub surface_highlight: Color,
    // Borders
    pub border: Color,
    pub border_focused: Color,
    // Diff colors
    pub diff_add: Color,
    pub diff_remove: Color,
    pub diff_context: Color,
    // Risk levels
    pub risk_low: Color,
    pub risk_medium: Color,
    pub risk_high: Color,
    pub risk_critical: Color,
    // ... additional fields
}
```

**28 built-in themes:**

Dark themes (24): `default`, `dracula`, `nord`, `gruvbox`, `catppuccin`, `solarized_dark`, `tokyo_night`, `one_dark`, `rose_pine`, `kanagawa`, `monokai`, `material`, `ayu_dark`, `ayu_mirage`, `everforest`, `nightfox`, `github_dark`, `moonlight`, `synthwave`, `palenight`, `onedark_vivid`, `horizon`, `poimandres`, `vesper`

Light themes (5): `github_light`, `solarized_light`, `catppuccin_latte`, `one_light`, `rose_pine_dawn`

**Custom themes:** Place `.toml` files in `~/.ava/themes/`. All fields are optional (missing fields fall back to default). Uses serde TOML deserialization.

### VoiceState

**File:** `src/state/voice.rs`

```rust
pub struct VoiceState {
    pub phase: VoicePhase,
    pub recording_start: Option<Instant>,
    pub amplitude: f32,
    pub error: Option<String>,
    pub auto_submit: bool,
    pub continuous: bool,
}

pub enum VoicePhase {
    Idle,
    Recording,
    Transcribing,
}
```

Feature-gated behind `feature = "voice"`. Tracks recording duration, amplitude levels for visual feedback, and supports continuous dictation mode.

---

## Widgets

### SelectListState\<T\>

**File:** `src/widgets/select_list.rs`

The shared reusable list widget used by 5+ modals. Generic over item value type `T`.

```rust
pub struct SelectListState<T> {
    items: Vec<SelectItem<T>>,
    query: String,
    selected: usize,
    scroll_offset: usize,
    line_map: Vec<usize>,       // maps visual line -> item index
    filtered_cache: Vec<usize>, // indices of items matching query
}

pub struct SelectItem<T> {
    pub title: String,
    pub detail: String,
    pub section: Option<String>,
    pub status: Option<ItemStatus>,
    pub value: T,
    pub enabled: bool,
}

pub enum ItemStatus {
    Connected,
    Active,
    Info(String),
}
```

**Key features:**

- **Nucleo fuzzy search**: Filters items by query with fuzzy matching via the `nucleo` crate
- **Section headers**: Items grouped by `section` field with blank separators between groups
- **line_map**: Maps visual line positions (including section headers and separators) to item indices for correct scroll behavior
- **ensure_visible**: Scrolls to keep selected item visible, accounting for section headers
- **Disabled items**: Rendered but not selectable

**handle_select_list_key** returns `SelectListAction`:
- `Moved` — selection changed
- `Selected(T)` — Enter pressed, returns item value
- `Cancelled` — Escape pressed
- `Filtered` — query changed
- `Ignored` — key not handled

**render_select_list**: Renders header bar with title, search input, categorized items with highlight, and optional sticky footer.

**Used by:** CommandPalette, SessionList, ModelSelector, ToolList, AgentList, ThemeSelector

### MessageList

**File:** `src/widgets/message_list.rs`

`render_message_list(frame, area, state)`:

1. Selects message source based on `ViewMode` (main messages or sub-agent messages)
2. Empty state: renders welcome screen (main view) or "no messages" hint (sub-agent view)
3. Builds all visual lines with 1 blank line between messages
4. Adds top/bottom padding (1 blank line each)
5. In `SubAgent` view mode: renders breadcrumb header ("<- Main > Sub-agent: description") with separator line
6. Manages auto-scroll: keeps bottom visible when `auto_scroll` is true
7. Clamps scroll offset to valid range
8. Renders scrollbar (vertical right) when content overflows and not at bottom

### Composer

**File:** `src/widgets/composer.rs`

`render_composer(frame, area, state)`:

- Left accent bar (2px) in primary color
- Prompt character (`>`) at cursor line
- Multi-line text editing area with block cursor
- Paste placeholders styled differently (dimmed, with expansion hint)
- Bottom info line showing: model name, agent mode badge (CODE/PLAN), thinking level badge

### CommandPalette

**File:** `src/widgets/command_palette.rs`

```rust
pub struct CommandPaletteState {
    pub list: SelectListState<CommandExec>,
}

pub enum CommandExec {
    Action(Action),
    Slash(String),
}
```

18 default commands across 8 categories:

| Category | Commands |
|----------|----------|
| Agent | New Session, Switch Model, Cycle Thinking, Toggle Auto-approve |
| Session | Session List, Copy Last Response |
| Chat | Clear Chat |
| Provider | Connect Provider |
| Tools | View Tools |
| MCP | Reload MCP |
| UI | Toggle Sidebar, Switch Theme |
| App | Help, Quit |

Plus all slash commands registered as `CommandExec::Slash`.

### ModelSelector

**File:** `src/widgets/model_selector.rs`

```rust
pub struct ModelSelectorState {
    pub list: SelectListState<ModelValue>,
}

pub struct ModelValue {
    pub display: String,
    pub provider: String,
    pub model: String,
}
```

Builds items from `ModelCatalog` + `CredentialStore`. Provider sections ordered: Recent, Copilot, Anthropic, OpenAI, OpenRouter, Gemini, Alibaba, ZAI, Kimi, MiniMax, Ollama.

Shows model names with pricing info in detail field. Only providers with configured credentials are shown.

### SessionList

**File:** `src/widgets/session_list.rs`

```rust
pub struct SessionListState {
    pub open: bool,
    pub list: SelectListState<Uuid>,
}
```

`update_sessions(sessions)` builds items:
- First item: "+ New Session" (value: `Uuid::nil()`)
- Session items: title (from metadata or first user message), detail with message count and relative date

**Relative date formatting**: "just now", "Xm ago", "Xh ago", "Xd ago", "Xw ago", or "YYYY-MM-DD" for older.

### ToolApproval

**File:** `src/widgets/tool_approval.rs`

Renders the permission approval modal with sections:

1. **TOOL** section — tool name
2. **COMMAND** section — code box with the command/arguments
3. **RISK** section — risk level badge (color-coded) + warning messages
4. **Button row** — action keys displayed at bottom

Three approval stages:
- `Preview` — shows tool details, press any key to continue
- `ActionSelect` — `a` approve, `s` approve for session, `r` reject, `y` YOLO (auto-approve all)
- `RejectionReason` — free-text input for rejection reason

### ToolList

**File:** `src/widgets/tool_list.rs`

```rust
pub struct ToolListState {
    pub list: SelectListState<String>,
}
```

Groups tools by source:
- **Core** — built-in tools (19)
- **MCP:{server}** — tools from MCP servers
- **Custom** — TOML-defined custom tools

### ProviderConnect

**File:** `src/widgets/provider_connect.rs`

```rust
pub struct ProviderConnectState {
    pub list: SelectListState<String>,
    pub screen: ConnectScreen,
    // auth flow state fields
}

pub enum ConnectScreen {
    List,            // Provider list with connection status
    AuthMethodChoice, // Choose: OAuth, Device Code, or API Key
    Configure,        // API key entry form
    OAuthBrowser,     // "Open browser" prompt for PKCE flow
    DeviceCode,       // Shows device code + verification URL
}
```

Supports three auth methods per provider:
- **PKCE OAuth** — opens browser for authorization
- **Device Code** — displays code + URL for out-of-band auth
- **API Key** — direct key entry

Shows provider connection status (`Connected` badge) in the list view.

### Welcome

**File:** `src/widgets/welcome.rs`

Rendered when message list is empty (main view only):

- ASCII art AVA logo (centered)
- Subtitle text
- Current model and working directory info
- Keyboard shortcuts grid (2-column layout)
- Vertically centered with 40/60 bias (40% above, 60% below)

### SlashMenu

**File:** `src/widgets/slash_menu.rs`

Inline autocomplete menu rendered **above the composer** (not a modal). Shows matching slash commands as the user types `/`.

- Max 10 visible items with scroll
- Highlighted selected item
- Renders upward from composer position

### Autocomplete

**File:** `src/widgets/autocomplete.rs`

```rust
pub enum AutocompleteTrigger {
    Slash,      // / commands
    AtMention,  // @ mentions
}

pub struct AutocompleteState {
    pub trigger: Option<AutocompleteTrigger>,
    pub items: Vec<String>,
    pub selected: usize,
    pub query: String,
}
```

Case-insensitive filtering. Manages the state for the slash menu widget.

### TokenBuffer

**File:** `src/widgets/token_buffer.rs`

Batches streaming tokens for 60fps rendering:

```rust
pub struct TokenBuffer {
    buffer: String,
    last_flush: Instant,
    frame_interval: Duration, // 16ms = ~60fps
}
```

| Method | Description |
|--------|-------------|
| `push(token)` | Appends token to buffer |
| `should_flush()` | Returns true if frame_interval elapsed |
| `flush()` | Returns accumulated text, resets buffer |
| `force_flush()` | Flushes regardless of timing (used at stream end) |

### Message

**File:** `src/widgets/message.rs`

Thin wrapper: `render_message()` delegates to `UiMessage::to_lines()` for all rendering logic. The actual rendering (Markdown parsing, syntax highlighting, word wrapping, spinner animation) is implemented in `state/messages.rs`.

---

## UI Layout

**File:** `src/ui/layout.rs`

```rust
pub struct MainLayout {
    pub top_bar: Rect,
    pub messages: Rect,
    pub messages_full: Rect,  // messages area without sidebar
    pub composer: Rect,
    pub context_bar: Rect,
    pub sidebar: Rect,
}
```

**Layout algorithm:**

1. **Top bar**: 1 row, pinned to top
2. **Context bar**: 1 row, pinned to bottom
3. **Composer**: dynamic height based on content lines, capped at 33% of terminal height
4. **Messages**: remaining space between top bar and composer
5. **Sidebar**: 36 chars wide on the right, only shown when `show_sidebar == true` and terminal width > 120

**Content margin**: responsive based on terminal width:
- Width > 160: 3 chars per side
- Width > 100: 2 chars per side
- Otherwise: 1 char per side

**Composer height**: calculated from input content lines + 2 (for borders/info line), minimum 3, maximum 33% of terminal height.

---

## UI Components

### Top Bar / Status Bar

**File:** `src/ui/status_bar.rs`

`render_top(frame, area, state)` renders a single-row bar containing:

- **Left**: AVA logo + session ID (truncated)
- **Center**: Status message (with TTL-based auto-dismiss: Info 3s, Warn 4s, Error 5s)
- **Right**: Voice recording indicator (when active), auto-approve warning badge

```rust
pub struct StatusMessage {
    pub text: String,
    pub level: StatusLevel,  // Info, Warn, Error
    pub created: Instant,
}
```

### Context Bar

**File:** `src/ui/status_bar.rs`

`render_context_bar(frame, area, state)` renders a single-row bar at the bottom:

**Left side** (contextual):
- If modal is open: modal-specific hint text (e.g., "Type to search, Enter to select")
- If agent is working: spinner animation + activity description
- If idle: hint text ("Type a message or / for commands")

**Right side** (always visible):
- Token counts: `{input_tokens}/{output_tokens}`
- Cost: `${total_cost_usd:.4f}`
- Model badge: provider/model name
- Thinking badge: level indicator (if not Off)

### Sidebar

**File:** `src/ui/sidebar.rs`

Rendered on the right (36 chars wide) when `show_sidebar == true` and terminal width > 120.

Sections displayed:

1. **Session** — current session UUID (truncated)
2. **Provider / Model** — current provider and model names
3. **Tokens** — input/output token counts
4. **Agent** — current turn number and activity
5. **Sub-agents** — list of spawned sub-agents (max 5 visible, with "+N more" overflow)
6. **Todos** — todo items from agent context
7. **Shortcuts** — key binding reference

In sub-agent view mode, shows "Esc back" hint for navigation.

---

## Event System

**File:** `src/event.rs`

```rust
pub enum AppEvent {
    Key(KeyEvent),
    Paste(String),
    Resize(u16, u16),
    Mouse(MouseEvent),
    Tick,
    Agent(AgentEvent),
    AgentDone(Result<(), String>),
    TokenUsage(TokenUsage),
    ShellResult(String),
    // Voice (feature-gated)
    VoiceReady(String),
    VoiceError(String),
    VoiceAmplitude(f32),
    VoiceSilenceDetected,
    // OAuth
    OAuthSuccess { provider: String },
    OAuthError { provider: String, error: String },
    // Agent questions
    Question(QuestionRequest),
    Quit,
}
```

**spawn_event_reader**: Spawns a Crossterm event reader task. Filters mouse events to only forward `ScrollUp` and `ScrollDown` (ignores clicks, moves, drags).

**spawn_tick_timer**: Spawns a tick timer task. Rate adapts:
- **16ms** when `is_streaming` is true (60fps for smooth token rendering)
- **250ms** when idle (low CPU usage)

---

## Event Handler

**File:** `src/app/event_handler.rs`

### handle_agent_event

Processes events from the agent runtime:

| Event | Handler Behavior |
|-------|-----------------|
| `Token(text)` | Pushes to `TokenBuffer` |
| `ToolCall{id, name, args}` | Flushes token buffer, adds ToolCall message, tracks sub-agent spawns |
| `ToolResult{id, output}` | Matches sub-agent results, adds ToolResult message |
| `Progress(text)` | Parses turn number from text |
| `Complete(text)` | Flushes buffer, adds final assistant message, sets Idle |
| `SubAgentComplete{id, tokens, messages}` | Accumulates sub-agent tokens, stores session messages |
| `Error(text)` | Flushes buffer, adds error message |

### submit_goal

Handles user input submission:

1. **Shell commands** (`!` prefix): Runs via bash, displays output as ShellResult
2. **Slash commands** (`/` prefix): Routes to `execute_slash_command()`
3. **Regular input**: Builds conversation history from UI messages, calls `agent.start(goal, history, event_tx)`

**Conversation history building**: Collects `User` and `Assistant` messages from `state.messages` into a `Vec<Message>` passed to the agent loop, providing multi-turn conversation context.

---

## Slash Commands

**File:** `src/app/commands.rs`

| Command | Description |
|---------|-------------|
| `/model [name]` | Switch model (opens selector if no argument) |
| `/tools` | Open tool list modal |
| `/mcp` | Reload MCP servers |
| `/connect` | Open provider connect modal |
| `/providers` | List configured providers |
| `/disconnect [provider]` | Remove provider credentials |
| `/status` | Show agent status (model, tokens, cost) |
| `/diff` | Show git diff of working directory |
| `/clear` | Clear message history |
| `/compact` | Trigger context condensation |
| `/think [level]` | Set thinking level (off/low/medium/high) |
| `/agents` | Open sub-agent list |
| `/sessions` | Open session list |
| `/permissions` | Toggle auto-approve mode |
| `/theme [name]` | Switch theme (opens selector if no argument) |
| `/commit` | Stage and commit changes |
| `/copy` | Copy last assistant response to clipboard |
| `/help` | Show help information |

`execute_command_action(action, state)` handles `Action` dispatch from both slash commands and keybindings.

---

## ViewMode

**File:** `src/app/mod.rs`

```rust
pub enum ViewMode {
    Main,
    SubAgent {
        agent_index: usize,
        description: String,
    },
}
```

- **Main**: Shows the primary conversation. Message source is `state.messages.messages`.
- **SubAgent**: Shows a sub-agent's conversation. Message source is `state.agent.sub_agents[agent_index].session_messages`.

Switching to sub-agent view:
- From the agent list modal, selecting a sub-agent sets `ViewMode::SubAgent`
- The message list renders a breadcrumb header: `"<- Main > Sub-agent: {description}"`
- Pressing `Esc` returns to `ViewMode::Main`

---

## Modal System

**File:** `src/app/mod.rs` (ModalType enum), `src/app/modals.rs` (handlers), `src/ui/mod.rs` (rendering)

```rust
pub enum ModalType {
    CommandPalette,
    SessionList,
    ModelSelector,
    ToolList,
    ProviderConnect,
    ThemeSelector,
    AgentList,
    ToolApproval,
    Question,
}
```

**Modal rendering** (`src/ui/mod.rs`):
- `render_modal()` draws a centered rect (60% width, 70% height) with dimmed backdrop
- Each modal type dispatches to its specific render function
- Modals capture all key events (handled before global keybinds)

**Modal key handling** (`src/app/modals.rs`):
- 6 modals use `handle_select_list_key`: CommandPalette, SessionList, ModelSelector, ToolList, AgentList, ThemeSelector
- ProviderConnect has its own 5-screen state machine
- ToolApproval has 3-stage approval flow
- Question modal supports free-text and options selection

**Theme selector** has live preview: theme changes as you navigate, reverts to previous theme on Esc.

---

## Headless Mode

**File:** `src/headless.rs`

Activated by `--headless` CLI flag. No TUI rendering; runs agent and outputs to stdout/stderr.

`run_headless(args)` dispatches to:

| Mode | Trigger | Description |
|------|---------|-------------|
| `run_single_agent` | Default | Single agent run with goal |
| `run_workflow` | `--workflow` | Workflow pipeline execution |
| `run_multi_agent` | `--multi-agent` | Multi-agent commander mode |
| `run_voice_loop` | `--voice` | Continuous voice input loop |

**Output modes:**

- **JSON mode** (`--json`): Structured JSON events to stdout, one per line. Events include `token`, `tool_call`, `tool_result`, `complete`, `error`, `usage`.
- **Text mode** (default): Tokens printed to stdout (streamable), tool calls and metadata to stderr.

Exit code: 0 on success, 1 on error.

---

## CLI Flags

**File:** `src/config/cli.rs`

```rust
pub struct CliArgs {
    pub goal: Option<String>,         // Positional: goal/prompt text
    pub resume: bool,                 // --resume: continue last session
    pub session: Option<String>,      // --session: resume specific session
    pub model: Option<String>,        // --model: model name/alias
    pub provider: Option<String>,     // --provider: provider name
    pub max_turns: Option<u32>,       // --max-turns: limit agent turns
    pub max_budget_usd: Option<f64>,  // --max-budget: cost limit in USD
    pub auto_approve: bool,           // --auto-approve / --yolo: skip permissions
    pub theme: Option<String>,        // --theme: theme name
    pub headless: bool,               // --headless: no TUI
    pub json: bool,                   // --json: structured output (headless)
    pub multi_agent: bool,            // --multi-agent: commander mode
    pub workflow: Option<String>,     // --workflow: workflow pipeline name
    pub voice: bool,                  // --voice: voice input mode
    pub command: Option<Command>,     // Subcommand (review, auth)
}

pub enum Command {
    Review(ReviewArgs),
    Auth(AuthCommand),
}
```

**Provider/model resolution priority** (`resolve_provider_model()`):
1. CLI flags (`--provider`, `--model`)
2. Environment variables (`AVA_PROVIDER`, `AVA_MODEL`)
3. Project state (`.ava/state.json`)
4. Config file (`~/.ava/config.toml`)

---

## Auth Subcommand

**File:** `src/auth.rs`

`ava auth <command>`:

| Command | Description |
|---------|-------------|
| `login <provider>` | Authenticate with provider (PKCE, Device Code, or API Key) |
| `logout <provider>` | Remove stored credentials |
| `list` | Show all configured providers and auth status |
| `test [provider]` | Test provider connectivity |

---

## Review Subcommand

**File:** `src/review.rs` (referenced from `src/main.rs`)

`ava review [options]`:

- Collects git diff of working directory
- Runs a code review agent against the diff
- Formats output (text, JSON, or Markdown)
- Exit code based on review severity findings

---

## Voice Input

**Files:** `src/audio.rs`, `src/transcribe.rs` (feature-gated: `feature = "voice"`)

**Audio pipeline** (`src/audio.rs`):
- `AudioRecorder` — uses `cpal` for microphone capture
- `SilenceDetector` — RMS-based sliding window for silence detection
- WAV encoding and optional resampling

**Transcription** (`src/transcribe.rs`):
- `Transcriber` trait with two implementations:
  - `WhisperApiClient` — sends audio to OpenAI Whisper API
  - `LocalWhisper` — uses `whisper-rs` for local transcription
- `create_transcriber()` factory selects based on configuration

**Voice flow**: `VoicePhase::Idle` -> `Recording` (Ctrl+V to start) -> silence detected -> `Transcribing` -> result inserted into composer -> `Idle`

---

## Configuration

**File:** `src/config/cli.rs`, `src/config/keybindings.rs`, `src/config/themes.rs`

| Config File | Location | Purpose |
|-------------|----------|---------|
| `~/.ava/config.toml` | Global | Default provider, model, theme |
| `~/.ava/credentials.json` | Global | Provider API keys |
| `~/.ava/keybindings.json` | Global | Custom key binding overrides |
| `~/.ava/themes/*.toml` | Global | Custom theme definitions |
| `.ava/state.json` | Project | Per-project model persistence |
| `.ava/tools/*.toml` | Project | Project-specific custom tools |

**Keybinding override format** (`~/.ava/keybindings.json`):
```json
{
  "ctrl+k": "CommandPalette",
  "ctrl+n": "NewSession"
}
```

Supports: `ctrl+<char>`, `pageup`, `pagedown`, `home`, `end`, and plain characters.
