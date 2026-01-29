# Estela - Project Vision

> Multi-Agent AI Coding Assistant (Tauri 2.0 + SolidJS)

---

## Executive Summary

A desktop application for AI-assisted coding using a **Commander + Council + Operators** multi-agent architecture. Built with Tauri 2.0 (Rust backend) and SolidJS (reactive frontend) for optimal performance and resource efficiency.

---

## Tech Stack

### Core Framework: Tauri 2.0

| Metric | Tauri | Electron |
|--------|-------|----------|
| App Size | ~3-10 MB | ~100+ MB |
| RAM (idle) | 30-40 MB | 200-300 MB |
| Startup Time | < 500ms | 1-2 seconds |
| Security | Rust-based, sandboxed | Node.js exposed |

### Frontend: SolidJS + TypeScript + Tailwind CSS

- **Fine-grained reactivity**: Updates only exact DOM nodes (perfect for streaming LLM responses)
- **No Virtual DOM**: Direct DOM manipulation = faster real-time updates
- **Smallest bundle**: ~7KB runtime vs React's ~40KB

### Database: SQLite via `tauri-plugin-sql`

- Session management
- Conversation history
- Agent state persistence
- Documentation index

### LSP Integration: Built-in Rust LSP client

For code intelligence (go-to-definition, find references, hover info, diagnostics).

---

## Multi-Agent Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        COMMANDER                             │
│  - Smartest/most expensive model (Claude Opus/Sonnet)       │
│  - Plans & decomposes tasks                                  │
│  - Maintains project context & documentation                 │
│  - Delegates to Operators                                    │
│  - Validates completed work                                  │
│  - NEVER touches code directly (except markdown/docs)        │
└────────────────────────┬────────────────────────────────────┘
                         │
          ┌──────────────┼──────────────┐
          │              │              │
          ▼              ▼              ▼
┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│  OPERATOR   │  │  OPERATOR   │  │  OPERATOR   │
│  (File A)   │  │  (File B)   │  │  (File C)   │
│             │  │             │  │             │
│ - Cheaper   │  │ - Isolated  │  │ - Own       │
│   model     │  │   to file   │  │   context   │
│ - Direct    │  │ - Reports   │  │ - Parallel  │
│   edits     │  │   summary   │  │   execution │
└─────────────┘  └─────────────┘  └─────────────┘
                         │
                         ▼
              ┌─────────────────┐
              │    VALIDATOR    │
              │  (Quick check)  │
              │                 │
              │ - Runs linter   │
              │ - Type checks   │
              │ - Reports back  │
              └─────────────────┘
```

---

## Key Architectural Principles

1. **Teach the orchestrator how to delegate**: Commander gives detailed task descriptions - objective, output format, tools to use, and clear boundaries.

2. **Scale effort to query complexity**:
   - Simple fact-finding: 1 agent, 3-10 tool calls
   - Direct comparisons: 2-4 operators, 10-15 calls each
   - Complex refactors: 5+ operators with clear divisions

3. **Tool design is critical**: Each tool needs a distinct purpose and clear description.

4. **Parallel tool calling**: Run multiple operators simultaneously (up to 90% time reduction).

5. **Pre-compaction documentation**: Before context window fills, spawn agent to summarize into docs.

---

## Development Phases

### Phase 1: Foundation (Week 1-2)
- [ ] Set up Tauri + SolidJS project
- [ ] Basic UI shell with tabs
- [ ] SQLite integration
- [ ] Single LLM chat (no agents yet)

### Phase 2: Single Agent (Week 3-4)
- [ ] File editing tools (str_replace, create_file)
- [ ] File reading tools
- [ ] Bash execution
- [ ] Basic streaming UI

### Phase 3: Commander + 1 Operator (Week 5-6)
- [ ] Commander agent logic
- [ ] Operator spawning
- [ ] Task delegation
- [ ] Result aggregation

### Phase 4: Parallel Operators (Week 7-8)
- [ ] Multiple concurrent operators
- [ ] File locking/boundaries
- [ ] Validation agent
- [ ] Error recovery

### Phase 5: Documentation System (Week 9-10)
- [ ] Auto-documentation before compaction
- [ ] Documentation reader for agents
- [ ] Session summaries

### Phase 6: LSP Integration (Week 11-12)
- [ ] LSP client in Rust
- [ ] Multi-language support
- [ ] Diagnostics tool for agents

### Phase 7: Polish (Week 13+)
- [ ] Multi-provider support (OpenAI, Google, etc.)
- [ ] Custom tools system
- [ ] Plugin architecture
- [ ] Performance optimization

---

## Research References

- [Anthropic Multi-Agent Research](https://www.anthropic.com/engineering/multi-agent-research-system)
- [Tauri 2.0 Docs](https://v2.tauri.app/)
- [SolidJS Tutorial](https://www.solidjs.com/tutorial/introduction_basics)
- [LSP Specification](https://microsoft.github.io/language-server-protocol/)
