# AVA Backlog

> Last updated: 2026-03-19
> Related: [roadmap.md](roadmap.md), [epics.md](epics.md)
> SOTA gap research: [sota-gap-analysis.md](sota-gap-analysis.md) (60 items from 12 codebases — reference only, not a todo list)

Tool surface policy: default tools stay capped at 6 (`read`, `write`, `edit`, `bash`, `glob`, `grep`). New capabilities go to Extended, MCP, plugin, or custom-tool tier.

## Recently Completed

- **Praxis v2 Phases 1-6** — LLM-powered Director (3 intelligence levels), scout system, Board of Directors (multi-model consensus), plan tool with PlanBridge, structured events + Tauri bridge, 91 tests
- **Enhancement Batch 3** — edit reliability cascade (14 strategies: 3-way merge + diff-match-patch), persistent audit log (SQLite, opt-out, session/tool queries)
- **Enhancement Batch 2** — Anthropic prompt caching (cache_control on system + tools, ~25% cost savings), auto-retry middleware for read-only tools (2x, exponential backoff), tiktoken-rs accurate BPE token counting
- **Enhancement Batch 1** — tool schema pre-validation (catches malformed calls before execution), stream silence timeout (90s configurable, per-chunk reset), auto-compaction toggle + threshold slider in Settings
- **Web mode** — `ava serve` with HTTP API + WebSocket, session CRUD, async agent streaming, auto-titling, mid-stream messaging endpoints, web DB fallback
- **Desktop parity** — Ctrl+T thinking toggle, Ctrl+Y copy response, 29 themes, `/later` and `/queue` slash commands, mid-stream messaging IPC
- **Plugin system Phase 1** — `ava-plugin` crate, AgentStack wiring, TypeScript + Python SDKs, 4 examples, CLI commands, auth hooks
- **Dead code cleanup** — 30 unwired modules → `docs/ideas/`, -10.5K LOC
- **Docs overhaul** — README, crate-map, plugin guide, changelog, codebase docs, SOTA gap analysis

## Execution Order

### Next Sprint (high impact, aligned with vision)

1. **Wildcard permission patterns** — `*.env` → ask, `src/**/*.rs` → allow. Glob-based rules. Simple, high UX impact.
2. **Per-agent model override** — each agent uses different provider/model. Already half-built in agents.toml config.
3. ~~**Fuzzy matching upgrade**~~ — DONE: edit reliability cascade now has 14 strategies including 3-way merge + diff-match-patch.
4. **StreamingDiff** — apply edits as tokens stream. Users see changes instantly instead of waiting.

### Desktop App Bugs (from testing)

5. **Duplicated response text** — assistant messages show content twice in the chat. Likely the streaming content + final message both render.
6. **Cost showing for OAuth** — $0.04 displayed for ChatGPT subscription users. Backend returns 0 but frontend may use registry pricing.
7. **Thinking content not displayed** — thinking badge shows "Med" but thinking blocks don't render as collapsible sections like TUI. Content may be concatenated with response.

### After That

5. **Plugin Phase 2** — `@ava-ai/plugin` npm publish, auth hook sub-protocol.
6. **Plugin Phase 3** — OpenCode compatibility bridge, plugin marketplace.
7. **Message revert** — undo specific tool call results. Important for trust.

### Platform Verification

8. **TUI smoke test suite** — automated smoke tests for TUI mode.
9. **CLI headless regression** — verify all headless flags work.

### Praxis Frontend Wiring (backend Phases 1-6 complete, frontend wiring remaining)

15. **Wire Tauri IPC commands for Praxis** — `start_delegation`, `get_praxis_status`, `cancel_praxis` commands in `src-tauri/src/commands/`.
16. **Connect Team button to team mode activation** — status bar Team button triggers `start_delegation`, switches UI to team layout.
17. **Wire TeamPanel to live PraxisEvent stream** — connect to live event stream via agent-team-bridge (store wired, events not flowing yet).
18. **Wire TeamMetrics/DelegationLog/WorkerDetail into right panel** — populate metrics footer (tokens, files, cost, success rate) and worker detail views.
19. **Handle 12 currently-dropped Praxis events in TUI** — workflow, spec, artifact, conflict, ACP events.
20. **Wire worktree creation per lead** — git worktree lifecycle management during team mode.
21. **Implement Merge Worker** — integration worker that merges lead worktrees, resolves conflicts.
22. **Lead question relay through Director chat** — leads surface questions as colored border cards in Director chat; user answers relay back.
23. **Solo/Team mode switching** — full lifecycle: Solo -> Team (plan + spawn), Team -> Solo (stop all + collapse), Resume Team (review + replan).

### Research Items

24. **Batch 4: Security & Transport** — seccomp-bpf sandbox profile for Linux, WebSocket transport for remote agent communication.
25. **Summary chaining research** — explore chaining compaction summaries across sessions for long-running projects.
26. **Competitor logging research** — analyze Claude Code / Cursor / Devin logging and telemetry approaches for improving debug observability.

### Later (when users ask for it)

10. **Agent tree branching** — build/plan/explore agent roles. Big feature, do when multi-agent is a user priority.
11. **Message file attachments** — embed files in conversation messages.
12. **Session todo tracking** — persistent todos scoped to sessions.
13. **Session templates** — save conversation patterns as reusable templates.
14. **Custom keybindings** — user-definable keybindings in `~/.ava/keybindings.json`.

### Plugin/Extended Only (do not expand default surface)

- **B68** Batch tool — Extended tier. Parallel tool execution.
- **B75** Directory listing tool — Extended tier. Tree-view respecting .gitignore.
- **B55** Security scanning — Plugin. semgrep/cargo-audit.
- **B56** Test generation — Plugin. Edge case detection.
- **B72** Browser automation — Plugin/MCP. Web page interaction.
- **B77** PR checkout workflow — Plugin. `/pr <number>` via gh CLI.

## Implemented (Pending Manual Testing)

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
