# Changelog

All notable changes to AVA are documented in this file.

## [Unreleased]

### Added
- HQ desktop backend now has real SQLite-backed orchestration state: epics, issues, comments, plans, agents, activity feed, director chat, and HQ settings all load through new Tauri commands instead of frontend mock data.
- Desktop chat now has a real `/compact` slash command, structured context-summary cards, configurable auto-compaction settings (toggle, threshold, compaction model), and Tauri/Rust session compaction wired to the agent context pipeline instead of the old frontend-only sliding-window stub.

### Removed
- **Deleted `packages/` directory entirely.** The TypeScript desktop layer (`packages/core-v2/`, `packages/extensions/`, `packages/core/`, `packages/platform-node/`, `packages/platform-tauri/`) has been removed. The desktop app now calls Rust crates directly via Tauri IPC commands (`src-tauri/src/commands/`), eliminating the `dispatchCompute` bridge pattern and all Node.js runtime dependencies from the desktop path.

### Changed
- HQ plan persistence/execution is more faithful and robust: replanning an epic now replaces its generated issue set instead of duplicating stale tasks, persisted HQ plan tasks now carry dependency and budget metadata through approval into runtime execution, HQ status reports now return real worker counts from persisted state, runtime issue matching no longer silently swallows database lookup failures during worker event ingestion, invalid intra-phase dependencies are now blocked before execution, and first-run HQ memory bootstrap now has a reusable `.ava/HQ/` generator shared across desktop onboarding, web/API, and the headless `ava hq init` CLI path.
- HQ's simplified product shell is now live in the desktop/frontend path: the old multi-screen HQ navigation was collapsed into Director Chat, Overview, Team Office, and HQ-native Plan Review surfaces, the shared sidebar now carries mission/team/metrics state across all views, Director Chat shows real HQ summary/status/memory cards above the normal AVA chat stream, and the Team Office inspector now lets users inspect live worker state without dropping into the old agent-detail flow.
- Desktop frontend audit follow-up tightened the shared desktop surface tokens: title bar, splash/error screens, sidebar, inspector, dashboard, settings/dialog primitives, and key chat/subagent surfaces now lean on semantic CSS variables instead of fixed hex values, dashboard cards now show real session/token/file/cost data instead of placeholders, chat watcher lifecycles stop cleanly before rebinding, and modal/menu accessibility wiring is more consistent.
- Desktop chat polish went deeper on the main Tauri/Solid conversation screen: shared chat surfaces now expose a more accessible `role="log"` message stream, session switches restore composer focus, idle Escape cleanly blurs the composer, scroll-to-bottom/navigation uses smoother motion-aware scrolling, tool/thinking/error cards rely more heavily on semantic tokens instead of inline hexes, and the message/composer/tool shells add stronger containment so long streaming conversations feel more native on WebKitGTK/macOS-style desktops.
- Follow-up desktop polish cleaned the last stray frontend lint regression in onboarding, tightened the chat title bar + team strip token usage, and aligned a few remaining chat/header surfaces with the Pencil spacing/color language without reintroducing broad transitions.
- A broader follow-up token sweep removed more chat-surface hardcoded colors from grouped tool cards, subagent cards, streaming activity wrappers, and team follow-up inputs so the remaining desktop chat stack inherits theme/high-contrast behavior more consistently.
- Desktop chat scrolling and streaming are smoother on WebKitGTK-class desktops: the main message scroller no longer rebonds nested scroll listeners on every subtree mutation, several chat/status progress bars now animate with `transform: scaleX(...)` instead of width changes, and high-frequency chat transitions were narrowed away from `transition-all` so fast scroll and streaming updates trigger less layout and paint work.
- Frontend hover/streaming polish is lighter on constrained compositing paths: `hover:brightness-110` button patterns now resolve to explicit hover colors instead of filter passes, streaming shimmer text in chat/tool headers was converted to static emphasis, pulsing chat/loading affordances were softened, and markdown/tool-output surfaces now avoid unnecessary post-render work while streaming plus use additional containment so large responses scroll more cleanly.
- Additional list/panel cleanup removed the remaining `transition-all` usage across onboarding, settings, project/panel cards, and progress indicators, converted more repeated list-item accent styling onto CSS variables, and replaced the last width-based frontend progress bars with transform-driven fills.
- HQ model picking and Director chat are now aligned with the main desktop chat flow: HQ settings, Team routing, agent overrides, and Director chat all use the shared model-browser modal instead of ad-hoc selects, Director chat now says "message" instead of "steer", exposes slash-command autocomplete in the composer, and both normal chat and HQ Director chat now share the same extracted `ChatSurface`, `ChatMessageStream`, and `ChatComposer` presentation layers while still keeping separate runtime controllers behind them.
- Desktop settings interactions are smoother and more premium: settings persistence now batches localStorage/FS/core sync work instead of doing a full write on every tweak, the settings modal uses a single primary content scroll container so scrolling feels less sticky and nested scroll behavior is less awkward, the split-pane Agents/HQ tabs now keep their own list/detail scrollers contained instead of fighting the modal scroll, tab switches now reset scroll position cleanly with richer sidebar/content motion, and the main settings content pane now uses the same native overflow/scrollbar behavior as the smooth chat sidebar instead of the custom wheel interpolation experiment.
- Backend persistence/cancel paths are harder to break: HQ artifact files now flush through temp-file atomic replacement, quarantine corrupt JSON on reopen instead of crashing the workflow state, session migrations only ignore truly already-applied SQLite schema changes while failing loudly on real migration errors, and ACP stdio subprocesses now drain stderr plus wait on real kill attempts instead of silently skipping cancellation when the child lock is busy.
- Desktop/backend session access is more robust under load: Tauri session CRUD and checkpoint saves now run through `spawn_blocking` instead of doing synchronous SQLite work directly on async runtime threads, session checkpoint tests now cover restart recovery, MCP server requests now fail with bounded per-call timeouts instead of hanging forever, and plugin hook chains no longer fan out each subscriber repeatedly when applying tool-definition or message-transform hooks.
- Agent/runtime consistency is tighter: duplicate-request suppression now uses one shared hash path instead of diverging heuristics between completion and response code, new MCP/plugin fault-injection tests cover unresponsive transports and timed-out hooks, and the touched Rust paths are clean against the targeted clippy warnings from this pass.
- Follow-up reliability cleanup landed too: `ava-llm` is back to workspace fmt-clean, and the TUI startup/memory benchmark now measures `AgentStack` overhead in a deterministic benchmark configuration instead of inheriting unrelated environment startup cost from CLI-agent discovery or prior test-process memory.
- HQ desktop/browser UX is less confusing and more reliable: Director chat now accepts Enter-to-send and kicks off work when idle in the Tauri flow, HQ agent/chat state now seeds the Director so the org chart is never empty on first open, the chat session sidebar stays collapsed while HQ is active, HQ settings live under a dedicated settings category without the duplicate Team tab, and browser-mode HQ now has matching `/api/hq/*` endpoints so Playwright/web verification no longer crashes on missing API routes.
- HQ Director chat now behaves like a real assistant path instead of canned placeholder copy: stale fake kickoff messages are purged from stored chat history, new Director messages trigger actual HQ runs in both Tauri and web mode, delayed replies auto-refresh back into the UI, HQ now has an explicit `Back to Chat` exit path, and the New Epic modal uses a calmer layered surface so it reads like a proper dialog instead of a washed-out overlay.
- HQ docs now match the shipped product: `docs/hq/README.md` documents the live SQLite/Tauri/web-backed HQ architecture, clarifies that Director chat reuses the normal AVA chat renderer/composer with streaming/thinking/tool surfaces, notes the Back to Chat/sidebar/settings cleanup, and `docs/README.md` now links HQ as a first-class doc entry.
- HQ desktop polish is now fully live-data driven: the sidebar shows the active workspace name, HQ modal actions route through the shared store, and the dashboard's cost surface now uses exact PAYG worker telemetry when HQ runtimes report real spend, while subscription-style runs stay excluded instead of showing a fake placeholder.
- HQ agent customization is now real in the desktop app: built-in HQ presets sync provider/model/system-prompt overrides into the Rust config, new HQ runs honor Commander and domain-lead overrides, and team lead custom prompts are forwarded correctly instead of being dropped on the desktop side.
- HQ now has a real dedicated settings surface: the HQ tab is rendered in the modal, includes HQ-only runtime routing, lead execution, worker-pool, and HQ-agent override controls, and backlog ownership now lives in `docs/backlog.md` after removing the old `.swarm/` tree.
- External agent orchestration is more production-ready: task analysis now uses one shared routing/delegation/tool-visibility pass, hidden subagents can run through configured ACP-backed runtimes, and Claude Code/Codex/OpenCode wrappers now launch with tool-specific non-interactive commands, stronger env scrubbing, richer JSONL parsing, CLI-prefixed provider routing, stale-session retry for Codex/OpenCode resumes, safer interrupt/cancel bookkeeping, typed delegation/external-session metadata, persisted structured external message blocks, benchmark-visible resume/provider analytics for delegated runs, closed-loop delegation quality scoring, adaptive runtime delegation tuning from recent outcomes, and timeout-backed external worker handling. The TUI/web layer now also surfaces provider/resume/cost/token metadata for subagents and shows delegation summaries in session lists and exports.
- Provider/runtime robustness is much stronger: streaming decoders now preserve split UTF-8 and NDJSON chunks instead of silently dropping them, provider errors surface clearer model-not-found and context-window diagnostics, OpenAI/Anthropic-compatible aliases keep their real provider labels, and Azure OpenAI plus Bedrock are now first-class provider factory targets. A second SOTA pass also adds safer OpenAI-compatible tool-call defaults (`tool_choice=auto`, explicit parallel tool calls), OpenRouter provider-routing constraints that disable silent fallback and require supported parameters, and recursive request-default merging so proxy-specific safety policies apply consistently. The coding harness also adds tighter provider-specific prompt notes for OpenAI-style, Anthropic-style, Gemini, OpenRouter, Copilot, and Ollama/local models, while GPT-5/Codex Responses requests default to low verbosity for more reliable coding-agent behavior.
- OpenAI Responses handling is stricter and more durable: streamed `response.output_item.done` function calls now keep their final argument payloads for Codex-class models, repeated complete argument payloads are deduped safely, and history replay now preserves message/tool-result order with explicit Responses `message` items, image parts, and placeholder `function_call_output` fallbacks. This fixes recurring TUI failures where `glob`/`read` arrived as `{}` and follow-up turns hit 400 "No tool output found for function call ..." errors.
- Workspace reliability checks are cleaner end-to-end: additional mechanical test and utility issues are cleaned up across MCP, TUI, context/token tracking, permissions parsing, and config trust handling, reducing remaining clippy/test friction beyond the provider layer.
- Desktop frontend (SolidJS) retained in `src/` but all backend logic is now pure Rust via `src-tauri/`.
- The `dispatchCompute` pattern is no longer present anywhere in the codebase (previously deprecated for new work, now fully removed).
- Agent/tool hot paths are more robust: streamed tool calls preserve provider ordering, read-only tool fan-out is bounded, transient retries stop once failures turn permanent, context compaction tracks the latest summary, and in-memory SQLite pools now reuse a single connection safely.
- Instruction and delegation DX are sharper: `.ava/rules` now activate only on direct file touches and dedupe for the session, contextual `AGENTS.md` guidance reloads after compaction instead of spamming every read, hidden subagents only appear on broader tasks with explicit spawn budgets, and scout/plan/review helpers now run in enforced read-only specialist lanes.
- Default tool runtime is leaner and more diagnosable: `grep` walks repos in parallel with deterministic ordering, `glob` avoids per-match metadata calls, missing-file errors suggest sibling paths, and edit failures now report similarity hints plus already-applied detection.
- Custom TOML tools now execute with bash-style env scrubbing, plugin-provided `shell.env` variables, structured stdout/stderr/exit metadata, secret redaction, and disk spillover for oversized output. DuckDuckGo search results now validate redirects, unwrap real target URLs, decode HTML entities, and drop blocked or duplicate results.
- Web fetches and recovery state are more resilient: `web_fetch` now enforces redirect safety as hard failures and streams response bodies under a byte cap instead of buffering arbitrarily large payloads, while file backup version discovery tolerates gaps and shadow snapshots ignore inherited `GIT_*` environment contamination.
- The docs tree is now much leaner: `docs/README.md` points at the small set of live references, stale duplicate docs were removed, and `CLAUDE.md` now points at the current doc locations.
- The TUI is sturdier on long sessions and richer diff output: side-by-side tool diffs now pair multi-line replacements correctly, diffs without summary prefixes still render, message scrolling no longer truncates after 65k visual lines, and the message list avoids an extra full-vector drain on every frame.
- The desktop frontend now tracks tool executions more reliably: Tauri emits real tool call IDs and approval events carry the originating tool call, so out-of-order tool results and repeated approvals no longer update the wrong card. The browser API bridge also unwraps GET args correctly, snake-cases query params, and avoids duplicating path params into the query string.
- Desktop streaming is more resilient across reconnects and long trajectories: stale WebSocket callbacks are ignored after reconnect, timeline events get stable timestamps at ingest time instead of being re-timestamped on every render, and targeted browser-mode tests now cover reconnect and query-param edge cases.
- Desktop state now behaves better over long runs and reloads: streaming event history is bounded instead of growing forever, and session-level window listeners are installed through a replaceable binding so hot reloads do not stack duplicate listeners.
- Desktop session state is safer under rapid switching and reloads: stale async session loads are ignored instead of overwriting the active session, and global `instructions-loaded` / `core-settings-changed` listeners now use replaceable bindings so hot reloads do not duplicate handlers.
- Desktop polish tightened further: remaining instruction/diagnostic listeners now clean up predictably, deep-link plugin cleanup is safe even if initialization resolves late, and chat/tool cards share a single elapsed-time ticker instead of spinning one interval per running card.
- Desktop chat surfaces are sturdier under long use: nested scrollable tool outputs are tracked incrementally instead of re-binding listeners to the whole subtree on every mutation, near-top backfill no longer re-triggers repeatedly for the same hidden-count window, and the focus-chain store now uses a replaceable global listener instead of per-mount subscriptions.
- Desktop session transitions now clear and repopulate the whole per-session side state consistently: creating or auto-switching sessions no longer leaves stale agents/files/checkpoints behind, and deep-link-driven lazy imports now fail through explicit logging instead of silent unhandled promise chains.
- Desktop UI polish is tighter and lint-cleaner: right-panel/settings tab lists now render with Solid's keyed list primitive instead of `.map()`, tool/focus UI surfaces no longer rely on non-reactive early returns, terminal ANSI parsing avoids control-regex lint traps, and message metadata persistence no longer carries avoidable object-spread fallbacks.
- Core chat surfaces shed more reactive footguns: queue/team/panel click handlers now use tracked wrappers, error and interleaved-thinking rows no longer rely on early-return rendering shortcuts, and group headers initialize from tracked state instead of one-shot prop reads.
- The desktop/frontend lint pass is now effectively clean: high-traffic dialogs, chat controls, onboarding, settings, panel surfaces, and the remaining plugin example warning have all been scrubbed down so `pnpm lint` completes without frontend lint findings.
- Benchmark infrastructure is cleaner and deeper: shared support helpers are now split into dedicated workspace and validation modules, benchmark runs track hidden subagent usage/cost, the CLI now supports `--task-filter` for one-off benchmark slices, benchmark-mode question prompts get a deterministic fallback answer, the tables print delegation details prominently, and new scenarios cover file-scoped rule following plus delegation-heavy multi-file debugging.

## [2.1.0] - 2026-03-08

Release polish, documentation, and E2E test matrix.

### Added
- E2E test matrix (`docs/development/test-matrix.md`) — 19 tools, 5 modes, 3 providers verified
- Version badge and smoke test command in README

### Changed
- Workspace version bumped to 2.1.0
- README refreshed with current architecture and test verification line
- docs/README.md updated with test-matrix link and corrected sprint references
- CLAUDE.md and AGENTS.md verified and updated with v2.1 version reference

## [2.0.0] - 2026-03-05

### Breaking Changes
- Backend runtime finalized on `core-v2` + extension architecture; legacy `packages/core/` reduced to compatibility shim behavior.
- Tool surface reduced from roughly 55 to ~39 tools.
- Extension surface reduced from 37 to 20 built-in modules.

### Performance
- Edit cascade reliability improved to target >85% first-pass replacement success.
- Streaming UX latency reduced for sub-500ms incremental updates.
- Startup path tuned for <1s warm-start target on desktop.
- Rust hotpaths expanded for grep, edit, validation, permissions, memory, and sandbox routing.

### Added
- Hybrid v2 architecture finalized around `core-v2` + extension-first runtime.
- Rust hotpath dispatch pattern (`dispatchCompute`) wired across compute, context, safety, and validation paths.
- Multi-tier edit cascade and streaming edit parsing support.
- Context intelligence upgrades: repo-map ranking, tiered compaction thresholds, post-edit diagnostics middleware.
- Agent reliability middleware for stuck-loop detection, recovery retries, and completion validation.
- Sandbox policy routing and checkpoint refs under `refs/ava/checkpoints/`.
- Desktop UX upgrades including token streaming debounce and hunk-level diff review controls.
- PageRank-style repository mapping for context prioritization.
- Dynamic permission learning with dangerous-command generalization safeguards.
- Git checkpoint references with pre-destructive operation capture.
- MCP plugin support with namespaced tool execution paths.
- 3-tier compaction strategy for long-running conversations.

### Changed
- Plugin SDK examples aligned with exported `core-v2` APIs and middleware return contracts.
- E2E harness defaults to real backend unless explicitly mocked.
- Documentation updated to current hybrid architecture and v2 release state.

### Fixed
- Known flaky tests in chat integration and extension loader suites.
- Permission learning safeguards to prevent dangerous command over-generalization.

### Removed
- Migration-era backlog and architecture docs no longer relevant to v2 release.
