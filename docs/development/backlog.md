# AVA Backlog

> Last updated: 2026-03-17
> Related: [roadmap.md](roadmap.md), [epics.md](epics.md)

Tool surface policy: default tools stay capped at 6 (`read`, `write`, `edit`, `bash`, `glob`, `grep`). New capabilities go to Extended, MCP, plugin, or custom-tool tier.

## Recently Completed

- **Web mode** — `ava serve` with HTTP API + WebSocket, session CRUD, async agent streaming, auto-titling, mid-stream messaging endpoints, web DB fallback
- **Desktop parity** — Ctrl+T thinking toggle, Ctrl+Y copy response, 29 themes, `/later` and `/queue` slash commands wired
- **Default model** — changed from gpt-4 to gpt-5.3-codex
- **CI consolidation** — 11 jobs down to 4
- **Plugin system Phase 1** — `ava-plugin` crate, AgentStack wiring, TypeScript SDK, 4 examples, CLI commands. Smoke tested e2e.
- **Dead code cleanup** — 30 unwired modules → `docs/ideas/`, -10.5K LOC
- **Docs overhaul** — README, crate-map, plugin guide, changelog, CLAUDE.md/AGENTS.md

## Execution Order

### SOTA Critical (must ship for competitive parity)

1. **Agent tree branching** — build/plan/explore/general agent roles with routing. Each agent type has its own system prompt, tool subset, and behavior. OpenCode's killer feature.
2. **Wildcard permission patterns** — `*.env` → ask, `src/**/*.rs` → allow. Glob-based rules instead of literal paths. OpenCode has this.
3. **Per-agent model override** — each agent can use a different provider/model. Plan agent uses cheap model, code agent uses frontier. Pi has this.
4. **Message file attachments** — embed files in conversation messages (not just text). OpenCode's Message V2 system.

### SOTA Important (differentiation)

5. **Message revert system** — undo/revert specific tool call results. Roll back file changes from a single tool.
6. **Session todo tracking** — persistent todos scoped to sessions. Track what's done and pending.
7. **Thinking budget enforcement** — per-agent thinking token budgets. Control reasoning depth.
8. **Plugin hot-reload** — live code updates without restart.

### SOTA Gaps (from comprehensive competitive analysis)

> See full analysis in [sota-gap-analysis.md](sota-gap-analysis.md)
> Generated from 12 reference codebases (OpenCode, Cline, Aider, Continue, Codex CLI, Gemini CLI, Goose, OpenHands, Zed, Plandex, Pi Mono, SWE-Agent)

**Critical gaps (must have for parity):**

9. **StreamingDiff** — Apply edits AS LLM streams tokens (Zed). Reduces perceived latency.
10. **3-tier fuzzy matching upgrade** — Exact → line-trimmed → block-anchor cascade (Cline). Improves edit success rates.
11. **Progressive error escalation** — Context-aware guidance, forced strategy switches after failures (Cline).
12. **PageRank repo map** — Tree-sitter + networkx graph analysis for intelligent context (Aider). No competitor has this sophistication.
13. **Multi-strategy edit cascade** — 12 (strategy, preprocessing) combinations with git cherry-pick fallback (Aider).
14. **RelativeIndenter** — Unicode-based relative indentation encoding (Aider). Simple, high impact.
15. **9 condenser strategies** — Recent, LLM-summarize, Amortized, Observation-masking, Structured, Hybrid, Browser-turn, Identity, No-op (OpenHands).
16. **Event-sourced architecture** — Full event replay, time-travel debugging (OpenHands).
17. **5-scenario StuckDetector upgrade** — Repeated pairs/errors/monologues/alternating/context window (OpenHands).
18. **OS-level sandboxing** — Seatbelt (macOS), Landlock+seccomp (Linux), Windows restricted tokens (Codex CLI). Gold standard for security.
19. **Two-phase memory pipeline** — Phase 1 extraction + Phase 2 consolidation with usage-count prioritization (Codex CLI).
20. **Three-layer loop detection** — Tool hash (5x) + content chanting (10x) + LLM judge after 40 turns (Gemini CLI).
21. **Conseca dynamic policies** — LLM generates least-privilege policies, second LLM enforces per tool call (Gemini CLI).
22. **Event-driven parallel scheduler** — Formal state machine, batches read-only tools via Promise.all, serializes writes (Gemini CLI).
23. **MCP-first architecture alignment** — Extensions ARE MCP servers, no separate plugin API (Goose).
24. **3-layer inspection pipeline** — Security → Permission → Repetition inspectors (Goose).
25. **MOIM context injection** — Per-turn ephemeral context (timestamp, CWD, token usage) without polluting history (Goose).
26. **Cross-provider normalization** — Single-pass transformMessages() for thinking blocks, tool IDs, orphaned results (Pi Mono).
27. **Steering interrupts** — Skip pending tools via skipToolCall(), inject follow-up messages mid-stream (Pi Mono).
28. **Diff sandbox / review pipeline** — Server-side changes, explicit user approval before filesystem apply (Plandex).
29. **Concurrent build race** — 4 strategies compete (auto-apply → fast-apply → validation loop → whole-file), first valid wins (Plandex).
30. **Context providers system** — 30+ providers with unified IContextProvider interface (Continue).
31. **Tab autocomplete** — Inline edit suggestions with separate context pipeline (Continue).

**Important gaps (differentiation):**

32. **AI comment file watcher** — `# AI!` / `# AI?` comments trigger agent automatically (Aider).
33. **Architect/Editor model split** — Two-model workflow: planner describes, editor applies (Aider).
34. **SmartApprove** — LLM classifies tool as read-only to reduce unnecessary prompts (Goose).
35. **Lead-Worker model** — Expensive model first N turns, cheap model after with auto-fallback (Goose).
36. **Deleted-range truncation** — Novel context management preserving first user-assistant pair (Cline).
37. **Auto-formatting detection** — Detects and reports IDE auto-formatting changes between writes (Cline).
38. **Per-hunk accept/reject** — Granular change review for multi-file edits (Zed).
39. **MCP client + server** — Dual role enables agent marketplace (Zed).
40. **StreamingFuzzyMatcher** — Incremental Levenshtein matching as tokens arrive (Zed).
41. **Reindentation** — Adjusts indentation while streaming edits (Zed).
42. **Model packs with 9 roles** — Different models per role with fallback chains (Plandex).
43. **2M token context handling** — Model fallback chain (Claude → Gemini 2.5 Pro → Gemini Pro 1.5) (Plandex).
44. **Tree-sitter file maps** — Structural code summaries via tree-sitter (Plandex).
45. **Differential TUI rendering** — Synchronized ANSI output for flicker-free updates (Pi Mono).
46. **Terminal security evaluator** — 1,241-line shell command classifier (Continue).
47. **BrowserGym integration** — Research-grade browser automation (OpenHands).
48. **Security analyzer subsystem** — Three backends: Invariant, LLM, GraySwan (OpenHands).

**Nice-to-have:**

49. **Streaming diff progress bar** — Real-time `[██░░] XX%` during file generation (Aider).
50. **Voice input pipeline** — Whisper transcription with live audio levels (Aider).
51. **Recipe system expansion** — YAML workflows with sub-recipes, cron scheduling (Goose).
52. **Process hardening** — Anti-debug, anti-dump, env stripping (Codex CLI).
53. **Network proxy with SSRF protection** — Managed network with policy enforcement (Codex CLI).
54. **A2A protocol server** — Agent-to-Agent interoperability with discovery (Gemini CLI).
55. **1M token context handling** — Curated/comprehensive dual views (Gemini CLI).
56. **Mid-confirmation editing** — Edit tool arguments before approval (Gemini CLI).
57. **Tell/Build pipeline** — Two-phase planning and execution (Plandex).
58. **Plan branching** — Git-backed strategy branches (Plandex).
59. **BashArity** — Command prefix to arity mapping for permissions (OpenCode).
60. **15+ plugin hooks** — Extensive extension points for tool mutation (OpenCode).

### Next (High Impact)

9. **Plugin Phase 2** — `@ava-ai/plugin` npm publish, auth hook sub-protocol.
10. **Plugin Phase 3** — OpenCode compatibility bridge, plugin marketplace.
11. **B26** Praxis in chat composer — deeper worker/task inspection.
12. **B79** Evaluation harness — SWE-bench integration.
13. **B80** Trajectory recording — JSONL decision trees.

### Desktop/Web Frontend Fixes (Remaining)

6. ~~**Web mode: + button creates session but UI doesn't update**~~ — DONE (session CRUD API + frontend wired)
7. ~~**Web mode: async agent streaming**~~ — DONE (WebSocket events consumed by chat UI)
8. ~~**Desktop: Ctrl+T thinking toggle**~~ — DONE
9. ~~**Desktop: Ctrl+Y copy last response**~~ — DONE
10. ~~**Desktop: theme picker UI**~~ — DONE (29 themes via `/theme`)
11. ~~**Desktop: `/later` and `/queue` commands**~~ — DONE
12. ~~**Desktop: mid-stream messaging IPC**~~ — DONE (was already fully wired: Enter/Alt+Enter/Ctrl+Alt+Enter + queue UI)
13. ~~**Web mode: full DB operation parity**~~ — DONE (stub endpoints for agents/files/terminal/memory/checkpoints)

### Platform Verification

14. **TUI smoke test suite** — automated smoke tests for TUI mode.
15. **CLI headless regression** — verify all headless flags work.

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
