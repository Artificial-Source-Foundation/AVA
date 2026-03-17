# AVA Roadmap

> Last updated: 2026-03-16

## v2.1.x (Current) -- Stabilization and Polish

Released v2.1.0 on 2026-03-08. Currently on v2.1.1 with security hardening, dead code cleanup, and backend gap fills.

### Delivered
- 66 sprints completed (11-66)
- 1,466 tests across 21 crates
- ~128K lines of Rust
- Security audit and hardening pass
- Dead code cleanup (~10.5K lines removed, 30 unwired modules archived to `docs/ideas/`)
- Documentation overhaul
- Backend gaps filled from competitive analysis (BG-1 through BG-14)

### Remaining v2.1.x Work
- Manual testing of Sprint 60-61 implemented features (16 items pending validation)
- Praxis chat UX deepening (B26)
- Bug fixes and polish as discovered

## v3.0 -- Plugin System and Lean Core

The plugin system is AVA's next major milestone. Goal: make AVA extensible by third parties without expanding the compiled default tool surface.

### Phase 1: Plugin Runtime
- `ava-plugin` crate (started, currently empty)
- Plugin trait with lifecycle hooks (init, activate, deactivate)
- Plugin isolation (separate process or WASM sandbox)
- Plugin configuration via TOML

### Phase 2: Plugin SDK and Distribution
- `@ava-ai/plugin` npm package or Rust crate template
- `ava plugin install <name>` CLI command (B46)
- Plugin registry (local index, optional remote)
- Version pinning and update mechanism

### Phase 3: Community Ecosystem
- Plugin marketplace with search and ratings
- Verified publisher program
- Plugin templates for common patterns (tool, hook, agent mode, theme)
- OpenCode plugin compatibility bridge

### Plugin-First Capabilities (deliver as plugins, not built-in)
- Security scanning (B55) -- semgrep/cargo-audit wrapper
- Test generation (B56)
- Browser automation (B72) -- Playwright/Puppeteer MCP
- PR checkout workflow (B77) -- gh CLI wrapper

## Future -- Team and Cloud Features

These are not actively planned but represent the natural evolution:

- **Cloud sync**: Session and memory sync across machines
- **Team features**: Shared agent configurations, team-wide plugins
- **Hosted runtime**: Cloud-hosted agent execution
- **Analytics dashboard**: Usage, cost, and quality metrics across team

## Codebase Stats

| Metric | Value |
|--------|-------|
| Rust crates | 21 |
| Rust source files | 452 |
| Lines of Rust | ~128,000 |
| Tests | 1,466 |
| Clippy | Clean |
| Default tools | 6 |
| Extended tools | 7 |
| LLM providers | 7 |
| Built-in themes | 29 |
| Total commits | 484 |

## Completed Milestones

| Version | Date | Sprints | Focus |
|---------|------|---------|-------|
| v1.0.0 | 2026-03-07 | 11-50f | Foundation through stabilization |
| v2.0.0 | 2026-03-07 | 51-59 | TUI rework, providers, model catalog |
| v2.1.0 | 2026-03-08 | 60-66 | v3 backend + UX delivery |
| v2.1.1 | 2026-03-16 | post-66 | Security, cleanup, backend gaps |
