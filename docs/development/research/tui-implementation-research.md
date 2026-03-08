# TUI Implementation Research — OpenCode & Codex CLI

> Research conducted: March 2026  
> Purpose: Inform Sprint 34 (TUI parity) for AVA

---

## OpenCode

### Architecture

**Framework:** Go with [Bubble Tea](https://github.com/charmbracelet/bubbletea) (Charm's TUI framework)

**Repository Structure:**
```
internal/tui/
├── tui.go              # Main app model
├── components/         # Reusable UI components
│   ├── chat/
│   │   ├── chat.go     # Chat view, welcome screen
│   │   └── editor.go   # Multi-line input
│   ├── core/
│   │   └── status.go   # Status bar
│   ├── command.go      # Command palette
│   ├── model-dialog.go # Model selector
│   └── ...
├── layout/             # Layout utilities
├── page/               # Page management
├── styles/             # Styling utilities
├── theme/              # Theme system
└── util/               # Utilities
```

**App Model Pattern:**
```go
type appModel struct {
    pages           map[page.PageID]tea.Model
    currentPage     page.PageID
    statusBar       tea.Model
    showQuit        bool
    showHelp        bool
    showSessionDialog bool
    showCommandDialog bool
    showModelDialog bool
    showThemeDialog bool
    showPermissions bool
    showFilepicker  bool
    showInitDialog  bool
}
```

Key insight: Uses an overlay pattern where dialogs render centered over the main view using `layout.PlaceOverlay()`.

---

### Welcome Screen

**Location:** `internal/tui/components/chat/chat.go`

**Implementation:**
- `header()` function combines logo, repo info, and current working directory
- Uses `lipgloss` for styling with theme-aware colors
- Shows OpenCode icon + version, GitHub URL, and CWD
- LSP configuration section shows configured language servers

**Code Pattern:**
```go
func header() string {
    return lipgloss.JoinVertical(lipgloss.Left,
        logo(),
        repo(),
        cwd(),
    )
}
```

---

### Command Palette

**Location:** `internal/tui/components/command.go`

**Implementation:**
- Triggered by `Ctrl+K`
- Registered via `RegisterCommand()` function
- Commands loaded from:
  - `~/.config/opencode/commands/` (global)
  - `.opencode/commands/` (project-local)
- Multi-argument support with placeholders like `$NAME`
- Fuzzy search through command names and descriptions

**Key Bindings:**
- `Ctrl+K` — Open command palette
- `Enter` — Execute selected command

**Architecture:**
- Commands are JSON/YAML definitions
- Placeholder substitution before execution
- Separate dialog for argument input (`cmdargs/`)

---

### Model Selector

**Location:** `internal/tui/components/model-dialog.go`

**Implementation:**
- Triggered by `Ctrl+O`
- Lists all configured models with their providers
- Runtime model switching
- Shows provider badge and model capabilities

**Key Pattern:**
- Models defined in `~/.config/opencode/config.yaml`
- Supports multi-provider (OpenAI, Anthropic, Gemini, etc.)
- Selection immediately updates active session model

---

### Status Bar

**Location:** `internal/tui/components/core/status.go`

**Data Shown (left to right):**
1. **Help hint** — `Ctrl+? help` (muted background)
2. **Session info** — Token count + cost
   - Formatted with K/M suffix (e.g., "12.5K tokens")
   - Context percentage (warns if >80%)
   - Total cost in USD
3. **Info message** — TTL-based messages (info/warn/error)
4. **Diagnostics** — LSP errors/warnings/hints/info counts with icons
5. **Current model** — Badge showing active model

**Update Mechanism:**
```go
func (m *Model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
    switch msg := msg.(type) {
    case InfoMsg:
        m.setInfo(msg)
        return m, m.clearMessageCmd()
    }
    // ... pubsub events for session/diagnostics
}

func (m *Model) clearMessageCmd() tea.Cmd {
    return tea.Tick(m.ttl, func(time.Time) tea.Msg {
        return clearInfoMsg{}
    })
}
```

---

### Keyboard Hints

**Location:** `internal/tui/tui.go` (keyMap struct)

**Pattern:**
```go
type keyMap struct {
    Logs          key.Binding  // Ctrl+L
    Quit          key.Binding  // Ctrl+C
    Help          key.Binding  // Ctrl+?
    SwitchSession key.Binding  // Ctrl+S
    Commands      key.Binding  // Ctrl+K
    Filepicker    key.Binding  // Ctrl+F
    Models        key.Binding  // Ctrl+O
    SwitchTheme   key.Binding  // Ctrl+T
}
```

**Context-Sensitive Hints:**
- Editor mode: Shows "Ctrl+S to send, Ctrl+E for editor"
- Normal mode: Shows help hint in status bar
- Help overlay: Shows full key binding reference

---

### Input Handling

**Location:** `internal/tui/components/chat/editor.go`

**Multi-line Input:**
- Uses Charm's `bubbles/textarea` component
- `Enter` or `Ctrl+S` sends message
- Backslash at end-of-line adds newline (escape pattern)
- Supports up to 5 image attachments

**External Editor:**
- `Ctrl+E` opens `$EDITOR` (defaults to nvim)
- Creates temp file, launches editor, reads on exit
- Content replaces textarea content

**Attachment Management:**
- `Ctrl+R+R` — Delete all attachments
- `Ctrl+R+{i}` — Delete attachment at index (e.g., `Ctrl+R+1`)
- Shows thumbnails in UI

**Theme Integration:**
```go
func CreateTextArea() textarea.Model {
    t := textarea.New()
    t.FocusedStyle = textarea.Style{
        CursorLine: lipgloss.NewStyle().Foreground(theme.CurrentTheme().Primary),
        // ... other styles
    }
    return t
}
```

---

### Streaming Display

**Location:** Chat widget renders streaming content via LLM client

**Pattern:**
- Messages appended to conversation list
- Typing indicator shown while streaming
- No explicit frame rate control — relies on Bubble Tea's event loop
- Content updates trigger re-render via `tea.Msg` dispatch

---

### Layout

**Window Size Handling:**
```go
func (m appModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
    switch msg := msg.(type) {
    case tea.WindowSizeMsg:
        m.width = msg.Width
        m.height = msg.Height
        // Resize all pages
        for id, page := range m.pages {
            m.pages[id], _ = page.Update(msg)
        }
    }
}
```

**Overlay Layout:**
```go
// Center dialog over main view
return layout.PlaceOverlay(
    m.width/2, m.height/2,
    dialogView,
    mainView,
    lipgloss.WithWhitespaceChars(" "),
)
```

---

### Theming

**Location:** `internal/tui/theme/`

**Pattern:**
- Global `CurrentTheme()` function returns theme struct
- Themes define: Primary, Secondary, Error, Warning, Info, Success colors
- Components import theme and use `theme.CurrentTheme().Primary` etc.
- Dark/light theme switching at runtime via `Ctrl+T`

---

## Codex CLI

### Architecture

**Framework:** Rust with [Ratatui](https://github.com/ratatui/ratatui) (same as AVA)

**Repository Structure:**
```
codex-rs/tui/src/
├── app.rs              # Main application state
├── bottom_pane/        # Approval UI, status
│   ├── mod.rs
│   ├── approval.rs
│   └── status.rs
├── chatwidget/         # Main chat display
│   ├── mod.rs
│   ├── message.rs
│   └── render.rs
├── streaming/          # Streaming animation
│   └── mod.rs
├── status/             # Status bar
├── onboarding/         # Welcome flow
└── main.rs
```

**App State Pattern:**
```rust
pub struct App {
    server: ThreadManager,
    chat_widget: ChatWidget,
    auth_manager: AuthManager,
    config: Arc<AppConfig>,
    overlay: Option<Box<dyn Overlay>>,
    commit_anim_running: AtomicBool,
    thread_event_channels: HashMap<ThreadId, ThreadEventChannel>,
}
```

---

### Streaming Animation

**Location:** `codex-rs/tui/src/streaming/`

**Key Innovation:** `TARGET_FRAME_INTERVAL` for smooth animation

```rust
pub const COMMIT_ANIMATION_TICK: Duration = tui::TARGET_FRAME_INTERVAL;

// Used for smooth streaming token display
// Frame rate controlled to prevent flicker and reduce CPU
```

**Thread Event Store:**
```rust
pub struct ThreadEventStore {
    events: VecDeque<ThreadEvent>,
    capacity: usize,
}

impl ThreadEventStore {
    pub fn push(&mut self, event: ThreadEvent) {
        if self.events.len() >= self.capacity {
            self.events.pop_front();
        }
        self.events.push_back(event);
    }
}
```

**Commit Animation:**
- Runs at target frame interval (typically 60fps)
- AtomicBool `commit_anim_running` controls animation state
- Batches token updates to reduce re-renders

---

### Input Handling

**Location:** `app.rs` event handling

**Pattern:**
```rust
pub enum AppEvent {
    UserMessage(String),
    ThreadEvent(ThreadId, ThreadEvent),
    ApprovalGranted(ApprovalId),
    ApprovalDenied(ApprovalId),
    // ...
}

// Event loop
while let Some(event) = rx.recv().await {
    app.handle_event(event).await;
}
```

**Key Bindings:**
- Handled via termion/crossterm key events
- Multi-line input with Shift+Enter for newline
- Command prefix with `/` (e.g., `/help`)

---

### Tool Approval UI

**Location:** `codex-rs/tui/src/bottom_pane/approval.rs`

**Pattern:**
```rust
pub enum ApprovalRequest {
    Exec(ExecApprovalRequest),
    ApplyPatch(ApplyPatchApprovalRequest),
}

pub struct ExecApprovalRequest {
    pub id: ApprovalId,
    pub command: String,
    pub description: String,
}
```

**UI Flow:**
1. Tool requires approval → `ApprovalRequest` sent
2. Bottom pane switches to approval view
3. Shows command/patch with syntax highlighting
4. User presses `y` (yes) or `n` (no)
5. `ApprovalGranted`/`ApprovalDenied` event dispatched

---

### Status Display

**Location:** `codex-rs/tui/src/bottom_pane/status.rs`

**Data Shown:**
- Token usage (current session)
- Cost accumulator
- Agent status (idle/running/waiting for approval)
- Network status
- Mode indicator (full-auto vs ask)

**Multi-Agent Support:**
```rust
struct AgentPicker {
    threads: Vec<(ThreadId, AgentInfo)>,
    selected: usize,
}

struct AgentInfo {
    nickname: String,
    role: String,
}
```

---

### Error Display

**Pattern:**
- Errors shown inline in chat as red-bordered message
- Uses `ratatui::widgets::Paragraph` with error styling
- Non-blocking — app continues running
- Critical errors show in modal overlay

---

## Patterns to Steal for AVA

### 1. Overlay Dialog System
**What:** Centered modal dialogs over main view  
**From:** OpenCode  
**How to implement in Ratatui:**
- Use `Paragraph` or `Block` for dialog container
- Calculate center position: `(area.width - dialog_width) / 2`
- Render dialog after main view with `frame.render_widget(dialog, centered_rect)`
- Handle `Esc` to close overlay

**Priority:** HIGH — needed for command palette, model selector

---

### 2. TTL-Based Status Messages
**What:** Info/warn/error messages auto-clear after timeout  
**From:** OpenCode  
**How to implement in Ratatui:**
```rust
// In App::update()
if let Some(msg) = self.status_message {
    if msg.expires_at <= Instant::now() {
        self.status_message = None;
    }
}

// In render()
if let Some(msg) = &self.status_message {
    frame.render_widget(
        Paragraph::new(&msg.text)
            .style(Style::default().fg(msg.color)),
        status_area,
    );
}
```

**Priority:** MEDIUM — nice UX polish

---

### 3. TARGET_FRAME_INTERVAL Animation
**What:** Smooth streaming at controlled frame rate  
**From:** Codex CLI  
**How to implement in Ratatui:**
```rust
const TARGET_FRAME_INTERVAL: Duration = Duration::from_millis(16); // ~60fps

// In event loop
let last_frame = Instant::now();
loop {
    let now = Instant::now();
    if now - last_frame >= TARGET_FRAME_INTERVAL {
        terminal.draw(|f| app.render(f))?;
        last_frame = now;
    }
}
```

**Priority:** HIGH — improves streaming UX significantly

---

### 4. Thread Event Store with Circular Buffer
**What:** Ring buffer for events with automatic eviction  
**From:** Codex CLI  
**How to implement in Ratatui:**
```rust
pub struct EventRing<T> {
    events: VecDeque<T>,
    capacity: usize,
}

impl<T> EventRing<T> {
    pub fn push(&mut self, event: T) {
        if self.events.len() >= self.capacity {
            self.events.pop_front();
        }
        self.events.push_back(event);
    }
    
    pub fn iter(&self) -> impl Iterator<Item = &T> {
        self.events.iter()
    }
}
```

**Priority:** MEDIUM — for long-running sessions

---

### 5. Context-Sensitive Key Hints
**What:** Show different hints based on current mode  
**From:** OpenCode  
**How to implement in Ratatui:**
```rust
fn render_help(&self, area: Rect, buf: &mut Buffer) {
    let hints = match self.mode {
        Mode::Normal => "Ctrl+P palette | Ctrl+M model | Enter send",
        Mode::Editor => "Ctrl+S send | Ctrl+E external | Esc cancel",
        Mode::Dialog => "↑↓ navigate | Enter select | Esc close",
    };
    Paragraph::new(hints).render(area, buf);
}
```

**Priority:** HIGH — essential for discoverability

---

### 6. Approval UI Pattern
**What:** Bottom pane for tool/command approval  
**From:** Codex CLI  
**How to implement in Ratatui:**
```rust
// Split layout
let chunks = Layout::default()
    .direction(Direction::Vertical)
    .constraints([Constraint::Min(0), Constraint::Length(5)])
    .split(area);

let main_area = chunks[0];
let approval_area = chunks[1];

// Render approval pane if pending
if let Some(req) = &self.pending_approval {
    self.render_approval(req, approval_area, buf);
}
```

**Priority:** HIGH — required for tool safety

---

### 7. Theme System with Global Access
**What:** Centralized theme with component access  
**From:** OpenCode  
**How to implement in Ratatui:**
```rust
lazy_static! {
    static ref THEME: RwLock<Theme> = RwLock::new(Theme::default());
}

pub fn current_theme() -> Theme {
    THEME.read().unwrap().clone()
}

// In component
let theme = current_theme();
let style = Style::default().fg(theme.primary);
```

**Priority:** MEDIUM — nice to have for customization

---

### 8. Multi-argument Command Dialog
**What:** Dynamic form for command arguments  
**From:** OpenCode  
**How to implement in Ratatui:**
```rust
pub struct CommandArgsDialog {
    command: Command,
    inputs: Vec<InputField>,
    focused: usize,
}

impl CommandArgsDialog {
    fn render(&self, area: Rect, buf: &mut Buffer) {
        for (i, field) in self.inputs.iter().enumerate() {
            let style = if i == self.focused {
                Style::default().fg(Color::Yellow)
            } else {
                Style::default()
            };
            // Render field with style
        }
    }
}
```

**Priority:** LOW — advanced feature

---

## Recommendations for Sprint 34

### Phase 1: Foundation (Week 1)
1. **Implement overlay dialog system** — Required for all modal UI
2. **Add bottom status bar** — Show model, token count, session info
3. **Context-sensitive key hints** — Essential for user discovery

### Phase 2: Core Features (Week 2)
4. **Command palette** — `/command` system with fuzzy search
5. **Model selector overlay** — Runtime model switching
6. **Approval UI** — Bottom pane for tool approval (safety critical)

### Phase 3: Polish (Week 3)
7. **TARGET_FRAME_INTERVAL streaming** — Smooth token animation
8. **TTL status messages** — Auto-clearing notifications
9. **Theme system** — Centralized color configuration

### Phase 4: Advanced (Week 4)
10. **External editor support** — Ctrl+E to open $EDITOR
11. **Event ring buffer** — For long session performance
12. **Multi-argument commands** — Dynamic input forms

---

## Key Dependencies to Consider

### OpenCode Stack:
- `github.com/charmbracelet/bubbletea` — TUI framework
- `github.com/charmbracelet/bubbles` — Common components (textarea, list)
- `github.com/charmbracelet/lipgloss` — Styling
- `github.com/charmbracelet/log` — Logging

### Codex CLI Stack:
- `ratatui` — TUI framework (already using)
- `crossterm` — Cross-platform terminal (already using)
- `tokio` — Async runtime (already using)
- `serde` — Config serialization (already using)

AVA is already aligned with the Codex CLI stack (Rust + Ratatui).

---

## Open Questions for AVA

1. **Multi-agent UI** — Codex shows thread picker; does AVA need this?
2. **LSP integration** — OpenCode shows diagnostics; should AVA?
3. **Image attachments** — OpenCode supports up to 5; does AVA need this?
4. **Config hot-reload** — Both support runtime theme switching; implement?
5. **Pager integration** — Codex has overlay pager for long outputs; needed?

---

*End of research document*
