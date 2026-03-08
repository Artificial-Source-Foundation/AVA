# Codex CLI

> OpenAI's official CLI agent (~20k GitHub stars)
> Analyzed: March 2026

---

## Architecture Summary

Codex CLI is OpenAI's official terminal-based AI coding assistant. It's built as a **lightweight TypeScript CLI** (~15k lines) with a focus on simplicity and OpenAI ecosystem integration. Uses the OpenAI Responses API with built-in tool calling.

**Key architectural decisions:**
- **Native tool calling** — Uses OpenAI's function calling API, not chat-and-parse
- **Simple agent loop** — Single-threaded, straightforward implementation
- **Sandboxed execution** — Uses macOS seatbelt for command sandboxing
- **Minimal dependencies** — Lean architecture, easy to understand

### Project Structure

```
codex/
├── codex-cli/               # Main CLI package
│   ├── src/
│   │   ├── components/      # React Ink UI components
│   │   ├── utils/           # Utilities (agents, formatting, etc.)
│   │   ├── tests/           # Test suite
│   │   └── ...
│   └── package.json
└── README.md
```

---

## Key Patterns

### 1. OpenAI Responses API

Built specifically for the Responses API with native tool calling:
- Structured function definitions
- Parallel tool execution
- Built-in reasoning traces (o1, o3 models)

### 2. Sandboxed Execution

Uses macOS seatbelt for shell command sandboxing:
- Restricted file system access
- Network policy controls
- Prevents dangerous operations

### 3. React Ink TUI

Terminal UI built with React Ink (React for terminals):
- Component-based architecture
- Real-time streaming updates
- Interactive approval UI

### 4. Approval Workflows

Built-in approval system for destructive operations:
- Read operations auto-approved
- Write operations require confirmation
- Commands can be pre-approved per-session

---

## What AVA Can Learn

### High Priority

1. **Sandboxed Execution** — Codex's seatbelt approach to shell safety is excellent. AVA should consider similar sandboxing.

2. **Simplicity** — Codex's lean architecture makes it easy to understand and contribute to. AVA should maintain modularity without over-engineering.

### Medium Priority

3. **Native Tool Calling** — Codex uses proper function calling, which is more reliable than parsing text. AVA already does this well.

4. **React Ink Patterns** — For AVA's CLI, React Ink provides a clean component model for TUIs.

---

## Comparison: Codex CLI vs AVA

| Capability | Codex CLI | AVA |
|------------|-----------|-----|
| **Platform** | CLI only | Desktop + CLI |
| **Sandboxing** | macOS seatbelt | Docker (optional) |
| **Provider** | OpenAI only | Multi-provider |
| **Architecture** | Simple, lean | Modular, feature-rich |
| **UI** | React Ink TUI | Tauri + SolidJS |
| **Tool calling** | Native | Native |

---

*Consolidated from: audits/codex-cli-audit.md, backend-analysis/codex-cli.md*
