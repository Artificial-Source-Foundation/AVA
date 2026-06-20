# AVA MVP Baseline

This is the living product baseline for getting AVA to a practical MVP. It is distinct from the historical `1.0.0` backend release-position docs: here, MVP means the current product target we can keep working against.

Reference repositories are local behavior references only:

- Pi primary baseline: `docs/reference-code/pi/`, cloned from `https://github.com/earendil-works/pi-mono.git`. The coding agent package is `docs/reference-code/pi/packages/coding-agent/`.
- OpenCode secondary baseline: `docs/reference-code/opencode/`, cloned from `https://github.com/anomalyco/opencode.git`.

Do not copy reference source code or architecture into AVA. Borrow capability shape, then implement with narrow C++23 modules, explicit permissions, inspectable local files, and backend/frontend boundaries that match `AGENTS.md`.

## Baseline Rule

Pi defines core parity. If Pi treats a capability as part of the coding-agent loop, AVA should implement a lean C++23 equivalent or explicitly document why the product decision excludes it from AVA's MVP.

OpenCode is the secondary source. It has useful mature-agent ideas, but its desktop, web, SaaS, package, telemetry, and broad platform surfaces are not MVP requirements for AVA unless a separate product decision pulls one in.

## Current AVA State

| Area | Pi baseline | AVA current state | MVP disposition |
| --- | --- | --- | --- |
| Native/local agent | Pi is a local terminal coding agent. | AVA is a native C++23 terminal-first agent with TUI, line-shell fallback, print mode, and JSONL RPC. | Present. Direct positional one-shot prompts and `--no-session` remain optional follow-up. |
| Provider/model abstraction | Pi has a unified provider API, model metadata, env auth, OAuth, custom models, and provider compatibility settings. | AVA has OpenAI, Anthropic, Kimi, Moonshot, and OpenRouter-compatible paths; model metadata includes capabilities, pricing, modalities, context windows, reasoning controls, and API-family validation. | Present with breadth gaps. Keep OpenAI excellent, keep Kimi as validated non-OpenAI path, and live-smoke Anthropic/Moonshot/OpenRouter when credentials allow. |
| Auth | Pi supports API keys and OAuth across selected providers. | AVA supports OpenAI browser/device OAuth and API keys; provider-scoped API keys; Anthropic OAuth bearer resolution/refresh for stored or env tokens. | Partial. Anthropic interactive OAuth and provider-specific auth chains for Google/Copilot/Bedrock/Vertex/Azure are later scoped decisions. |
| Agent loop/events | Pi streams agent/turn/message/tool lifecycle events and supports steering/follow-up. | AVA has provider-neutral runtime envelopes, streaming assistant/reasoning/tool events, retry/compaction/cancel events, RPC steering, follow-up queues, and cooperative cancellation. | Present. Keep event schemas stable and add tests when fields change. |
| Built-in tools | Pi core tools are read/write/edit/bash/grep/find/ls, with strong validation and bounded output. | AVA model-visible built-ins are `read_file`, `list_directory`, `write_file`, `edit_file`, `apply_patch`, `glob`, `grep`, `bash`, `webfetch`, `websearch`, `skill`, `question`, and capability-gated LSP tools. | Present. Keep fewer tools with better semantics; avoid adding tools without permission, audit, cancellation, and output-bound design. |
| Permissions | Pi intentionally avoids built-in permission popups; OpenCode has structured allow/deny/ask rules. | AVA has backend allow/ask/deny policy, TUI/RPC resolvers, headless allow flags, session grants, durable permission rules, protected rule files, TUI remembered prompt rules, and permission audit entries. | Present and stronger than Pi. Remaining work is UX, diagnostics, broader categories, and security review for any new side-effect class. |
| Sessions | Pi stores append-only JSONL sessions with tree/fork/clone/compact/export workflows. | AVA has append-only JSONL sessions, resume/list/export/stats, compaction, usage/cost records, model/reasoning entries, metadata, labels/names, archive state, tree inspection, fork, clone, and caller-supplied branch summaries over RPC. The TUI/session selector and `/sessions`/`/tree` output expose tree metadata, selector sorting, named-session filtering, path-display toggling, archived-session visibility toggling, selector rename/label drafting, and confirmed selector archive/restore; `/new`, `/resume`, `/fork`, `/clone`, `/name`, `/labels`, `/sessions rename`, `/sessions labels`, `/sessions archive`, and `/sessions unarchive` expose user-facing session creation, switching, branch creation, renaming, labels, and reversible archive. | Present in backend/RPC with partial TUI exposure. Remaining work is true in-session branch navigation, provider-generated branch summaries, and migration polish for future schema changes. |
| Context and prompts | Pi loads global/project `AGENTS.md`/`CLAUDE.md`, system prompt files, skills, prompt templates, and editor file references. | AVA loads project/global instructions, prompt commands, skills, plugin prompt/skill resources, provider prompt overrides, and TUI `@` file references with bounded permissioned expansion. | Partial. Decide whether AVA MVP needs Pi-style `SYSTEM.md`/append files, prompt templates, shell helpers, context freshness diagnostics, and project trust semantics or whether existing command/context loading is enough. |
| Config/settings | Pi has global/project settings, keybindings, trust, package config, and env controls. | AVA has domain-specific XDG JSON files for auth, models, prompts, compaction, LSP, keybinds, plugins, and MCP. | Partial. Unified global/project settings, validation, safe writes, reload diagnostics, and trust boundaries are an MVP gap. |
| Extensibility | Pi has extensions, skills, prompts, themes, packages, and custom providers. | AVA has bounded out-of-process local plugins, prompt/skill resources, plugin commands/tools/events, compatibility policy, sample plugin, and stdio MCP tools/prompts/read-style resources. | Core foundation present. Package manager, marketplace, remote install, custom provider plugins, and broad UI/theme extension are later/security-scoped. |
| MCP | Pi intentionally does not make MCP core. OpenCode has broad MCP transports and OAuth. | AVA has stdio MCP under explicit config, permission/audit identity, bounded tool/resource/prompt paths, and fake-server tests. | Present for a bounded local slice. Advanced HTTP/OAuth/subscriptions/sampling/templates/binary resources are not MVP. |
| LSP/code intelligence | Pi has coding-agent ergonomics; OpenCode exposes LSP config and tooling. | AVA has backend LSP diagnostics, document symbols, workspace symbols, definitions, references, explicit `lsp.json`, bounded on-disk `didOpen`, and server-launch permission. | Partial. Automatic server recipes, richer negotiation, incremental/unsaved-buffer sync, and presentation polish remain. |
| Multimodal | Pi supports images; OpenCode tracks attachments and multimodal config. | AVA has image-capable model metadata, sanitized attachment metadata, session attachment storage, fork/clone copy, replay validation, and OpenAI/compatible/Anthropic provider serialization. | Partial. RPC upload/input plumbing and user-facing import flows remain. |
| Headless/API | Pi has print/json/RPC modes; OpenCode has server/SSE/SDK. | AVA has print text/JSONL and stdio JSONL RPC protocol v1 with session, model, permission, compaction, export, tree, and resolver commands. | Present for MVP. HTTP server, OpenAPI, SDK, and SSE are later only after stdio RPC proves stable. |
| Testing | Pi has faux-provider regression harnesses. OpenCode has extensive package tests. | AVA has CTest `ava_tests`, fake providers/servers, plugin/MCP golden fixtures, headless CLI smokes, sanitizer workflow, and live provider smoke opt-in. | Present. Keep every safety-sensitive feature tied to focused tests and `git --no-pager diff --check`. |

## Pi-First MVP Checklist

This checklist is the working baseline. It is intentionally product-wide: backend work should expose safe contracts and persistence, while frontend/TUI implementation is in scope for AVA agent work when a selected MVP item needs user-facing terminal behavior.

Status guide: checked means AVA is MVP-usable for that Pi capability; unchecked means missing, partial, or not yet product-ready.

### Product Shell, CLI, And Modes

- [x] Native local terminal coding agent implemented in C++23.
- [x] Interactive TUI entry point for TTY use, with non-TTY line-shell fallback for scripted slash commands.
- [x] Print mode with `ava --print`/`-p`, text output, JSONL output, and piped stdin prompt input.
- [x] Stdio JSONL RPC mode for long-lived automation clients.
- [x] Session resume by latest workspace session and explicit session id/prefix.
- [ ] Direct positional one-shot prompt UX, for example `ava "prompt"`, if we want Pi-level fastest-path CLI ergonomics instead of requiring `--print`.
- [ ] Sessionless/ephemeral `--no-session` mode.
- [ ] Pi-style file argument UX, including `@file` or equivalent file inclusion from CLI/editor flows.
- [ ] Pi-style session CLI options such as fork-at-start, custom session name, and session directory selection where they fit AVA's storage model.
- [ ] CLI compatibility aliases only where useful, such as an explicit JSON mode alias if automation users expect `--mode json` rather than `--print --json`.

### Provider, Models, And Auth

- [x] Provider registry with OpenAI, Anthropic, Kimi, Moonshot, and OpenRouter-compatible families.
- [x] Model metadata for capabilities, modalities, context windows, pricing, and reasoning controls.
- [x] API-key auth from secure local storage and provider environment variables.
- [x] OpenAI browser/device/headless OAuth flows.
- [x] Anthropic OAuth token resolution and refresh for stored or environment-provided tokens.
- [ ] Anthropic interactive OAuth setup and live validation.
- [ ] Broader Pi-style provider breadth decision and implementation plan for Google, Copilot, Bedrock, Vertex, Azure, DeepSeek, Groq, xAI, Mistral, and other providers.
- [ ] Provider/model listing UX comparable to Pi's model discovery commands. `/models` and `/model` list provider/model capabilities, exact `/model` or `/models` open the TUI selector, Ctrl+L opens the selector between turns, Ctrl+P cycles to the next configured model, and Shift+Ctrl+P cycles to the previous model when the terminal reports that enhanced key sequence.
- [ ] Custom model/provider configuration with per-provider compatibility settings, request headers/body overrides, and clear validation errors.
- [ ] Per-request or scoped environment override story for provider credentials/config where useful.
- [ ] Cross-provider reasoning/thinking mapping and handoff behavior where providers expose incompatible reasoning formats.

### Agent Loop, Events, And Control

- [x] Streaming assistant text, reasoning, tool lifecycle, retry, compaction, cancellation, and terminal turn events.
- [x] Sequential tool-call loop with validation, permission checks, audit entries, and bounded outputs.
- [x] RPC steering and follow-up queues while a prompt is running.
- [x] Cooperative cancellation through RPC and runtime boundaries.
- [ ] User-facing interrupt/abort/resume polish equivalent to Pi's control flow, including clear continuation semantics after interruption. Stopped TUI turns now say to submit a new prompt to continue, and skipped queue audits explain what was not delivered.
- [ ] Explicit delivery vocabulary in docs and UI for steer-now, queue-next, and resume-later behavior. Active-run queued follow-ups and steering render above the composer; `/restore` or Alt+Up restores the latest pending queued item to the draft before it starts. Stopping an active run skips pending queued items and renders delivery guidance.
- [ ] Parallel or configurable tool execution only after permissions, cancellation, output ordering, and session replay semantics are designed.

### Built-In Tools

- [x] Pi core file/shell/search/list shape covered by `read_file`, `list_directory`, `write_file`, `edit_file`, `bash`, `glob`, and `grep`.
- [x] Stronger AVA-native tool set adds `apply_patch`, `webfetch`, `websearch`, `skill`, `question`, and capability-gated LSP tools.
- [x] Tool outputs are bounded and side effects go through permission/audit paths.
- [ ] Tool allowlist/exclusion controls comparable to Pi's `--tools`, `--exclude-tools`, `--no-builtin-tools`, and `--no-tools`, distinct from permission auto-allow policy.
- [ ] Tool naming/alias audit so Pi-style `find`/`ls` expectations map cleanly to AVA `glob`/`list_directory` without confusing models or users.
- [ ] Consistent user-visible tool cards for all model-visible tools, including progress, truncation, spill files, changed paths, diffs, permission state, and failure cause. Current cards render lifecycle/progress, shell status/duration, bounded output previews, truncation/spill metadata, unified diffs, changed paths, failure status, and linked permission audit decisions when backend events provide them; remaining work is broader per-tool polish and affordances.

### Permissions, Trust, And Safety

- [x] Backend allow/ask/deny policy with TUI, RPC, and headless resolvers.
- [x] Session-scoped grants, durable permission rules, protected rule files, and permission audit entries.
- [x] Hard-deny paths for unsafe or model-writable policy locations.
- [ ] Project trust boundary before loading project-local executable/plugin/config resources by default.
- [ ] Mature rule-management UX for listing, explaining, adding, removing, and diagnosing persistent rules. `/permissions` now provides TUI-visible list, audit, diagnose, explain, add, and remove flows over the protected persistent-rule store, and permission prompts can remember exact allow/deny decisions as workspace-scoped rules; remaining work is guided diagnostics and deeper audit navigation/export.
- [ ] Clear denial explanations in TUI, RPC, and headless output.
- [ ] Security review checklist for every new side-effect class: filesystem, shell, network, plugin, MCP, LSP server, credential, and session mutation.

### Sessions And Conversation History

- [x] Append-only JSONL session storage with resume/list behavior.
- [x] Compaction, export, stats, usage/cost records, model changes, and reasoning changes.
- [x] Backend/RPC tree inspection, fork, clone, names, labels, and caller-supplied branch summaries.
- [ ] User-facing session tree workflow for navigate/show, new session, resume/switch, fork, clone, rename, label, and reversible archive. `/sessions`/`/tree`, exact `/resume` picker with PageUp/PageDown row paging, Ctrl+S/Ctrl+T recent/name/path sorting, Ctrl+N named-session filtering, Ctrl+P path-display toggling, Ctrl+A archived visibility toggling, Ctrl+R rename drafting, Ctrl+L label drafting, and confirmed Ctrl+D archive/restore, `/new`, `/resume <id>`, `/fork`, `/clone`, `/name`, `/labels`, `/sessions rename`, `/sessions labels`, `/sessions archive`, and `/sessions unarchive` are implemented; true in-session branch navigation remains.
- [ ] Provider-generated branch summaries when switching or compacting branches, if backend-owned.
- [x] Pi-style session commands for `/new`, `/resume`, `/name`, `/sessions rename`, `/tree`, `/fork`, `/clone`, `/compact`, `/export`, and AVA-specific equivalents already present in the slash path.
- [ ] Export parity decision: keep Markdown-only, add HTML, or explicitly reject Pi-style share/export formats.
- [ ] Session migration/versioning policy for future schema changes.

### Context, Prompts, Skills, And File References

- [x] Project/global instruction loading from `AGENTS.md` and compatible context files.
- [x] Prompt commands and skills through the unified command/context registry.
- [x] Plugin prompt/skill resources can contribute bounded context.
- [ ] Pi-style `SYSTEM.md` and append-system prompt files, if adopted as AVA convention.
- [ ] CLI overrides for system prompt and appended system prompt.
- [ ] Prompt templates with variable interpolation and predictable expansion rules.
- [x] `@` file reference UX with fuzzy search in the editor/TUI and safe expansion into prompts. AVA supports bounded workspace-relative suggestions, quoted paths with spaces, case-insensitive fuzzy matching, directory continuation, and permissioned prompt expansion with per-file and per-prompt caps.
- [ ] `!` and `!!` shell-command prompt helpers only if they can preserve AVA permission/audit semantics.
- [ ] Context freshness diagnostics so users can see which global, project, plugin, skill, and prompt files affected a turn.

### Config, Settings, And Reload

- [x] Domain-specific XDG JSON files for auth, models, prompts, compaction, LSP, keybinds, plugins, and MCP.
- [ ] Unified global/project settings model with ordered merge and schema validation.
- [ ] Safe config writes with lock/atomic-write behavior and actionable parse errors.
- [ ] Reload diagnostics when settings, keybindings, prompts, plugins, MCP, LSP, or skills change. Keybinding reload now reports success/failure in the TUI and keeps prior bindings active on failure; broader settings, prompts, plugins, MCP, LSP, and skills reload diagnostics remain.
- [ ] Project-local settings and trust rules that cannot be silently upgraded by model-writable files.
- [ ] Offline/network-disable mode that clearly gates provider catalog updates, web tools, MCP remotes, and version checks if those exist.
- [ ] Keybinding customization UX at Pi maturity level: discoverable defaults, validation, conflict reporting, and reload behavior. AVA now accepts Pi-style single-key strings and arrays of key strings plus the existing comma-separated string form, accepts matching Pi namespaced action ids such as `tui.editor.cursorLineStart`, `tui.select.confirm`, and `app.tools.expand`, supports named `Space` without breaking normal text insertion, keeps `tui.select.*` bindings scoped to active select-list modals so Space can confirm highlighted rows without becoming composer submit and Ctrl+W can cancel a modal without removing composer delete-word behavior, supports the Pi Emacs-style `Ctrl+H` delete-backward binding and Pi Vim-style `Alt+H/J/K/L` plus `Alt+W` cursor aliases, gives current ids precedence over legacy camelCase aliases for the same AVA action, exposes effective bindings through `/hotkeys` and `/keybindings` with config/reload guidance, rejects user-configured key conflicts with key/action/path diagnostics, renders startup alerts for invalid keybind files, reloads valid keybind changes live with `/reload`, keeps previous bindings active on reload failure, and still allows custom bindings to intentionally shadow defaults.

### TUI And UX Maturity

- [x] Interactive TUI can render assistant text, thinking blocks, tool lifecycle updates, and permission prompts.
- [x] TUI can open provider/login flows and structured user questions through backend contracts.
- [ ] Mature multiline editor with history, paste handling, cursor movement, selection-friendly behavior, and robust terminal resize handling. Prompt history recall with draft restoration, Shift/Ctrl/Alt+Enter newline aliases, bracketed paste normalization, small paste insertion, large paste markers with atomic character/word navigation, deletion, and submit expansion, Shift+Tab/Ctrl+T reasoning-cycle aliases, Ctrl+-/Ctrl+Z undo, redo, kill/yank, Delete/Ctrl+D forward character deletion with empty-draft Ctrl+D exit, Ctrl+]/Ctrl+Alt+] character jumps, Alt+Left/Right and Ctrl+Left/Right word movement aliases, configurable Vim-style Alt+H/J/K/L cursor aliases and Alt+W word-right alias, Alt+Backspace backward word deletion, Alt+D/Alt+Delete forward word deletion, Ctrl+K line-end joining, UTF-8 cursor movement, vertical line navigation with sticky columns, Home/End line-boundary aliases, and tmux resize smoke are implemented; remaining work includes richer selection behavior and broader terminal compatibility.
- [x] Slash-command palette with autocomplete, descriptions, argument hints, clear unavailable-command errors, backend model/session/context/MCP/plugin argument suggestions, and bounded workspace path/glob completions for `/read`, `/write`, `/glob`, and `/grep`.
- [ ] General editor file/path autocomplete and `@` references beyond slash-command arguments. The `@` reference side now supports fuzzy matching, quoted paths with spaces, directory continuation, and bounded permissioned prompt expansion. Normal prompt text now supports path-like token completion for relative, `./`, quoted paths, and Tab-forced bare-token completion. Remaining work is richer editor behavior.
- [ ] Session tree UI over existing backend/RPC contracts.
- [ ] Permission UX maturity: readable diffs, risk labels, prompt reasons, remembered allow/deny prompt affordances, linked denied/allowed permission audit state on settled tool cards, and `/permissions` rule-management plus session-audit entry points are implemented; remaining work includes deeper audit navigation/export and broader denial diagnostics.
- [ ] Tool result UX maturity: lifecycle cards, streaming progress, changed-file summaries, shell failure cause, truncation/spill metadata, unified diffs, linked permission audit state, Pi-style Ctrl+O detail expansion, `/copy tool` clipboard export of the latest plain tool details, and `/copy diff` clipboard export of the latest unified diff are partially implemented; remaining work includes richer per-tool affordances, diff navigation beyond latest-diff export, retry/cancel states, and focused copy actions beyond the latest tool.
- [ ] Markdown/code/diff rendering good enough for real coding sessions, including long output performance and syntax-highlight strategy if adopted.
- [ ] Theme support and visual polish at product level, not just functional terminal output. AVA now honors `NO_COLOR=1` with a plain full-frame TUI render path and `/settings` reports the active plain `NO_COLOR` mode, covered by unit and tmux-smoke checks; Pi-style selectable built-in/custom theme files, terminal-background detection, and hot reload remain.
- [ ] Keyboard shortcut discovery and customization UX. `/hotkeys` and `/keybindings` expose the active TUI bindings, identify shared/context-resolved keys, and point users to `keybinds.json` plus `/reload keybindings`; Pi-style arrays, namespaced action ids, modal-scoped select-list bindings including Space confirm, named Space, Ctrl+H, and Vim-style Alt+H/J/K/L plus Alt+W cursor aliases now load through the same config path. Remaining work is richer editing/import affordances beyond read-only discovery.
- [ ] Inline image attachment import/preview for terminals that support it, plus safe textual fallback everywhere else.
- [ ] First-run onboarding that gets a user from no config to an authenticated working prompt with minimal confusion.
- [ ] Accessibility pass: keyboard-only operation, readable non-color fallback, screen-reader-friendly/headless alternatives, and no critical information conveyed by color alone. `NO_COLOR=1` now strips TUI styling while preserving content and width bounds; broader screen-reader/headless review remains.
- [ ] Performance pass: low flicker, responsive input while tools/providers run, bounded rendering cost for large transcripts, and stable behavior on narrow/mobile terminals.

### Extensibility, Packages, And Themes

- [x] Bounded out-of-process local plugin foundation with diagnostics, enablement, commands, tools, prompts, skills, events, and compatibility policy.
- [x] Stdio MCP local slice with explicit config, permission/audit identity, tools, prompts, and read-style resources.
- [ ] Pi-style package install/remove/update/list/config workflow, or an explicit product decision to defer it beyond MVP.
- [ ] Package trust/signing/source policy before any remote install flow.
- [ ] Custom provider registration through config or plugins, with strict auth/model metadata validation.
- [ ] Theme package story if AVA wants Pi-level customization.
- [ ] Extension hooks comparable to Pi's tool, command, event, and keyboard customization only where AVA can preserve process isolation and permissions.

### Multimodal And Attachments

- [x] Image-capable model metadata, sanitized attachment metadata, AVA-managed storage, fork/clone copy, replay validation, and provider serialization.
- [ ] RPC upload/input plumbing for attachments.
- [ ] TUI/user-facing image import flow.
- [ ] Inline preview where terminal capabilities allow it, with safe textual fallback metadata everywhere.
- [ ] Attachment export/replay behavior documented for every session/export format.

### Testing, Release, And Quality Bar

- [x] CTest `ava_tests`, fake providers/servers, plugin/MCP fixtures, sanitizer workflow, and live provider smoke opt-in.
- [x] Headless print/RPC smoke coverage for critical automation paths.
- [ ] Pi-parity checklist mapped to test coverage or an explicit manual smoke for every checked item.
- [ ] TUI regression harness for input/editor/session-tree/permission/tool-card workflows. Current opt-in tmux smoke covers NO_COLOR plain rendering without captured style escapes, `/settings` plain mode visibility, `/copy` empty state, `/copy tool` latest tool-detail copy status, `/copy diff` latest unified-diff copy status after a real `/write` mutation, slash palette visibility, slash path suggestions, normal prompt path suggestions, `@` file-reference suggestions including quoted paths with spaces, prompt history recall, permission prompts/audit cards, remembered permission rules, `/permissions list`, `/permissions audit`, Ctrl+L model selector opening, Ctrl+P model cycling, Alt+Up terminal key delivery, live Alt+H custom cursor binding, live Alt+W custom word-right binding, live select-list Space confirm and Ctrl+W cancel bindings, session selector PageUp/PageDown paging, session selector sort, named-session filtering, path-display toggling, Ctrl+A archived visibility toggling, Ctrl+R rename drafting/execution, Ctrl+L label drafting/execution, confirmed Ctrl+D selector archive/restore plus `/sessions --archived`, Shift+Tab reasoning cycling, Shift/Ctrl/Alt+Enter newline insertion, bracketed paste, multiline cursor movement including Home/End line boundaries, Ctrl+O tool detail expansion, Alt+Left/Right word movement, Ctrl+D forward character deletion, empty-draft Ctrl+D exit, Ctrl+]/Ctrl+Alt+] character jumps, Alt+Backspace backward word deletion, Alt+D/Alt+Delete forward word deletion, Ctrl+K line-end joining, Ctrl+- undo, resize, and quit cleanup; more workflows still need harness coverage.
- [ ] Provider live-smoke matrix with credential-gated skips and clear release criteria.
- [ ] Performance smoke for startup, large transcript render, large tool output, search, and session replay.
- [ ] Documentation check before MVP cut: user docs, protocol docs, config docs, and this checklist all agree with current code.

## MVP Gap Ledger

### P0: Baseline Truth

- Keep `README.md`, `docs/USAGE.md`, `docs/CONFIG.md`, `docs/headless-protocol.md`, `docs/product/*.md`, and `docs/roadmap/*.md` aligned with current C++ code.
- Record whether a feature is implemented in backend/RPC, implemented in TUI/frontend, deferred, or intentionally out of scope.
- Keep `docs/reference-code/` ignored and excluded from builds, tests, formatting, and normal source searches unless a task explicitly asks for reference analysis.

### P1: MVP Product Gaps

- Unified settings and reload diagnostics: global/project merge, validation, safe writes, clear error reporting, and no model-writable policy files.
- Project trust and context policy: decide the AVA equivalent for Pi's trust boundary before loading project-local executable/plugin/config resources by default.
- Context ergonomics: decide on Pi-style `SYSTEM.md`, append-system files, `@` file references, and prompt-template interpolation if they materially improve the coding loop.
- Session workflow completion: expose current backend/RPC tree/fork/clone/name/label/summary contracts in user-facing workflows, and decide whether branch summaries should be provider-generated by backend.
- Provider breadth validation: keep credential-gated live smokes for Anthropic, Moonshot, and OpenRouter-compatible paths; design new providers only with auth, model metadata, pricing, and smoke plans.
- Attachment input: add RPC upload/input plumbing and safe import flows without weakening AVA-managed attachment storage and replay validation.
- LSP maturity: add automatic server recipe discovery, richer capability negotiation, and unsaved/incremental sync only behind explicit config and permission boundaries.
- Permission UX and diagnostics: improve guided rule diagnostics, denial reasons, and deeper audit navigation without letting persistent rules upgrade built-in hard denies.

### P2 Or Research

- HTTP/server daemon mode, OpenAPI, SDK, or SSE streams.
- Subagents/task workers and parallel tool execution.
- Plugin package manager, marketplace, remote install, signing/trust, and custom provider plugins.
- Advanced MCP transports, OAuth, subscriptions, sampling, templates, binary/blob resources, and richer pagination.
- OS/container sandboxing with truthful cross-platform guarantees.
- Remote model catalogs or dynamic provider package loading.

## Explicit Non-MVP From OpenCode

- Desktop app, web app, cloud console, Slack/Discord bots, SaaS sharing, enterprise account flows, telemetry/stats infrastructure, GPU/container orchestration, Storybook, generated public SDKs, and broad deployment automation.
- Dynamic npm plugin loading or AI SDK package importing inside AVA's core runtime.
- In-process native plugin ABI by default; AVA's out-of-process boundary remains the safer default.

## Acceptance Rule

An MVP feature is not done until docs, tests, and failure modes agree:

- C++ implementation has focused tests or a documented smoke path.
- Permission, cancellation, audit, and output bounds are explicit when side effects are possible.
- RPC/headless contracts are documented when automation clients can observe the feature.
- Session replay/export behavior is defined when persistent records are written.
- `git --no-pager diff --check` passes after documentation or code changes.
