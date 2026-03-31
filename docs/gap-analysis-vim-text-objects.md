# F18: Vim Text Objects — Gap Analysis

Sprint 61 | 2026-03-31

## Summary

Compare AVA TUI's current input handling with Claude Code's full vim mode implementation. Identify gaps and design recommendations for future implementation.

---

## Claude Code's Vim Mode

### Architecture

Claude Code implements a production-ready vim mode with a 4-mode state machine:

```
VimState (mode + command state)
    ├── INSERT: { mode, insertedText }
    └── NORMAL: { mode, command: CommandState }

CommandState (state machine)
    ├── idle
    ├── count (digits accumulating)
    ├── operator (waiting for motion)
    ├── operatorCount (operator + count digits)
    ├── operatorFind (operator + find type)
    ├── operatorTextObj (operator + scope selection)
    ├── find (find type selected)
    ├── g (g prefix waiting for second key)
    ├── replace (r prefix waiting for char)
    └── indent (> or < waiting for motion)

PersistentState (survives between commands)
    ├── lastChange (for dot-repeat)
    ├── lastFind (for ; and , repeat)
    ├── register (unnamed register)
    └── registerIsLinewise (paste mode flag)
```

**Key files:**
- `src/vim/types.ts` — State machine definitions
- `src/vim/motions.ts` — Motion calculations
- `src/vim/textObjects.ts` — Text object boundary detection
- `src/vim/operators.ts` — Delete/change/yank execution
- `src/vim/transitions.ts` — State transition table
- `src/hooks/useVimInput.ts` — Terminal integration
- `web/lib/input/vim-adapter.ts` — Web textarea integration (simplified)

### Supported Features

#### Modes
- INSERT — normal text insertion (default on startup)
- NORMAL — command processing and navigation
- VISUAL — selection mode
- COMMAND — ex-style command entry (`:q`, `:w` stubs)

#### Operators
| Operator | Key | Behavior |
|----------|-----|----------|
| delete | `d` | Remove text, store in register |
| change | `c` | Remove text, enter INSERT |
| yank | `y` | Copy to register |

Double-operator linewise: `dd`, `cc`, `yy`

#### Motions
| Category | Keys |
|----------|------|
| Character | `h`, `l`, `j`, `k` |
| Display line | `gj`, `gk` |
| Word | `w`, `W`, `b`, `B`, `e`, `E` |
| Line position | `0`, `^`, `$` |
| Document | `gg`, `G` |
| Find char | `f`, `F`, `t`, `T`, `;`, `,` |

All motions support count prefixes (e.g., `5j`, `3w`, `10G`).

#### Text Objects (inner `i` + around `a`)
| Object | Keys |
|--------|------|
| Word | `iw`, `aw`, `iW`, `aW` |
| Double quotes | `i"`, `a"` |
| Single quotes | `i'`, `a'` |
| Backticks | `` i` ``, `` a` `` |
| Parentheses | `i(`, `a(`, `ib`, `ab` |
| Square brackets | `i[`, `a[` |
| Braces | `i{`, `a{`, `iB`, `aB` |
| Angle brackets | `i<`, `a<` |

#### Additional Commands
| Category | Commands |
|----------|----------|
| Character ops | `x`, `X`, `r<char>`, `~` |
| Paste | `p`, `P` (with count) |
| Line ops | `o`, `O`, `J`, `>>`, `<<` |
| Insert modes | `i`, `I`, `a`, `A`, `s` |
| Aliases | `D` (`d$`), `C` (`c$`), `Y` (`yy`) |
| Undo | `u` (via callback) |
| Repeat | `.` (dot-repeat with count) |

#### Advanced Features
- **Dot-repeat**: Records last change for replay, supports count (`4.`)
- **Unnamed register**: All d/c/y operations store in register, linewise flag preserved
- **Find repeat**: `;` and `,` repeat last `f`/`F`/`t`/`T`
- **Grapheme-aware**: Handles multi-byte Unicode, emoji, combining characters
- **Operator composition**: Operators compose with motions, text objects, and counts (`2daw`, `d3j`, `ci"`)

#### Intentional Omissions
- Named registers (`"a` through `"z`)
- Macros (`q` recording)
- Marks and jump list
- Visual block mode
- Search (`/`, `?`, `n`, `N`, `*`, `#`)
- Ex commands (only `:q`, `:w` stubs)

---

## AVA TUI's Current State

### Input Handling (`crates/ava-tui/src/state/input.rs`)

AVA TUI has **no vim mode**. The input system provides basic text editing:

**Movement:**
- `Left` / `Right` — single character
- `Home` / `End` — start/end of line
- `Up` / `Down` — line navigation with column preservation

**Editing:**
- `insert_char()` / `insert_str()` — text insertion
- `delete_backward()` / `delete_forward()` — backspace/delete
- `clear()` — clear buffer

**Special:**
- Paste placeholder collapsing (5+ lines or 500+ chars)
- `Ctrl+O` toggle paste expansion
- Command autocomplete (`/` commands, `@` mentions)
- History navigation (`Up` / `Down`)

### Keybinding System (`crates/ava-tui/src/state/keybinds.rs`)

15 configurable actions (all application-level, none vim-related):
- Navigation: `CommandPalette`, `NewSession`, `SessionList`, `ModelSwitch`
- Scrolling: `ScrollUp/Down/Top/Bottom`
- Mode: `ModeNext`, `ModePrev`, `PermissionToggle`
- Actions: `Cancel`, `Quit`, `CopyLastResponse`, `BackgroundAgent`

Configured via `~/.ava/keybindings.json`.

---

## Gap Analysis

### Feature Comparison

| Feature | Claude Code | AVA TUI |
|---------|-------------|---------|
| **Modes** | INSERT, NORMAL, VISUAL, COMMAND | Single mode (always insert) |
| **Operators** | d, c, y + linewise | None |
| **Word motions** | w, W, b, B, e, E | None |
| **Line motions** | 0, ^, $, j, k | Home, End, Up, Down |
| **Find char** | f, F, t, T, ;, , | None |
| **Text objects** | 14 objects (word, quote, bracket) | None |
| **Count prefix** | All commands | None |
| **Dot-repeat** | Full support | None |
| **Register** | Unnamed register + paste | System clipboard only |
| **Undo** | u (via callback) | None (no undo in input) |
| **Mode indicator** | Mode display in UI | N/A |
| **Configuration** | Toggle on/off | N/A |

### The Gap Is Total

AVA has **zero vim support**. This is not a partial implementation needing enhancement — it's a greenfield feature. The question is whether to build it at all.

---

## Design Recommendations

### Should AVA Implement Vim Mode?

**Arguments for:**
- Developer-focused tool — many users expect vim keybindings
- Claude Code sets the expectation
- Competitive parity with other developer TUIs (lazygit, helix, etc.)
- Input area is the primary interaction surface

**Arguments against:**
- AVA's input is a chat composer, not a code editor — most inputs are short
- Multi-line editing is rare (Shift+Enter for newlines)
- Implementation cost is significant (~1500-2000 LOC for a proper state machine)
- Maintenance burden for a feature subset of users want
- Could use an existing crate instead of building from scratch

### Recommended Approach: Incremental, Crate-Based

**Phase 1: Core State Machine** (Sprint 63-64)

1. Evaluate existing Rust crates:
   - `tui-textarea` has built-in vim mode
   - `crossterm` provides raw key events needed for modal input
   - Consider extracting Claude Code's vim engine to Rust (it's well-structured)

2. If building from scratch, implement in `crates/ava-tui/src/state/vim.rs`:
   ```rust
   pub enum VimMode { Insert, Normal, Visual }

   pub enum CommandState {
       Idle,
       Count(u32),
       Operator(Operator),
       OperatorCount(Operator, u32),
       Find(FindType),
       Replace,
       GPrefix,
   }

   pub struct VimState {
       mode: VimMode,
       command: CommandState,
       register: String,
       register_linewise: bool,
       last_change: Option<Change>,
       last_find: Option<(FindType, char)>,
   }
   ```

3. Minimum viable feature set:
   - INSERT/NORMAL mode switching (`Escape`, `i`, `a`, `A`, `o`, `O`)
   - Basic motions: `h`, `j`, `k`, `l`, `w`, `b`, `e`, `0`, `$`
   - Operators: `d`, `c`, `y` with motions
   - Linewise: `dd`, `cc`, `yy`
   - Paste: `p`, `P`
   - Character: `x`, `r`

**Phase 2: Text Objects + Polish** (Sprint 65)

4. Text objects: `iw`, `aw`, `i"`, `a"`, `i(`, `a(`, `i{`, `a{`
5. Find: `f`, `F`, `t`, `T`, `;`, `,`
6. Dot-repeat
7. Count prefix for all commands
8. Mode indicator in composer/status bar

**Phase 3: Advanced** (Sprint 66+)

9. Visual mode (character selection)
10. Undo/redo in input buffer
11. Search within input (`/`, `?`)

### Configuration

```yaml
# ~/.ava/config.yaml
tui:
  vim_mode: true  # default: false
```

Toggle at runtime: `/vim` slash command or `Ctrl+Shift+V`.

### What NOT to Implement

- Named registers (overkill for chat input)
- Macros (not useful for short inputs)
- Ex commands beyond `:q` (AVA has slash commands)
- Visual block mode (chat input is single-column)
- Marks and jump list (input buffer is ephemeral)

---

## Conclusion

AVA has no vim support — the gap is complete. Claude Code's implementation is well-architected (clean state machine, composable operators, grapheme-aware) and serves as a good reference. The recommended approach is incremental: start with a minimal INSERT/NORMAL mode with basic operators and motions, expand to text objects and dot-repeat, and defer advanced features. An existing crate like `tui-textarea` could accelerate Phase 1 significantly. Target Sprint 63-64 for initial implementation, behind a config flag defaulting to off.
