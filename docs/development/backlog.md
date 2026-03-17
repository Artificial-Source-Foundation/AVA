# AVA Backlog

> Last updated: 2026-03-17
> Related: [roadmap.md](roadmap.md), [epics.md](epics.md)

Tool surface policy: default tools stay capped at 6 (`read`, `write`, `edit`, `bash`, `glob`, `grep`). New capabilities go to Extended, MCP, plugin, or custom-tool tier.

## Recently Completed

- **Plugin system Phase 1** — `ava-plugin` crate, AgentStack wiring, TypeScript SDK, 4 examples, CLI commands. Smoke tested e2e.
- **Dead code cleanup** — 30 unwired modules → `docs/ideas/`, -10.5K LOC
- **Docs overhaul** — README, crate-map, plugin guide, changelog, CLAUDE.md/AGENTS.md

## Execution Order

### Next (High Impact)

1. **Plugin Phase 2** — `@ava-ai/plugin` npm publish, Python SDK, auth hook sub-protocol.
2. **Plugin Phase 3** — OpenCode compatibility bridge, plugin marketplace.
3. **B26** Praxis in chat composer — deeper worker/task inspection.
4. **B79** Evaluation harness — SWE-bench integration.
5. **B80** Trajectory recording — JSONL decision trees.

### Platform Verification (Must Do)

6. **Desktop app audit** — verify Tauri desktop app works end-to-end (session CRUD, agent runs, tool execution, model switching). Fix broken IPC commands.
7. **TUI smoke test suite** — automated smoke tests for TUI mode (session resume, slash commands, theme switching, plugin list, model selector).
8. **CLI headless regression** — verify all headless flags work (--json, --follow-up, --later, --multi-agent, --workflow).
9. **Web/browser mode** — explore hosting AVA locally with a web UI on a port (e.g. `ava serve --port 8080`). Could use the existing SolidJS frontend served via HTTP instead of Tauri.

### Soon (Medium Impact)

6. **B41** Session templates -- P3. Save conversation patterns as reusable templates (system prompt + tool set + follow-up pipeline). Store in `.ava/templates/`.
7. **B42** Custom agent modes/personas -- P3. User-defined modes beyond Code/Plan with specific system prompts, tool permissions, model overrides.
8. **B74** Custom keybindings -- P3. User-definable keybindings in `~/.ava/keybindings.json`.

### Later (Polish)

9. **B78** Auto-formatting detection -- P3. Detect IDE auto-formatters changing files between agent write and next read. Opt-in.

### Plugin/Extended Only (do not expand default surface)

- **B68** Batch tool -- Extended tier. Parallel tool execution up to 25 invocations.
- **B75** Directory listing tool -- Extended tier. Tree-view respecting .gitignore.
- **B55** Security scanning -- Plugin. Vulnerability detection via semgrep/cargo-audit.
- **B56** Test generation -- Plugin. Automated test generation with edge case detection.
- **B72** Browser automation -- Plugin/MCP. Web page interaction.
- **B77** PR checkout workflow -- Plugin. `/pr <number>` via gh CLI.

### Ideas (from `docs/ideas/`)

These are archived design docs for capabilities that were descoped. Worth revisiting for plugin implementations:

- Agent hooks and lifecycle automation
- Build race (parallel build strategies)
- Cron scheduler for background tasks
- GitHub issue resolver agent
- Guardian subagent (safety monitor)
- Model routing classifier
- Network policy enforcement
- Permission pattern learning
- Recipe system (reusable multi-step workflows)
- Session continuity across machines
- Streaming edit (apply edits as they stream in)

## Implemented (Pending Manual Testing)

These are code-complete features from Sprints 60-61 that need live validation:

| ID | Sprint | Title |
|----|--------|-------|
| B67 | 61 | RelativeIndenter for edit matching |
| B54 | 61 | Auto lint+test after edits |
| B37 | 61 | Smart `/commit` with LLM message generation |
| B66 | 61 | Ghost snapshots |
| B34 | 60 | Three-tier mid-stream messaging |
| B33 | 60 | Claude Code as subagent |
| B24 | 60 | Hooks system (16 events, 3 action types) |
| B25 | 60 | Background agents (`Ctrl+B`) |
| B32 | 60 | OS keychain credential storage |
| B21 | 60 | `/btw` side conversations |
| B22 | 60 | Rewind system (`/undo`, `Esc+Esc`) |
| B23 | 60 | `/export` conversation export |
| B27 | 60 | `/compact` command |
| B28 | 60 | `/init` project bootstrap |
| B29 | 60 | Custom slash commands |
| B30 | 60 | `/copy` code block picker |

## Completed

80+ backlog items completed across Sprints 11-66 and post-66 work. See [epics.md](epics.md) for grouped summaries and [CHANGELOG.md](CHANGELOG.md) for version-level detail.
