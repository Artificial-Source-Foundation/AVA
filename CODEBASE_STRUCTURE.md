# AVA Codebase Structure

| Layer | Directory/Path | Purpose | Technology |
|-------|----------------|---------|------------|
| **Rust CLI** | `crates/` | ~20 Rust crates — PRIMARY codebase for agent/CLI | Rust |
| ├─ Agent | `crates/ava-agent/` | Agent execution loop + reflection | Rust |
| ├─ TUI | `crates/ava-tui/` | Interactive terminal interface (Ratatui + Crossterm) | Rust |
| ├─ LLM | `crates/ava-llm/` | LLM providers (Anthropic, OpenAI, Gemini, Ollama, OpenRouter) | Rust |
| ├─ Tools | `crates/ava-tools/` | Tool trait + 6 default tools + 7 extended | Rust |
| ├─ HQ | `crates/ava-praxis/` | Multi-agent orchestration | Rust |
| ├─ Session | `crates/ava-session/` | Session persistence (SQLite + FTS5) | Rust |
| ├─ Memory | `crates/ava-memory/` | Persistent memory/recall | Rust |
| ├─ Auth | `crates/ava-auth/` | Credentials + auth flows | Rust |
| ├─ Config | `crates/ava-config/` | Configuration management | Rust |
| ├─ Platform | `crates/ava-platform/` | File system + shell abstractions | Rust |
| ├─ Context | `crates/ava-context/` | Context window + condensation | Rust |
| ├─ Codebase | `crates/ava-codebase/` | Code indexing (BM25 + PageRank) | Rust |
| ├─ MCP | `crates/ava-mcp/` | Model Context Protocol support | Rust |
| ├─ Extensions | `crates/ava-extensions/` | Extension system | Rust |
| ├─ Permissions | `crates/ava-permissions/` | Tool permission system | Rust |
| ├─ Sandbox | `crates/ava-sandbox/` | Command sandboxing (bwrap/sandbox-exec) | Rust |
| ├─ DB | `crates/ava-db/` | SQLite connection pool | Rust |
| └─ Types | `crates/ava-types/` | Shared type definitions | Rust |
| **Desktop App** | `src/` | Frontend — SolidJS webview | TypeScript/SolidJS |
| Backend | `src-tauri/` | Tauri native host + Rust commands | Rust |
| Packages | `packages/core-v2/` | Desktop orchestration kernel | TypeScript |
| | `packages/extensions/` | 20 extension modules | TypeScript |
| | `packages/platform-tauri/` | Tauri IPC bridge | TypeScript |
| | `packages/platform-node/` | Node platform abstraction | TypeScript |
| | `packages/core/` | Legacy compatibility shim | TypeScript |
| **Legacy** | `cli/` | Legacy TS CLI (being replaced) | TypeScript |
| **Docs** | `docs/` | Architecture, roadmap, research | Markdown |
| **Tests** | `tests/`, `e2e/` | Integration and E2E tests | Rust/TS |
