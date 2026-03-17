# AVA Backlog

> Last updated: 2026-03-16
> Related: [roadmap.md](roadmap.md), [epics.md](epics.md)

Tool surface policy: default tools stay capped at 6 (`read`, `write`, `edit`, `bash`, `glob`, `grep`). New capabilities go to Extended, MCP, plugin, or custom-tool tier.

## Execution Order

### Next (High Impact)

1. **B26** Praxis in chat composer -- P1. First TUI slice shipped (Tab cycling, worker sidebar). Needs deeper worker/task inspection, merge-back, session persistence.
2. **B73** Network proxy with SSRF protection -- P2. Managed proxy for agent outbound requests. Block private IPs, metadata endpoints, configurable deny-lists.
3. **B46** Plugin/skill marketplace -- P2. `ava plugin install <name>` for community tools, hooks, MCP configs, agent presets. Requires plugin runtime (v3.0 Phase 2).
4. **B79** Evaluation harness enhancement -- P3. SWE-bench integration, formal quality regression scoring.
5. **B80** Trajectory recording -- P3. Full agent decision tree as structured JSONL per session. Enables replay, pattern analysis, debugging.

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
