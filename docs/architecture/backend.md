<!-- Last verified: 2026-03-16. Run 'cargo test --workspace' to revalidate. -->

# Backend Overview

> AVA uses a pure Rust backend for both CLI and desktop. The SolidJS frontend communicates with Rust via Tauri IPC commands.
>
> See `CLAUDE.md` for current architecture guidance.

## Runtime Model

```
SolidJS (src/) → Tauri IPC → Rust commands (src-tauri/) → Rust crates (crates/)
CLI (ava-tui)  → direct calls → Rust crates (crates/)
```

All backend logic lives in Rust crates. Tauri commands in `src-tauri/src/commands/` bridge the desktop frontend to the crate ecosystem.

## Current Shape

- Rust crates: ~22 under `crates/`
- Built-in tools by default: 6, with 8 additional extended tools available when enabled, plus task/todo/question helpers and dynamic MCP/custom tools

### Rust Crate Index (~22)

1. `ava-agent` — Agent execution loop
2. `ava-auth` — OAuth, Copilot token exchange, PKCE
3. `ava-cli-providers` — CLI provider management
4. `ava-codebase` — Code indexing and search
5. `ava-config` — Configuration management
6. `ava-context` — Context window management
7. `ava-db` — SQLite connection pool
8. `ava-extensions` — Extension system
9. `ava-llm` — LLM providers and clients
10. `ava-mcp` — Model Context Protocol
11. `ava-memory` — Persistent memory/recall
12. `ava-permissions` — Permission system (9-step DefaultInspector, PermissionPolicy, CommandClassifier, SafetyTag/RiskLevel)
13. `ava-platform` — Platform abstractions
14. `ava-praxis` — Multi-agent orchestration (Praxis)
15. `ava-sandbox` — Command sandboxing
16. `ava-session` — Session persistence
17. `ava-tools` — Tool trait and registry
18. `ava-tui` — CLI/TUI binary (Ratatui)
19. `ava-types` — Shared types
20. `ava-validator` — Validation pipeline

See `crates/` directory for source. Run `cargo test --workspace` for test status.

## Safety and Reliability Layers

- sandbox routing for install-class shell commands
- dynamic permission learning with dangerous-command safeguards
- checkpoint refs for recovery before destructive actions
- agent reliability middleware for stuck-loop and recovery handling

### Permission Model

The permission system uses two levels:

- **PermissionLevel** (`ava-tui/src/state/permission.rs`): `Standard` (default) or `AutoApprove` (replaces old `--yolo` flag, CLI flag `--auto-approve` with `--yolo` alias). Toggle at runtime via `/permissions` command.
- **DefaultInspector** (`ava-permissions/src/inspector.rs`): 9-step evaluation — command classification, path safety, auto-approve check, session approvals, policy blocked/allowed tools, tag checks, risk threshold, static/dynamic rules. Critical commands (rm -rf /, sudo, fork bombs) are always blocked regardless of permission level.

### Agent Modes

Agent execution modes (`ava-tui/src/state/agent.rs`): `Code` (default, full tool access), `Plan` (read-only tools, analysis/planning). Mode-specific prompt suffix injected via `AgentStack.mode_prompt_suffix` into `AgentConfig.system_prompt_suffix`. Tab/Shift+Tab cycles modes in the TUI composer.

## Where To Read Next

- `docs/architecture/architecture-guide.md`
- `docs/troubleshooting/`
- `CLAUDE.md`
