# AVA Backlog

> Last updated: 2026-03-23
> Related: [roadmap.md](roadmap.md), [epics.md](epics.md)
> SOTA gap research: [sota-gap-analysis.md](sota-gap-analysis.md) (60 items from 12 codebases — reference only, not a todo list)

Tool surface policy: default tools are now 9 (`read`, `write`, `edit`, `bash`, `glob`, `grep`, `web_fetch`, `web_search`, `git_read`). New capabilities go to Extended (plugin), MCP, or custom-tool tier. Extended tools are NOT auto-registered.

## Recently Completed

- **22 LLM providers** — added Azure OpenAI, AWS Bedrock, xAI, Mistral, Groq, DeepSeek, ChatGPT alias (was 15)
- **Mid-stream messaging refactor** — renamed to Queue/Interrupt/Post-complete; new keybindings (Enter=queue, Ctrl+Enter=interrupt, Alt+Enter=post-complete, Double-Escape=cancel); MessageQueueWidget with reorder/edit/remove
- **Context overflow auto-compact** — 12 overflow patterns detected, agent loop auto-compacts and retries
- **Shadow git snapshots** — file_snapshot.rs creates git snapshots before edits, enabling revert_file
- **100+ security patterns** — command classifier rules.rs (728 LOC), symlink escape detection in path guard
- **Incremental message persistence** — messages saved as they arrive, session context preserved across cancel/continue
- **Retry-after header parsing** — retry.rs extracts and respects Retry-After headers from providers
- **Quota error classification** — typed error variants for rate limits, quota exceeded
- **Conversation repair** — typed error recovery for malformed conversation state
- **Edit-and-resend fix** — properly deletes old messages when editing and resending
- **Subagent stream error fix** — subagent streaming errors no longer crash parent agent
- **Web mode session persistence** — frontend→backend session ID mapping, UUID v4 message IDs, backend-only persistence model, WebSocket reuse, PUT endpoint for message updates
- **MCP reliability** — NDJSON framing fix, race condition fix (await init), lazy init with 30s timeout, tool name underscores for OpenAI compat
- **Plugin hooks complete** — all 23 plugin hooks wired into agent runtime (full OpenCode parity), MCP HTTP transport, plugin settings UI
- **Web UI polish** — todo panel, smooth transitions, diff browser, 47 Playwright e2e tests, CSS audit
- **Streaming fixes** — tool grouping, thinking scroll/duplicate fix, assistant message content persistence on restore
- **Tool surface refactor** — 9 default tools (was 6); `web_fetch`, `web_search`, `git_read` promoted; Extended tools no longer auto-registered
- **Security audit round 1-6** — SBPL injection, env scrubbing, rm -rf hardening, find -delete blocking, regex compile safety
- **Performance audit** — blocking I/O → async, trust caching, connection pooling, ToolCall clone elimination, CodebaseIndex sharing
- **Error handling** — `From<io::Error>` preserves ErrorKind; typed AvaError variants; deprecated `Other`/`Internal` variants
- **Test coverage** — 1,895+ tests (was 1,798); includes Azure/Bedrock crypto tests, overflow patterns, security classifier rules
- **Web mode endpoint parity** — 14 new endpoints for desktop↔web feature parity
- **Frontend audit** — debug logs removed, dead code cleaned, async prop fixed
- **`--verbose` CLI flag** — `-v` info, `-vv` debug, `-vvv` trace to stderr
- **JSONL session logging** — structured logs at `~/.ava/log/` (opt-in via `features.session_logging: true`)
- **Ellipsis edit strategy** — `...` placeholder handling; 15 total strategies (was 14)
- **Rich edit error feedback** — similar lines + "did you mean?" hints on edit failure
- **OpenAI OAuth account fallback** — derive `ChatGPT-Account-ID` from JWT claims for ChatGPT/Codex requests when credentials lack a stored account id
- **God file splits** — praxis/lib.rs -> 5 files, stack.rs -> 4 files, agent_commands.rs -> 3 files
- **Security fixes** — 6 production unwraps replaced, extension test fix
- **Quality fixes** — 0 clippy warnings, 0 dead code, nextest 6 threads, `just check` tests 3 core crates
- **Praxis v2 Phases 1-6** — LLM-powered Director (3 intelligence levels), scout system, Board of Directors (multi-model consensus), plan tool with PlanBridge, structured events + Tauri bridge, 91 tests
- **Enhancement Batch 3** — edit reliability cascade (15 strategies: ellipsis + 3-way merge + diff-match-patch), persistent audit log (SQLite, opt-out, session/tool queries)
- **Enhancement Batch 2** — Anthropic prompt caching (cache_control on system + tools, ~25% cost savings), auto-retry middleware for read-only tools (2x, exponential backoff), tiktoken-rs accurate BPE token counting
- **Enhancement Batch 1** — tool schema pre-validation (catches malformed calls before execution), stream silence timeout (90s configurable, per-chunk reset), auto-compaction toggle + threshold slider in Settings
- **Web mode** — `ava serve` with HTTP API + WebSocket, session CRUD, async agent streaming, auto-titling, mid-stream messaging endpoints, web DB fallback
- **Desktop parity** — Ctrl+T thinking toggle, Ctrl+Y copy response, 29 themes, `/later` and `/queue` slash commands, mid-stream messaging IPC
- **Plugin system Phase 1** — `ava-plugin` crate, AgentStack wiring, TypeScript + Python SDKs, 4 examples, CLI commands, auth hooks
- **Dead code cleanup** — 30 unwired modules → `docs/ideas/`, -10.5K LOC
- **Docs overhaul** — README, crate-map, plugin guide, changelog, codebase docs, SOTA gap analysis
- **DX cleanup pass** — CI/release workflows aligned on pnpm + rust-toolchain, plugin workspace build script restored, stale npm lockfile and Playwright artifact tracking removed, helper scripts updated for the Rust-first workflow
- **CLI benchmark harness** — reproducible AVA-vs-OpenCode startup and matched-task benchmark script with JSON/Markdown output under `.tmp/benchmarks/`
- **Copilot verification cleanup** — `auth test copilot` now recognizes OAuth credentials, installed AVA binary was refreshed from the current repo build, and benchmark reporting is more useful for flaky external-provider runs
- **Fast-path benchmark controls** — AVA `--fast` now isolates prompt/indexing overhead during headless benchmarking, and runtime project instruction loading no longer pulls in `CLAUDE.md`
- **Hybrid instruction loading** — startup prompt now stays AGENTS-first while `.ava/rules` load on demand after touched-file detection, reducing simple-task prompt bloat
- **Second lean-runtime pass** — on-demand rules now also wake up from search results, prompt token telemetry is logged, and startup hot paths are tighter around plugin/memory overhead
- **Model-aware lean prompts** — frontier/reasoning models now use a shorter native-tool prompt profile so benchmark runs spend less time on repeated workflow prose

## Execution Order

### Next Sprint (high impact, aligned with vision)

1. **Wildcard permission patterns** — `*.env` → ask, `src/**/*.rs` → allow. Glob-based rules. Simple, high UX impact.
2. **Per-agent model override** — each agent uses different provider/model. Already half-built in agents.toml config.
3. ~~**Fuzzy matching upgrade**~~ — DONE: edit reliability cascade now has 15 strategies including ellipsis handling, 3-way merge + diff-match-patch.
4. **StreamingDiff** — apply edits as tokens stream. Users see changes instantly instead of waiting.

### Desktop App Bugs (from testing)

5. ~~**Duplicated response text**~~ — DONE: fixed by tool grouping in streaming + duplicate display fix (v2.2.5).
6. **Cost showing for OAuth** — $0.04 displayed for ChatGPT subscription users. Backend returns 0 but frontend may use registry pricing.
7. ~~**Thinking content not displayed**~~ — DONE: thinking markdown rendering, scroll, and duplicate display fixed (v2.2.5).

### After That

5. **Plugin Phase 2** — `@ava-ai/plugin` npm publish, auth hook sub-protocol.
6. **Plugin Phase 3** — OpenCode compatibility bridge, plugin marketplace.
7. **Message revert** — undo specific tool call results. Important for trust.
8. **Shared daemon for multi-project** — Single background process owns expensive shared resources (LLM connections, credential store, model registry, global memory). Each project gets a lightweight agent context (codebase index, MCP, session, permissions). Reduces RAM from ~30MB×N to ~30MB+5MB×N for N projects. Lock file at `~/.ava/daemon.lock` with `{ pid, port }`. CLI/desktop detects running daemon and connects instead of spawning new process. Similar to Docker daemon / VS Code server architecture.

### Release MVP (do once product is ready)

9. **Tauri code signing** — Run `npx tauri signer generate -w ~/.tauri/ava.key`, add public key to `tauri.conf.json` `plugins.updater.pubkey`, add `TAURI_SIGNING_PRIVATE_KEY` + `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` as GitHub Actions secrets. Required for desktop auto-updates to work.
10. **First GitHub Release** — Tag `v2.2.0`, push tag to trigger release workflow. Produces CLI binaries (5 targets), desktop installers (.deb/.AppImage/.dmg/.msi), and `latest.json` for the Tauri updater. After this, `curl | sh` installer and `ava update` work.
11. **Homebrew formula** — `brew install ava` for macOS users. Submit to homebrew-core or host a tap at `ASF-GROUP/homebrew-ava`.
12. **npm wrapper** — `npm install -g @ava-ai/cli` that downloads the prebuilt binary. No Node runtime needed at execution time.

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

24. **Seccomp sandboxing** — seccomp-bpf sandbox profile for Linux (research needed: policy design, syscall whitelist, integration with ava-sandbox).
25. **WebSocket prewarming** — persistent WebSocket connections for remote agent transport (research needed: reconnection strategy, multiplexing, auth).
26. **`ava doctor` command** — diagnostic command to check provider connectivity, config validity, tool health (not yet implemented).
27. **Summary chaining research** — explore chaining compaction summaries across sessions for long-running projects.
28. **Competitor logging research** — ~~analyze Claude Code / Cursor / Devin logging approaches~~ partially addressed by JSONL session logging; remaining: telemetry dashboard.

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
