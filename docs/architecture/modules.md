<!-- Last verified: 2026-03-07. Run 'cargo test --workspace' to revalidate. -->

# Backend Modules

> **Hybrid Architecture**: Rust crates (`crates/`) for CLI + TypeScript (`packages/core-v2/` + `packages/extensions/`) for desktop
>
> `packages/core/` is now a compatibility re-export shim.
>
> **Rule**: All new CLI/agent features MUST be Rust (crates/).

## Module Organization

### Core Runtime (`packages/core-v2/`)

The execution kernel contains:

| Module | Purpose |
|--------|---------|
| `agent/` | Agent loop, turn execution, tool dispatch, event handling |
| `tools/` | Core tool registry and built-in tool definitions |
| `llm/` | LLM client factory and message handling |
| `session/` | Session CRUD, persistence, forking |
| `context/` | Token tracking and context window management |
| `extensions/` | Extension loading, lifecycle, manifest parsing |
| `permissions/` | Permission checks and security pipeline |
| `bus/` | Pub/sub message bus |

### Extensions (`packages/extensions/`)

20 built-in extension modules:

1. **agent-modes** — Agent mode management (plan mode, etc.)
2. **commander** — Worker delegation and batch execution
3. **context** — Context strategies and compaction
4. **diff** — Diff tracking and patch application
5. **git** — Git operations and auto-commit
6. **hooks** — Lifecycle hook system
7. **instructions** — Project instruction loading
8. **lsp** — Language Server Protocol client
9. **mcp** — Model Context Protocol support
10. **memory** — Persistent memory and recall
11. **models** — Model registry and availability
12. **permissions** — Runtime permission system
13. **plugins** — Plugin management UI
14. **prompts** — Prompt building and variants
15. **providers** — LLM provider implementations (16 sub-providers)
16. **recall** — Memory recall integration
17. **server** — Local server for external integrations
18. **slash-commands** — User-defined slash commands
19. **tools-extended** — Extended tool surface (~15 tools)
20. **validator** — QA validation pipeline

### Rust Crates (`crates/`) — CLI/Agent Stack

20 Rust crates currently make up the CLI and agent runtime:

1. `ava-agent` — Agent execution loop + reflection
2. `ava-auth` — OAuth and credential flows
3. `ava-cli-providers` — CLI provider management
4. `ava-codebase` — Code indexing (BM25 + PageRank)
5. `ava-config` — Configuration management
6. `ava-context` — Context window management
7. `ava-db` — SQLite connection pool
8. `ava-extensions` — Extension system
9. `ava-llm` — LLM providers (Anthropic, Copilot, Gemini, Inception, Ollama, OpenAI, OpenRouter, mock)
10. `ava-mcp` — Model Context Protocol
11. `ava-memory` — Persistent memory/recall
12. `ava-permissions` — Permission system
13. `ava-platform` — Platform abstractions (fs, shell)
14. `ava-praxis` — Multi-agent orchestration (Praxis)
15. `ava-sandbox` — Command sandboxing (bwrap/sandbox-exec)
16. `ava-session` — Session persistence (SQLite + FTS5)
17. `ava-tools` — Tool trait + registry + tiered core tools
18. `ava-tui` — CLI/TUI binary (Ratatui + Crossterm)
19. `ava-types` — Shared types
20. `ava-validator` — Validation pipeline

**Tool surface**: 6 built-in tools by default, 7 extended tools when enabled, plus separately-registered task/todo/question helpers and dynamic MCP/custom tools.

## Migration Notes

The previous `packages/core/` monolith has been restructured:
- Business logic moved to `packages/core-v2/`
- Features modularized into `packages/extensions/`
- `packages/core/` now re-exports from core-v2 for compatibility

---

*See also: [architecture-guide.md](architecture-guide.md), [CLAUDE.md](../../CLAUDE.md)*
