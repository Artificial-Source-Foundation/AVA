# Sprint 58: Modal System Revamp

## Context

AVA is a Rust-first AI coding agent (~21 crates, Ratatui TUI, Tokio async). See `CLAUDE.md` for conventions.

The TUI has 6 modal types (CommandPalette, SessionList, ToolApproval, ModelSelector, ToolList, ProviderConnect). Each reimplements scrolling, search, keybinds, and rendering from scratch. There's a scroll bug where the provider list doesn't scroll properly after the first page — `ensure_visible()` uses item index as line position but doesn't account for section headers and blank lines in rendered output.

This sprint builds a shared modal framework and redesigns all list-based modals.

## Research Phase

Before writing any code, study how competitors handle modals in their TUIs.

### Research 1: OpenCode's dialog system
Read these files thoroughly:

1. `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/ui/dialog-select.tsx` — The core reusable select dialog:
   - **Scroll**: Uses `ScrollBoxRenderable` with smart scrolling — finds the selected item's actual rendered Y position, then scrolls to keep it visible (lines ~160-184). Key insight: it finds the actual DOM element by ID, not by index.
   - **Search**: `fuzzysort` with weighted scoring (title 2x, category 1x), auto-flattens categories when searching
   - **Focus**: Background color fill on selected item (`backgroundColor: active ? theme.primary : transparent`)
   - **Categories**: Group headers that collapse when filtering
   - **Keybind footer**: Fixed-height bar at bottom with `title + key` pairs
   - **PageUp/PageDown**: Move by 10 items. Home/End jump to first/last.
   - **Vim keys**: Ctrl+P/Ctrl+N for up/down

2. `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/ui/dialog.tsx` — Base modal wrapper:
   - Semi-transparent backdrop (`RGBA(0,0,0,150)`)
   - Stack-based: multiple modals can layer
   - ESC closes top modal
   - Focus restoration on close

3. `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/component/dialog-model.tsx` — Model selector:
   - Sections: Favorites → Recent → Provider categories
   - Filled bullet `●` for current model
   - Cost display, disabled models grayed out
   - Keybinds: favorite toggle, connect provider

4. `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/component/dialog-provider.tsx` — Provider dialog:
   - Chained dialogs (list → auth method → auth flow)
   - `dialog.replace()` for sequential steps

### Research 2: Current AVA modal code
Read these files to understand what needs to change:
- `crates/ava-tui/src/ui/mod.rs` — `render_modal()`, `centered_rect()`
- `crates/ava-tui/src/app/modals.rs` — All key handlers
- `crates/ava-tui/src/widgets/provider_connect.rs` — Provider connect render + state
- `crates/ava-tui/src/widgets/model_selector.rs` — Model selector state (has scroll_offset)
- `crates/ava-tui/src/widgets/tool_list.rs` — Tool list render
- `crates/ava-tui/src/widgets/command_palette.rs` — Command palette render
- `crates/ava-tui/src/state/theme.rs` — Theme colors

### Research 3: Ratatui scrollable list patterns
Read Ratatui documentation or examples for:
- `ratatui::widgets::List` with `ListState` — built-in scroll support
- `ratatui::widgets::Scrollbar` — visual scroll indicator
- `Paragraph::scroll()` — manual scroll offset

**Document your design decisions before implementing. Invoke the Code Reviewer sub-agent to verify the design aligns with competitor patterns and project conventions.**

## Task 1: Create shared SelectList widget

**New file**: `crates/ava-tui/src/widgets/select_list.rs`

Build a reusable scrollable list widget that handles the common pattern shared by 5 of the 6 modals.

### SelectListState

```rust
/// Reusable state for scrollable, searchable, categorized list modals.
pub struct SelectListState<T: Clone> {
    /// All items (unfiltered).
    pub items: Vec<SelectItem<T>>,
    /// Current search query.
    pub query: String,
    /// Selected item index (within filtered results).
    pub selected: usize,
    /// Scroll offset in rendered lines (NOT item index).
    pub scroll_offset: usize,
    /// Cached: number of rendered lines per item (1 + section headers above it).
    line_map: Vec<usize>,
}

pub struct SelectItem<T: Clone> {
    /// Display text (left side).
    pub title: String,
    /// Secondary text (right-aligned, dimmed).
    pub detail: String,
    /// Category/section header this item belongs to.
    pub section: Option<String>,
    /// Status indicator (right side, colored).
    pub status: Option<ItemStatus>,
    /// User data payload.
    pub value: T,
    /// Whether this item is selectable.
    pub enabled: bool,
}

pub enum ItemStatus {
    /// Green checkmark + text (e.g., "connected")
    Connected(String),
    /// Bullet indicator (e.g., current model)
    Active,
    /// Dimmed text (e.g., "free")
    Info(String),
}
```

### Key methods

```rust
impl<T: Clone> SelectListState<T> {
    /// Filter items using nucleo fuzzy matching on title.
    pub fn filtered(&self) -> Vec<&SelectItem<T>> { ... }

    /// Move selection down by N. Wraps at boundaries.
    pub fn move_down(&mut self, n: usize) { ... }

    /// Move selection up by N. Wraps at boundaries.
    pub fn move_up(&mut self, n: usize) { ... }

    /// Jump to first/last item.
    pub fn move_to_start(&mut self) { ... }
    pub fn move_to_end(&mut self) { ... }

    /// Ensure selected item is visible. Uses line_map to compute
    /// the actual rendered line position (accounting for section headers
    /// and blank lines between groups).
    pub fn ensure_visible(&mut self, viewport_height: usize) { ... }

    /// Rebuild line_map from current filtered items + sections.
    /// Called after filter changes.
    fn rebuild_line_map(&mut self) { ... }

    /// Get the selected item's value, if any.
    pub fn selected_value(&self) -> Option<&T> { ... }

    /// Type a character into the search query.
    pub fn type_char(&mut self, ch: char) { ... }

    /// Delete last character from search query.
    pub fn backspace(&mut self) { ... }
}
```

### The scroll fix

The critical fix is `rebuild_line_map()` — it precomputes the actual rendered line for each item:

```rust
fn rebuild_line_map(&mut self) {
    let filtered = self.filtered_indices();
    let show_sections = self.query.is_empty();
    let mut line = 0; // Current rendered line (after header)
    let mut last_section: Option<&str> = None;
    let mut map = Vec::with_capacity(filtered.len());

    for &idx in &filtered {
        let item = &self.items[idx];
        if show_sections {
            if let Some(ref section) = item.section {
                if last_section != Some(section) {
                    if last_section.is_some() {
                        line += 1; // Blank line between sections
                    }
                    line += 1; // Section header line
                    last_section = Some(section);
                }
            }
        }
        map.push(line);
        line += 1; // The item itself
    }
    self.line_map = map;
}
```

Then `ensure_visible` uses `self.line_map[self.selected]` instead of `self.selected`:

```rust
pub fn ensure_visible(&mut self, viewport_height: usize) {
    if self.line_map.is_empty() || self.selected >= self.line_map.len() {
        return;
    }
    let line = self.line_map[self.selected];
    if line < self.scroll_offset {
        self.scroll_offset = line;
    } else if line >= self.scroll_offset + viewport_height {
        self.scroll_offset = line.saturating_sub(viewport_height) + 1;
    }
}
```

### Render function

```rust
/// Render a select list modal with title, search, categorized items, and keybind footer.
pub fn render_select_list<T: Clone>(
    frame: &mut Frame<'_>,
    area: Rect,
    state: &SelectListState<T>,
    config: &SelectListConfig,
    theme: &Theme,
) { ... }

pub struct SelectListConfig {
    pub title: String,
    pub search_placeholder: String,
    pub keybinds: Vec<KeybindHint>,
    /// Whether to show a scrollbar when content overflows.
    pub show_scrollbar: bool,
}

pub struct KeybindHint {
    pub key: String,   // e.g., "enter", "d", "esc"
    pub label: String, // e.g., "connect", "disconnect", "close"
}
```

Visual design (matching OpenCode patterns):
- **Selected item**: Full-width background highlight using `theme.primary` with contrasting text
- **Current/active item**: Filled bullet `●` prefix in accent color
- **Section headers**: Accent color, bold, with slight indent
- **Search bar**: Input field with dimmed placeholder, cursor block
- **Keybind footer**: Fixed at bottom, `key` in muted + `label` in dimmed, separated by gaps
- **Scrollbar**: Thin scrollbar on right edge when content overflows (use `ratatui::widgets::Scrollbar`)
- **Status text**: Right-aligned with appropriate color per ItemStatus variant

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 2: Shared key handler

**New file**: `crates/ava-tui/src/widgets/select_list.rs` (add to same file, or `select_list/keys.rs`)

Add a shared key handler that list modals can delegate to:

```rust
/// Handle common list navigation keys. Returns true if key was consumed.
pub fn handle_select_list_key<T: Clone>(
    state: &mut SelectListState<T>,
    key: KeyEvent,
    viewport_height: usize,
) -> SelectListAction {
    match key.code {
        KeyCode::Down => { state.move_down(1); SelectListAction::Moved }
        KeyCode::Up => { state.move_up(1); SelectListAction::Moved }
        KeyCode::PageDown => { state.move_down(10); SelectListAction::Moved }
        KeyCode::PageUp => { state.move_up(10); SelectListAction::Moved }
        KeyCode::Home => { state.move_to_start(); SelectListAction::Moved }
        KeyCode::End => { state.move_to_end(); SelectListAction::Moved }
        KeyCode::Enter => SelectListAction::Selected,
        KeyCode::Esc => SelectListAction::Cancelled,
        KeyCode::Char(ch) => { state.type_char(ch); SelectListAction::Filtered }
        KeyCode::Backspace => { state.backspace(); SelectListAction::Filtered }
        _ => SelectListAction::Ignored,
    }
}

pub enum SelectListAction {
    Moved,
    Selected,
    Cancelled,
    Filtered,
    Ignored,
}
```

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 3: Rebuild ProviderConnect modal on SelectList

**Files**: `crates/ava-tui/src/widgets/provider_connect.rs`, `crates/ava-tui/src/app/modals.rs`

Refactor `ProviderConnectState` to use `SelectListState<String>` (where `String` is the provider ID) for the list screen:

```rust
pub struct ProviderConnectState {
    pub screen: ConnectScreen,
    pub list: SelectListState<String>,  // Provider ID as value
    // ... configure screen fields stay the same
    pub key_input: String,
    pub base_url_input: String,
    pub active_field: ConnectField,
    pub message: Option<String>,
}
```

Build items:
```rust
fn build_select_items(credentials: &CredentialStore) -> Vec<SelectItem<String>> {
    ava_auth::all_providers().iter().map(|info| {
        SelectItem {
            title: info.name.to_string(),
            detail: info.description.to_string(),
            section: Some(match info.group {
                ProviderGroup::Popular => "Popular".to_string(),
                ProviderGroup::Other => "Other".to_string(),
            }),
            status: if configured { Some(ItemStatus::Connected(redacted_key)) } else { None },
            value: info.id.to_string(),
            enabled: true,
        }
    }).collect()
}
```

Update the key handler to delegate list navigation to `handle_select_list_key`, keeping only custom keys (d=disconnect, t=test) in the provider-specific handler.

Update the render to call `render_select_list()` for the List screen.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 4: Rebuild ModelSelector on SelectList

**Files**: `crates/ava-tui/src/widgets/model_selector.rs`, `crates/ava-tui/src/ui/mod.rs`, `crates/ava-tui/src/app/modals.rs`

Refactor `ModelSelectorState` to use `SelectListState<ModelValue>`:

```rust
pub struct ModelValue {
    pub provider: String,
    pub model: String,
}
```

Map the existing model list into `SelectItem<ModelValue>` with sections (Recent, Anthropic, OpenAI, etc.), cost as detail text, and `ItemStatus::Active` for the current model.

Delete the now-redundant `render_model_selector` in `ui/mod.rs` — use `render_select_list` instead.

Delete the redundant scroll/filter logic from `ModelSelectorState` — it's now in `SelectListState`.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 5: Rebuild ToolList and CommandPalette on SelectList

Similarly convert:

1. **ToolList**: Items are `SelectItem<String>` with tool name as title, description as detail, source as section (BuiltIn/MCP/Custom), no status.

2. **CommandPalette**: Items are `SelectItem<CommandExec>` with name as title, hint+category as detail, no sections.

3. **SessionList**: Items are `SelectItem<Uuid>` with session ID as title, timestamp as detail, no sections.

For each, update the widget state to use `SelectListState`, delegate key handling, and use `render_select_list` for rendering.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 6: Visual polish

### 6a: Modal backdrop
In `crates/ava-tui/src/ui/mod.rs`, update `render_modal`:
- Add a dimmed overlay effect: render a full-screen semi-transparent block before the modal (Ratatui doesn't have true transparency, so use a darker bg color)
- Round the modal border corners if Ratatui supports it, or use double-line borders for distinction

### 6b: Scrollbar
Add a `Scrollbar` widget on the right edge of the list when content overflows:
```rust
use ratatui::widgets::{Scrollbar, ScrollbarOrientation, ScrollbarState};
```

### 6c: Selection highlight
Instead of just bold text, render the selected item with a contrasting background:
```rust
// Selected item gets full-width background
let bg = if is_selected { theme.primary } else { Color::Reset };
let fg = if is_selected { theme.bg } else { theme.text };
```

### 6d: Consistent keybind footer
All modals use the same `KeybindHint` format at the bottom, rendered by `render_select_list`.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 7: Tests

1. **SelectListState unit tests**:
   - `move_down` wraps at boundary
   - `move_up` wraps at boundary
   - `ensure_visible` with section headers calculates correct scroll offset
   - `type_char` + `backspace` filters correctly
   - `rebuild_line_map` accounts for section headers and blank lines
   - `move_down(10)` page navigation
   - Empty list handling

2. **Integration**: Verify all 5 refactored modals open, scroll, search, and select correctly.

3. Run: `cargo test --workspace` — all must pass
4. Run: `cargo clippy --workspace` — **ZERO warnings**

## Acceptance Criteria

- [ ] `SelectListState` shared widget handles scroll, search, categories, keybinds
- [ ] Scroll bug fixed — `line_map` accounts for section headers and blank lines
- [ ] ProviderConnect rebuilt on SelectList — scrolls correctly through all 23 providers
- [ ] ModelSelector rebuilt on SelectList — same behavior, less code
- [ ] ToolList, CommandPalette, SessionList rebuilt on SelectList
- [ ] PageUp/PageDown/Home/End supported in all list modals
- [ ] Selected item has full-width background highlight
- [ ] Scrollbar visible when content overflows
- [ ] Consistent keybind footer across all modals
- [ ] All tests pass, clippy clean (0 warnings workspace-wide)

## Final Code Review

After all changes, invoke the Code Reviewer sub-agent for a comprehensive review. Verify:
1. **Scroll correctness**: `line_map` accurately maps item indices to rendered line positions for every modal
2. **No regression**: All existing modal behavior preserved (search, keybinds, auth flows)
3. **Code reduction**: Each modal should be significantly shorter (most logic in SelectListState)
4. **Visual consistency**: All modals use the same rendering patterns
5. **Cross-reference with OpenCode**: Selection highlight, scroll behavior, keybind footer match competitor quality
6. **Clippy**: `cargo clippy --workspace` must show 0 warnings total
