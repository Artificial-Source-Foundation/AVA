# TUI/Interactive CLI Comparison Matrix

> Generated 2026-03-06. Based on analysis of 12 competitor codebases.

## Framework Overview

| Tool | Language | TUI Framework | Interactive? | Maturity |
|------|----------|---------------|-------------|----------|
| **OpenCode** | TypeScript | Solid.js + OpenTUI | Full TUI | Production |
| **Gemini CLI** | TypeScript | React + Ink (custom fork) | Full TUI | Production |
| **Claude Code (Pi-Mono)** | TypeScript | Custom differential TUI | Full TUI | Production |
| **Codex CLI** | Rust | Ratatui + Crossterm | Full TUI | Production |
| **Aider** | Python | Rich + prompt_toolkit | Interactive CLI | Production |
| **Goose** | TypeScript | Ink (React for CLI) | Basic TUI | Early |
| **Plandex** | Go | Bubble Tea + Lipgloss | Streaming TUI | Production |
| **SWE-Agent** | Python | Textual (inspector only) | Partial | Inspector |
| **Cline** | TypeScript | VS Code webview only | No CLI | N/A |
| **Continue** | TypeScript | IDE extension only | No CLI | N/A |
| **OpenHands** | Python | Web UI only | No CLI | N/A |
| **Zed** | Rust | GPUI (native GPU) | No CLI | N/A |

## Feature Matrix

| Feature | OpenCode | Gemini CLI | Claude Code | Codex CLI | Aider | AVA (current) |
|---------|----------|-----------|-------------|-----------|-------|---------------|
| **Chat input** | Multi-line + autocomplete | Multi-line + @mentions | Multi-line + kill ring | Multi-line + slash cmds | Multi-line + history | None (batch only) |
| **Streaming** | 60fps batched events | Hook-based + spinner | Differential line render | 120fps adaptive chunking | 20fps sliding window | Line-by-line console.log |
| **Tool approval** | 3-stage modal | Explicit approval dialog | Execute by default | Modal overlay | Embedded in chat | --yolo flag only |
| **Markdown** | Theme-based + syntax | marked + highlight.js | Custom + caching | pulldown_cmark + custom | Rich library | None |
| **Code highlighting** | Per-theme syntax styles | highlight.js + lowlight | Custom per-language | Rust syntax crate | Pygments | None |
| **Diff display** | Split/unified + colors | DiffRenderer component | Git-style inline | Three-tier color depth | Rich panels | None |
| **Session management** | Fork/branch/resume | Linear + resume | DAG (append-only JSONL) | JSONL + session picker | File history | Create only |
| **Model switching** | Ctrl+M dialog + cycle | /model command | Model change entries | Slash command | /model command | --model flag |
| **Themes** | 30+ built-in (JSON) | Configurable | Markdown theme system | Three color depths | Rich styles | None |
| **Keyboard shortcuts** | Leader key + configurable | Standard + vim bindings | Emacs/Kitty configurable | Full key state machine | vi mode toggle | None |
| **Command palette** | Ctrl+/ with fuzzy search | / slash commands | N/A | Slash command popup | / slash commands | None |
| **Sidebar/panels** | MCP, diffs, TODOs, cost | Background shell display | N/A | Multi-agent sidebar | N/A | None |
| **Accessibility** | Dark/light auto-detect | Screen reader support | IME + hardware cursor | Paste burst detection | N/A | None |
| **Image support** | N/A | Limited inline | Kitty protocol | N/A | N/A | None |

## Architecture Patterns

### Rendering Strategy Comparison

| Approach | Used By | FPS | Technique | Pros | Cons |
|----------|---------|-----|-----------|------|------|
| **Event batching** | OpenCode | 60 | Queue events for 16ms, flush batch | Smooth, predictable | Slight latency |
| **Reactive Ink** | Gemini CLI | ~30 | React reconciliation | Familiar, declarative | Ink overhead |
| **Differential** | Claude Code | ~60 | Line-level diff, ANSI cursor | Minimal bandwidth | Complex implementation |
| **Adaptive chunking** | Codex CLI | 120 | Hysteresis queue + drain plans | Ultra smooth | Complex state machine |
| **Throttled window** | Aider | 20 | Stable lines + live widget | Simple, effective | Lower FPS |

### State Management Comparison

| Approach | Used By | Pattern | Strengths |
|----------|---------|---------|-----------|
| **Solid.js signals** | OpenCode | Provider context stack (15 providers) | Fine-grained reactivity, no vDOM |
| **React Context** | Gemini CLI | UIStateContext + hooks | Familiar, composable |
| **Class properties** | Claude Code | Plain TS classes + events | Simple, zero overhead |
| **Tokio channels** | Codex CLI | MPSC + cancellation tokens | Structured concurrency |
| **Procedural** | Aider | Direct function calls | Straightforward |

### Tool Approval Patterns

| Pattern | Used By | UX Quality | Description |
|---------|---------|-----------|-------------|
| **3-stage modal** | OpenCode | Best | Preview -> action selection -> optional rejection reason |
| **Confirmation dialog** | Gemini CLI | Good | Tool details + approve/deny with diff preview |
| **Modal overlay** | Codex CLI | Good | Blocks input, queue-based, sandbox escalation |
| **Embedded in chat** | Aider | Okay | Search/replace blocks in markdown stream |
| **Execute by default** | Claude Code | Fast but risky | No approval, errors handled post-execution |

### Session Models

| Model | Used By | Branching | Persistence |
|-------|---------|-----------|-------------|
| **DAG (JSONL)** | Claude Code | Non-destructive tree branching | Append-only file |
| **Fork/branch** | OpenCode | Fork from any point | Backend storage |
| **Linear + resume** | Gemini CLI | No branching | JSON file |
| **JSONL + picker** | Codex CLI | Session selection | JSONL log |

## Recommendation for AVA

### Framework Decision: Ink (React) vs OpenTUI/Solid vs Custom

| Option | Pros | Cons | Effort |
|--------|------|------|--------|
| **Ink (React 19)** | Mature ecosystem, used by Gemini CLI + Goose, huge community, flexbox layout, many plugins | React not SolidJS (two paradigms), Ink fork may be needed | Medium |
| **OpenTUI + Solid.js** | Same framework as AVA desktop (SolidJS), code sharing potential, used by OpenCode (production), fine-grained reactivity | OpenTUI is newer/less mature, smaller ecosystem, bun-coupled in OpenCode | Medium-High |
| **Custom differential** | Max performance, full control, used by Claude Code | Huge effort, maintenance burden, reinventing wheel | Very High |

### Recommended: **Ink (React)** with adaptation layer

**Rationale:**
1. Gemini CLI proves Ink works at production scale for AI coding CLIs
2. Goose also chose Ink for the same use case
3. Ink has the largest ecosystem (ink-text-input, ink-select-input, ink-spinner, ink-gradient, ink-markdown, etc.)
4. React knowledge is universal — easier to maintain
5. The desktop frontend (SolidJS) and TUI (React/Ink) serve different purposes — code sharing is minimal anyway since terminal UI components are fundamentally different from DOM components
6. Ink's flexbox layout model handles terminal resize gracefully

### Key Patterns to Implement (Priority Order)

1. **Event batching** (from OpenCode) — 16ms batch window for streaming
2. **3-stage tool approval** (from OpenCode) — preview, action, confirmation
3. **Adaptive streaming** (from Codex CLI) — hysteresis-based smooth/catch-up modes
4. **Markdown + syntax highlighting** (from Gemini CLI) — marked + highlight.js
5. **Command palette** (from OpenCode) — Ctrl+/ with fuzzy search
6. **Configurable keybindings** (from Claude Code) — JSON config file
7. **Session fork/resume** (from OpenCode) — branch conversations
8. **Theme system** (from OpenCode) — 10+ themes with auto dark/light detection
9. **Diff display** (from Codex CLI) — split/unified with color depth detection
10. **Accessibility** (from Gemini CLI) — screen reader support

### Architecture Blueprint

```
cli/src/tui/
  app.tsx                    # Root Ink component + provider stack
  contexts/
    streaming.tsx            # Event batching (16ms) + streaming state
    session.tsx              # Session management + fork/resume
    keybind.tsx              # Configurable keyboard shortcuts
    theme.tsx                # Terminal theme system
    permission.tsx           # Tool approval state
  components/
    chat/
      message-list.tsx       # Scrollable message history
      message.tsx            # Single message (user/assistant/tool)
      streaming-text.tsx     # Real-time token streaming with markdown
    input/
      composer.tsx           # Multi-line text input
      autocomplete.tsx       # @mentions + /commands + file completion
      command-palette.tsx    # Ctrl+/ fuzzy command search
    approval/
      tool-approval.tsx      # 3-stage approval modal
      diff-preview.tsx       # Inline diff display
    layout/
      app-layout.tsx         # Header + messages + input + footer
      sidebar.tsx            # Optional info panel (wide terminals)
      status-bar.tsx         # Model, tokens, session info
    shared/
      markdown.tsx           # Terminal markdown renderer
      code-block.tsx         # Syntax-highlighted code
      spinner.tsx            # Loading indicators
      dialog.tsx             # Modal dialog system
  hooks/
    use-streaming.ts         # Streaming state + event handling
    use-keypress.ts          # Global key event handling
    use-terminal-size.ts     # Responsive layout
  themes/
    dracula.json
    nord.json
    catppuccin.json
    # ... more
```

## Reference File Index

### OpenCode (Solid.js + OpenTUI)
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/app.tsx` — Root component
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/index.tsx` — Session UI
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/theme.tsx` — Theme system
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/sdk.tsx` — Event batching
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/keybind.tsx` — Keybindings
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/permission.tsx` — Approval

### Gemini CLI (React + Ink)
- `docs/reference-code/gemini-cli/packages/cli/src/gemini.tsx` — Entry point
- `docs/reference-code/gemini-cli/packages/cli/src/ui/App.tsx` — Root component
- `docs/reference-code/gemini-cli/packages/cli/src/ui/layouts/DefaultAppLayout.tsx` — Layout
- `docs/reference-code/gemini-cli/packages/cli/src/ui/components/messages/ToolConfirmationMessage.tsx` — Approval
- `docs/reference-code/gemini-cli/packages/cli/src/ui/hooks/useGeminiStream.ts` — Streaming

### Claude Code / Pi-Mono (Custom TUI)
- `docs/reference-code/pi-mono/packages/tui/src/tui.ts` — Core TUI renderer
- `docs/reference-code/pi-mono/packages/tui/src/components/markdown.ts` — Markdown
- `docs/reference-code/pi-mono/packages/tui/src/components/input.ts` — Text input
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/session-manager.ts` — Session DAG

### Codex CLI (Ratatui / Rust)
- `docs/reference-code/codex-cli/codex-rs/tui/src/app.rs` — Main event loop (5,586 lines)
- `docs/reference-code/codex-cli/codex-rs/tui/src/chatwidget.rs` — Chat rendering (8,273 lines)
- `docs/reference-code/codex-cli/codex-rs/tui/src/streaming/chunking.rs` — Adaptive streaming
- `docs/reference-code/codex-cli/codex-rs/tui/src/bottom_pane/approval_overlay.rs` — Approval modal

### Aider (Rich + prompt_toolkit)
- `docs/reference-code/aider/aider/io.py` — I/O and input handling
- `docs/reference-code/aider/aider/mdstream.py` — Streaming markdown renderer

### Goose (Ink)
- `docs/reference-code/goose/ui/text/src/app.tsx` — Ink app root

### Plandex (Bubble Tea)
- `docs/reference-code/plandex/app/cli/stream_tui/model.go` — TUI model
