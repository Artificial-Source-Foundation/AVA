# Continue

> Open-source AI coding assistant for VS Code (~25k GitHub stars)
> Analyzed: March 2026

---

## Architecture Summary

Continue is a **VS Code extension** focused on being a universal LLM client with rich IDE integration. Unlike Cline (which is agent-focused), Continue is primarily a **chat interface** with optional agent capabilities.

**Key architectural decisions:**
- **Universal LLM client** — Supports many providers through config
- **Rich context providers** — @-mentions for files, URLs, docs, terminal, etc.
- **Autocomplete** — Code completion as well as chat
- **Open-source hub** — Community-shared configurations

### Project Structure

```
continue/
├── core/                    # Core logic (TypeScript)
│   ├── llm/                 # LLM provider abstraction
│   ├── context/             # Context providers
│   └── ...
├── extensions/
│   └── vscode/              # VS Code extension
├── gui/                     # React-based UI
└── ...
```

---

## Key Patterns

### 1. Context Providers

Rich @-mention system for injecting context:
- `@file` — File contents
- `@url` — Web page content
- `@docs` — Documentation
- `@terminal` — Terminal output
- `@git` — Git history

### 2. Autocomplete

Dual-mode operation:
- **Chat** — Conversational AI
- **Autocomplete** — Code completion (separate model, faster)

### 3. Configuration Hub

Community-shared configurations:
- Pre-built configs for different languages/stacks
- Easy import/export
- Version controlled

### 4. Universal LLM Support

Config-based provider support:
- Any OpenAI-compatible API
- Anthropic, Google, etc.
- Local models (Ollama, LM Studio)

---

## What AVA Can Learn

### High Priority

1. **Rich Context Providers** — Continue's @-mention system is excellent for quick context injection.

2. **Autocomplete** — Separate autocomplete mode with faster/smaller models improves UX.

3. **Configuration Hub** — Community sharing of configs reduces setup friction.

### Medium Priority

4. **Dual Mode** — Clear separation between chat and completion modes.

---

## Comparison: Continue vs AVA

| Capability | Continue | AVA |
|------------|----------|-----|
| **Primary focus** | Chat + autocomplete | Agent execution |
| **Platform** | VS Code | Desktop + CLI |
| **Context injection** | Rich @-mentions | File references |
| **Autocomplete** | Yes | No |
| **Agent features** | Limited | Full |

---

*Consolidated from: audits/continue-audit.md, backend-analysis/continue.md*
