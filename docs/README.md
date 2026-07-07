# AVA Documentation

This is the navigation spine for AVA's user, automation, engineering, and product docs. Prefer this index over the older release-ledger lists in the root README.

## Start Here

- [`features.md`](features.md): user-facing AVA vs. Pi feature matrix and current limits.
- [`USAGE.md`](USAGE.md): running AVA, TUI commands, headless modes, tool visibility, permissions, and current limits.
- [`CONFIG.md`](CONFIG.md): XDG paths, credentials, models, prompts, subagents, project trust, LSP, compaction, and local resources.
- [`TESTING.md`](TESTING.md): normal CTest workflow, headless/tool smokes, live-provider dogfood, sanitizer runs, and release evidence.
- [`CONTRIBUTING.md`](CONTRIBUTING.md): build prerequisites, development workflow, formatting, and review expectations.
- [`thinking-modes.md`](thinking-modes.md): build/plan modes, provider reasoning levels, and Pi thinking-mode parity.
- [`terminal-setup.md`](terminal-setup.md): terminal capability setup and troubleshooting for tmux, Kitty, OSC 8/52, paste, mouse, and themes.
- [`themes-keybindings.md`](themes-keybindings.md): TUI themes, display settings, keybinding files, and Pi theme/keybinding differences.
- [`security-sandboxing.md`](security-sandboxing.md): permission boundaries, trust, plugin/MCP process limits, and container/VM guidance.

## Automation And Extension Contracts

- [`headless-protocol.md`](headless-protocol.md): print/RPC JSONL protocol, event envelopes, resolver flows, and headless permission flags.
- [`mcp.md`](mcp.md): local stdio MCP configuration and safety boundaries.
- [`plugin-system.md`](plugin-system.md): local plugin authoring, manifest shape, runner protocol, permissions, diagnostics, and sample plugin.
- [`plugin-compatibility-policy.md`](plugin-compatibility-policy.md): compatibility, deprecation, and golden-fixture policy for plugin/MCP contracts.
- [`providers.md`](providers.md): runtime provider status, auth modes, reasoning support, live smoke variables, and deferred provider breadth.
- [`session-format.md`](session-format.md): public AVA session JSONL envelope, entry types, validation, import/export caveats, and Pi incompatibility.
- [`context-resources.md`](context-resources.md): context files, system prompts, prompt commands, skills, subagents, plugins, MCP, LSP, and trust.
- [`docker/README.md`](docker/README.md): Docker build and container usage.
- [`desktop-qml.md`](desktop-qml.md): optional Qt Quick desktop prototype.

## Engineering References

- [`engineering/cpp-safety-rules.md`](engineering/cpp-safety-rules.md): C++ ownership, error, concurrency, and RAII rules.
- [`engineering/session-versioning.md`](engineering/session-versioning.md): session JSONL entry and payload versioning policy.
- [`engineering/side-effect-safety-checklist.md`](engineering/side-effect-safety-checklist.md): required questions before adding new side-effect classes.
- [`AGENTS.md`](../AGENTS.md): repository source map and agent workflow rules.

## Product, Parity, And Roadmap

- [`product/mvp-baseline.md`](product/mvp-baseline.md): living Pi-first/OpenCode-second MVP baseline.
- [`product/mvp-coverage-ledger.md`](product/mvp-coverage-ledger.md): evidence mapping for checked baseline rows.
- [`product/backend-capabilities-1.0.md`](product/backend-capabilities-1.0.md): 1.0 backend baseline capability status view.
- [`product/capabilities-1.1.md`](product/capabilities-1.1.md): backend candidate planning after 1.0.
- [`product/tooling-plan.md`](product/tooling-plan.md): built-in tool contract and design notes.
- [`product/package-manager-plan.md`](product/package-manager-plan.md): package/resource install, trust, provenance, rollback, and offline design plan.
- [`product/parallel-tools-plan.md`](product/parallel-tools-plan.md): ordinary parallel tool execution design risks and rollout plan.
- [`release-checklist.md`](release-checklist.md): manual release-gate checklist, smokes, artifacts, checksums, and known blockers; it is not an implemented release pipeline.
- [`roadmap/backend.md`](roadmap/backend.md): backend roadmap and historical phase rationale.
- [`roadmap/backend-maturity-baseline.md`](roadmap/backend-maturity-baseline.md): maturity gates and acceptance criteria.
- [`goals/README.md`](goals/README.md): goal package index, including Pi MVP parity area files.

## Version Ledgers

- [`versions/README.md`](versions/README.md): release-position ledger index.
- [`versions/1.1.md`](versions/1.1.md): current backend planning and implementation journal.
- [`versions/1.0.md`](versions/1.0.md): 1.0 backend baseline notes.
- Older `versions/0.*.md` files are historical release-position evidence. They may mention features that were deferred at that time but have since landed.

## Reference Code

- `reference-code/pi/` and `reference-code/opencode/` are local behavior references only. Do not copy source or architecture from them into AVA, and do not include them in builds, formatting, or broad source searches unless explicitly doing reference analysis.
