# Sprint 34a: TUI Research — How OpenCode & Codex CLI Build Their Frontend

> **Research-only sprint** — no code changes. Output goes to `docs/development/research/tui-implementation-research.md`

## Goal

Deep-dive into how OpenCode and Codex CLI implement their TUI/frontend. Understand exactly how they build: command palette, welcome screen, model selector, status bar, keyboard hints, streaming display, and input handling. This research informs Sprint 34 (TUI parity).

## Method

Use subagents to analyze each codebase in parallel. Read actual source code — not just docs.

## Research Target 1: OpenCode TUI

OpenCode is a Go/TypeScript CLI with a polished Bubble Tea TUI.

### Questions to answer:
1. **Architecture**: What TUI framework do they use? How is the app structured (components, state, events)?
2. **Welcome screen**: How is the splash/logo screen implemented? What triggers showing vs hiding it?
3. **Command palette (ctrl+p)**: How is the `/command` system built? How do commands register? How does the fuzzy search work?
4. **Model selector**: How does the inline model badge work? How do they switch models at runtime?
5. **Status bar**: What data does it show (git branch, MCP count, version)? How does it update?
6. **Keyboard hints**: How are hints displayed below the input? Are they context-sensitive?
7. **Input handling**: How do they handle multi-line input, `!` shell prefix, key bindings?
8. **Streaming display**: How do they render streaming tokens? Frame rate? Buffer strategy?
9. **Layout**: How do they handle terminal resizing, scrolling, viewport management?
10. **Theming**: Do they support themes/colors? How?

### Files to read:
- Look in `packages/tui/` or `internal/tui/` or similar
- Find the main app component, views, components
- Find the command palette implementation
- Find the status bar / footer component
- Find the input / textarea component

## Research Target 2: Codex CLI TUI

Codex CLI is a Rust CLI (similar to AVA).

### Questions to answer:
1. **Architecture**: What TUI framework? (likely Ratatui — same as AVA). How do they structure components?
2. **Streaming animation**: How do they implement `TARGET_FRAME_INTERVAL` for smooth token display?
3. **Input handling**: Multi-line? Key bindings? Command parsing?
4. **Layout**: How do they handle the terminal canvas? Split panes?
5. **Tool approval UI**: How do they show tool calls and get approval?
6. **Status/progress**: How do they show agent status, token counts, cost?
7. **Error display**: How do they show errors inline?

### Files to read:
- Look in `codex-cli/src/` or `crates/` for TUI code
- Find the main render loop
- Find the streaming display component
- Find the approval/confirmation UI

## Research Target 3: Claude Code TUI (if accessible)

If reference code exists in `docs/reference-code/`, also look at how Claude Code implements its TUI for comparison.

## Output Format

Create `docs/development/research/tui-implementation-research.md` with:

```markdown
# TUI Implementation Research — OpenCode & Codex CLI

## OpenCode

### Architecture
[framework, structure, component model]

### Welcome Screen
[how it works, code patterns]

### Command Palette
[implementation details, registration pattern]

### Model Selector
[runtime switching, UI pattern]

### Status Bar
[data sources, update mechanism]

### Keyboard Hints
[display pattern, context sensitivity]

### Input Handling
[multi-line, shell prefix, key bindings]

### Streaming Display
[rendering strategy, frame rate, buffering]

## Codex CLI

### Architecture
[framework, structure]

### Streaming Animation
[TARGET_FRAME_INTERVAL, buffering]

### Tool Approval UI
[confirmation pattern]

### Status Display
[token counts, cost, progress]

## Patterns to Steal for AVA

### 1. [Pattern name]
- What: [description]
- From: [OpenCode/Codex]
- How to implement in Ratatui: [brief approach]
- Priority: [high/medium/low]

### 2. [Pattern name]
...

## Recommendations for Sprint 34
[Ordered list of what to build first based on research]
```

## Constraints

- **Research only** — do NOT modify any code
- Read actual source files, not just READMEs
- Focus on implementation patterns that can be adapted to Ratatui
- Note any dependencies or libraries they use that AVA should consider
- Be specific — include file paths and code snippets where relevant
