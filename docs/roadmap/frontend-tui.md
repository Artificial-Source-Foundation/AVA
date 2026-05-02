# AVA Frontend TUI Roadmap

This roadmap defines the frontend/TUI work needed for AVA 1.0 to feel like a strong daily terminal coding agent while preserving AVA's constraints: native C++23, one binary, terminal first, explicit backend safety boundaries, and inspectable local state.

The backend roadmap remains the source of truth for runtime, provider, session, tool, permission, and plugin sequencing. This document owns how those capabilities become a usable interactive terminal experience.

Frontend phase numbers are local to this document. They do not imply that frontend Phase N and backend Phase N are the same milestone; cross-roadmap dependencies are capability-based.

Cross-roadmap references should name backend capabilities, not just backend phase numbers. When implementation plans cite this roadmap, use dependencies such as "event envelope stream", "question resolver contract", "session stats API", "provider model catalog", or "backend-provided unified diff" so frontend work can proceed safely even if backend phase labels change.

## 1.0 TUI Goal

AVA 1.0 should have a calm, fast, readable terminal interface that supports long coding sessions without hiding important state.

Required 1.0 behavior:

- The TUI and headless RPC consume the same backend event stream for assistant text, tool lifecycle, permission requests, question requests, cancellation, and terminal outcomes.
- The composer remains fixed and dependable: multiline input, history, mode awareness, slash discovery, and clear submit/cancel behavior.
- The transcript tells the session story: user messages, assistant text, tool activity, errors, compaction, permissions, and exported session events are visible and scannable.
- Tool and edit operations are understandable before and after they mutate files.
- Long-session state is visible: current workspace, session, provider/model, context pressure, usage/cost when known, and loaded context sources.
- Interactive prompts are focused decisions with safe defaults and parity with headless permission/question contracts.
- The TUI remains a presentation and interaction layer. Backend modules own command semantics, authorization, session mutation, provider behavior, and tool execution.

## Reference Lessons

Reference code is used for product and interaction comparison only. Do not copy architecture or source code into AVA.

### OpenCode

Useful reference paths:

- `docs/reference-code/opencode/packages/web/src/content/docs/tui.mdx`
- `docs/reference-code/opencode/packages/plugin/src/tui.ts`
- `docs/reference-code/opencode/packages/opencode/specs/tui-plugins.md`
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/component/dialog-command.tsx`
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/component/dialog-variant.tsx`
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/permission.tsx`
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/question.tsx`
- `docs/reference-code/opencode/packages/opencode/src/config/keybinds.ts`
- `docs/reference-code/opencode/packages/opencode/src/provider/transform.ts`
- `docs/reference-code/opencode/packages/web/src/content/docs/keybinds.mdx`
- `docs/reference-code/opencode/packages/web/src/content/docs/models.mdx`
- `docs/reference-code/opencode/.opencode/plugins/tui-smoke.tsx`

Lessons to adapt:

- Commands should be metadata-rich: title, description, category, slash name/aliases, keybind display, suggested/hidden/enabled state, and one backend-owned action id.
- Dialogs for permissions and questions should handle single-select, multi-select, custom text, confirmation, rejection, and keyboard escape paths consistently.
- The TUI should expose compact commands for common session work: `/compact`, `/details`, `/export`, `/models`, `/sessions`, and thinking visibility once provider capabilities exist. `Ctrl+T` should be the primary way to rotate thinking modes.
- Thinking display and thinking capability are separate controls. OpenCode uses `/thinking` for display visibility and `Ctrl+T` for cycling model variants; AVA should keep the same conceptual split. A `/variants` or `/thinking-modes` selector can exist later as a direct-pick modal, but it should not replace the `Ctrl+T` rotation flow.
- Provider/model reasoning levels are capability data, not universal TUI constants. OpenAI-style models may expose levels such as `none`, `minimal`, `low`, `medium`, `high`, and `xhigh`, while other providers expose different names or no variants.
- Keybinds should be semantic actions first, physical keys second. Even if AVA does not expose user-configurable keybinds in v1, the internal model should avoid hardcoding behavior across renderers.
- OpenCode's plugin slots, route system, install flow, theme marketplace, MCP/sidebar surfaces, and web-style extensibility are not AVA v1 goals. They are useful proof that narrow host APIs matter later.

### PI / pi-mono

Useful reference paths:

- `docs/reference-code/pi-mono/packages/tui/src/tui.ts`
- `docs/reference-code/pi-mono/packages/tui/src/terminal.ts`
- `docs/reference-code/pi-mono/packages/tui/src/stdin-buffer.ts`
- `docs/reference-code/pi-mono/packages/tui/src/utils.ts`
- `docs/reference-code/pi-mono/packages/tui/src/autocomplete.ts`
- `docs/reference-code/pi-mono/packages/tui/src/components/editor.ts`
- `docs/reference-code/pi-mono/packages/tui/src/keybindings.ts`
- `docs/reference-code/pi-mono/packages/tui/test/virtual-terminal.ts`
- `docs/reference-code/pi-mono/packages/tui/test/tui-render.test.ts`
- `docs/reference-code/pi-mono/packages/tui/test/editor.test.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/slash-commands.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/footer-data-provider.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/session-manager.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/test/suite/harness.ts`
- `docs/reference-code/pi-mono/packages/ai/src/providers/faux.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/docs/tui.md`
- `docs/reference-code/pi-mono/packages/coding-agent/src/modes/interactive/interactive-mode.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/modes/interactive/components/footer.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/modes/interactive/components/assistant-message.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/modes/interactive/components/bash-execution.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/modes/interactive/components/tool-execution.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/modes/interactive/components/compaction-summary-message.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/modes/interactive/components/diff.ts`
- `docs/reference-code/pi-mono/packages/tui/src/components/markdown.ts`

Lessons to adapt:

- A terminal component contract can stay small: render to bounded-width lines, handle input only when focused, invalidate cached render state, and preserve text width correctness.
- Differential rendering, synchronized output, and throttled redraws are useful after live streaming lands, but AVA should first migrate to event-driven state. Optimize measured redraw pain, not theoretical flicker, and do not replace ncursesw solely to imitate PI's raw-ANSI renderer.
- Terminal correctness is product quality: escape-sequence buffering, bracketed paste, resize redraws, cursor placement, IME-sensitive hardware cursor positioning, width overflow diagnostics, and Unicode grapheme/CJK/emoji width tests should be treated as v1 hardening work.
- The footer should be structured, not a single opaque status string: workspace/cwd, git/session, token usage, cost, context percentage, provider/model, and reasoning state where available.
- Token counts and context pressure belong in the structured footer/status surface, not inside the composer text area. Command outputs such as help/session stats should render into the chat transcript as normal system-style entries.
- Chat/transcript rendering should have a small, explicit message taxonomy: user, assistant, inline thinking, tool, shell, permission/question audit, compaction, command output, and error entries. Avoid inventing ad hoc transcript shapes per command.
- Markdown rendering should cover the common coding-agent subset well: paragraphs, lists, headings, inline code, fenced code, blockquotes, links, and simple tables when feasible. Complex tables should degrade to readable text rather than becoming a Phase 2 layout blocker. Full HTML, Mermaid, rich media, and shell integration zones are not v1 requirements.
- The command surface should cover daily session control, not just discovery: help/hotkeys, model selection, compaction, export, session stats, session naming, new/resume session, context reload, and provider login/logout where backend support exists.
- Slash autocomplete should eventually cover command arguments, model names, file/path references, prompt/context sources, and disabled/conflict diagnostics instead of only filtering static command names. Treat this as a staged subsystem, not a prerequisite for the first prompt UX pass.
- The composer should support real editor behaviors before cosmetic features: word movement/deletion, line start/end, undo/yank where feasible, bracketed paste handling, autocomplete cancellation, scroll indicators for tall drafts, and visible queued steering/follow-up messages.
- Bash shortcuts are useful only if they preserve AVA's backend safety boundary: `!` can be a compact command-execution affordance and `!!` can mean excluded from model context, but both must route through permissioned backend command execution and session policy.
- Pending tool state should be separate from immutable transcript history so streaming tool updates do not rewrite completed messages.
- Tool cards need lifecycle states beyond running/success/error: arguments streaming, execution started, arguments complete, partial result, complete result, expanded/collapsed, and error detail.
- Tool output should use a common visual truncation contract: display omitted line/byte counts, keep wrapped-width math consistent, and preserve an expansion path when full output exists.
- Diff rendering can stay unified-diff-only for v1, but should include line numbers, add/remove/context colors, width-aware wrapping, and optional intra-line highlighting when cheap and deterministic.
- Compaction should render as a first-class transcript event with collapsed summary and expandable details.
- Startup/context visibility should be compact by default and expandable on demand, showing loaded context files and diagnostics without forcing users to parse prompts.
- Testing should include a fake provider/event harness and terminal-render harness so streaming, prompts, tool events, resize, Unicode width, and regression cases are verifiable without a real provider or paid tokens.
- PI's extension-loaded custom renderers, raw-ANSI renderer, image protocol handling, branch tree UI, external share flows, and package/resource ecosystem are post-v1 unless they directly support a v1 acceptance criterion.

## Current AVA Baseline

Current TUI strengths:

- `src/ava/tui/runtime.cpp` owns an extracted ncursesw interactive loop with input, scroll, bounded history, mouse wheel/click support, permission/question prompt flow, spinner processing state, and callback wiring.
- `src/ava/tui/composer*.cpp` renders a composer-first interface with top-start transcript layout, fixed bottom input block, integrated slash palette, compact tool cards, permission/question docks, draft indicators, and spinner-only processing feedback.
- `src/ava/tui/terminal.cpp` wraps terminal setup with RAII and wide-character ncurses handling.
- `docs/USAGE.md` documents current TUI layout, permission/question prompt behavior, semantic keybinds, commands, and current limits.
- The TUI already presents tool activity as compact timeline cards and keeps permission/question decisions backend-owned and auditable.
- Phase 1 shipped metadata-rich slash commands, disabled command explanations, `/help` and `/hotkeys`, user-configurable semantic keybinds, UTF-8-safe draft editing, undo/yank, bracketed paste, autocomplete dismissal without draft loss, and Ctrl-C clear-before-exit behavior.

Current 1.0 gaps:

- The TUI still uses blocking runtime glue and replayed callback results; it does not fully consume the shared backend event stream.
- Assistant text and tool results are not live in the interactive TUI even though headless event foundations exist.
- Session stats, usage/cost, context pressure, loaded context files, and compaction state are not visible enough for long sessions.
- Slash commands are metadata-rich, but commands that need backend state such as model/session/import/reload/login flows remain disabled or shallow until backend APIs exist.
- Slash autocomplete is still mostly command-name based; it does not yet complete command arguments, model names, file/path references, or context/prompt sources.
- The composer has core editor affordances, but visible queued follow-up/steering state is still missing.
- Mouse-wheel scrolling inside tall composer drafts is still pending. The render state can represent draft offsets, but runtime wheel behavior needs a follow-up fix before this is considered complete.
- Terminal/input hardening is incomplete for v1-level daily use: broader escape-sequence buffering, IME-sensitive cursor placement, Unicode width edge cases, and resize stress need explicit coverage.
- Tool cards are summaries only; there is no detail toggle, diff preview, spill-file affordance, or streaming progress view.
- Thinking/reasoning UI is not specified enough yet: `Ctrl+T` exists as an inert semantic action, but provider-specific variants and thinking blocks must come from backend capability/event data.
- Render tests cover static composer behavior, prompts, keybinds, paste, and palette behavior, but live event consumption, resize stress, long transcripts, and performance remain under-covered.

## Roadmap Phases

### Phase 0: Roadmap And TUI Boundary

Purpose: define the frontend target and prevent drift before broad TUI work starts.

Scope:

- Treat this document as the frontend companion to `docs/roadmap/backend.md`.
- Keep the custom ncursesw path as the default until a concrete blocker justifies FTXUI or another dependency.
- Preserve the existing backend/TUI boundary: slash palette renders options, but backend command handlers own parsing and effects.
- Keep OpenCode as the primary visual/interaction reference and PI as the primary terminal-runtime/session-visibility reference.

Acceptance criteria:

- Each frontend phase lists explicit capability dependencies rather than relying on matching backend phase numbers.
- TUI non-goals are explicit.
- Future implementation plans can cite this roadmap instead of rediscovering scope.

Status: complete. This roadmap was accepted as the frontend companion to the backend roadmap, with Phase 1 implemented and reviewed as the first interaction-contract batch.

### Phase 1: Interaction Contract Foundation

Purpose: make the terminal chat feel like a normal chat app while closing the current prompt and command UX gaps without requiring the full event-stream migration.

Backend dependency: current permission resolver and backend/RPC question resolver contracts. This phase is specifically about the interactive TUI prompt UX, not inventing the resolver contract.

Scope:

- Start short conversations at the top of the transcript area instead of pinning them above the composer. Scrolling should feel natural: new output stays visible by default, but existing scrollback remains reachable and visibly indicated.
- Add a small processing animation in or near the composer so users can see that AVA is working during the current blocking TUI runtime. Include a reserved composer/status slot for future token or context usage text, even if the backend cannot fill it yet.
- Design tool usage as readable compact cards with clear running/success/error states, argument summaries, and result summaries. Keep richer live updates, diff expansion, and full tool-detail panes for the evented TUI phase unless a small static card improvement is enough.
- Add an interactive question prompt that supports header text, question text, options, single-select, multi-select, optional custom text, cancel/reject, and safe keyboard defaults. Multi-select should follow the existing backend `QuestionPrompt::multiple` contract and serialize selected values through the current answer path.
- Keep the permission prompt dock, but make its request summary, default deny state, allow-once wording, denial path, and long command/path wrapping consistent with question prompts.
- Introduce semantic TUI action names internally, such as `submit`, `new_line`, `cancel`, `clear_input`, `history_prev`, `history_next`, `cursor_word_left`, `cursor_word_right`, `delete_word_backward`, `delete_to_line_start`, `delete_to_line_end`, `undo`, `yank`, `autocomplete_accept`, `palette_next`, `palette_prev`, `prompt_allow`, `prompt_deny`, `details_toggle`, `variant_cycle`, and `interrupt`. Editor-only actions such as `undo`/`yank` stay TUI-local; `variant_cycle` is inert until backend provider/model capability data exists.
- Add a user-configurable keybind file for these semantic actions. Defaults should preserve current behavior where possible, and `/help` or `/hotkeys` should show the effective bindings rather than hardcoded prose.
- Prefer fast prompt shortcuts where unambiguous: number keys for visible question options, consistent Tab/arrow navigation, and explicit rejection feedback for permission/question denial when backend contracts support it.
- Polish slash command metadata: category, short description, hint/key display, aliases where supported, and disabled-state text for commands whose backend capability is not ready. Slash command metadata is TUI-owned presentation state until a backend command registry exposes structured metadata.
- Add `/help` or `/hotkeys` output that reflects semantic key behavior and current mode. It should render into the transcript/chat area, not open an unrelated settings surface.
- Define the v1 command surface in one place. Baseline commands should include `/help` or `/hotkeys`, `/model` or `/models`, `/compact`, `/export`, `/import` when JSONL/session import exists, `/session` or `/stats`, `/context`, `/new`, `/resume`, `/reload`, `/login`, `/logout`, and `/quit` where backend support exists.
- Keep command availability honest: unavailable commands should be hidden or disabled with a short reason rather than advertised as working.
- Show command/resource diagnostics for conflicts and skipped entries, such as a future plugin/prompt command colliding with a built-in slash command. In v1 this can be a read-only diagnostic; do not add dynamic command registration.
- Establish autocomplete trigger/cancel/apply rules before adding breadth: `/` at command position, Escape/Ctrl-C cancel without losing input, Tab accepts suggestions, and Enter behavior is deterministic.
- Stage argument-aware autocomplete behind real data sources. Model names for `/model`, destination/path hints for `/export`, session identifiers for `/resume`, and context source names for `/context` should only ship when the relevant backend metadata/API exists.
- Defer file/path autocomplete breadth until the backend/tooling path can provide ignored-path-aware search semantics. Do not recursively walk the workspace from the TUI to satisfy Phase 1.
- Add bracketed paste handling and draft scroll indicators before adding richer composer chrome. Bracketed paste is TUI-local terminal input handling and should not require backend protocol changes.
- Keep session-local input history bounded and private: no consecutive duplicates, bounded size such as 100 entries, and no persistent cross-session prompt history without an explicit privacy/storage decision.
- Defer `!` and `!!` shell affordances until the backend command/tool path can preserve permission checks, auditability, cancellation, output bounds, and context-inclusion semantics. If implemented later, the composer should visibly distinguish shell mode and context-excluded `!!` mode.

Likely files:

- `src/ava/tui/composer.h`
- `src/ava/tui/composer_permission.cpp`
- `src/ava/tui/composer_palette.cpp`
- `src/ava/tui/runtime.cpp`
- `src/ava/tui/runtime.h`
- `tests/core_tests.cpp` or current TUI test sections
- `docs/USAGE.md`

Acceptance criteria:

- Interactive `question` requests no longer require the assistant to ask manually in chat.
- Permission and question prompts have deterministic keyboard behavior and fail closed on escape/cancel.
- Slash palette entries are grouped or clearly labeled by category where useful.
- The help/hotkeys command lists real semantic actions, including `Ctrl+T` thinking-mode rotation, rather than stale hardcoded prose.
- Commands with missing backend support are not executable without a clear disabled-state explanation.
- Composer editing tests cover word movement/deletion, line start/end, undo/yank if implemented, bracketed paste, tall draft scrolling, and autocomplete cancellation.
- Argument/file autocomplete breadth may remain disabled in Phase 1 if backend metadata or ignored-path-aware search semantics are not available.
- Static render/input tests cover top-start transcript layout, scroll indicators, composer processing status, tool cards, question prompt selection, multi-select toggling, custom answer entry, cancel, narrow widths, and UTF-8 labels.

Status: complete. Phase 1 shipped the interaction-contract foundation in the TUI and was locally validated and reviewed. Follow-up polish also removed composer footer status chatter, made the processing indicator spinner-only, integrated and simplified the slash palette, fixed transcript scroll affordances, added Ctrl-C clear-before-exit behavior, and replaced plain TUI startup/exit text with a user-friendly exit card.

Completed implementation notes (2026-05-01): AVA now has interactive permission and question prompts, including multi-select and custom answers; metadata-rich slash commands with aliases, disabled explanations, `/help`, and `/hotkeys`; semantic configurable keybinds loaded from `keybinds.json`; a TUI-local composer draft editor for UTF-8-safe insertion/deletion, word movement/deletion, current-line movement/deletion, undo (`Ctrl+Z`), and yank (`Ctrl+Y`); bracketed paste handling; autocomplete dismissal without clearing input; and a reserved token/status slot plus spinner-only processing indicator. Mouse-wheel scrolling for tall composer drafts remains pending. Persistent history, argument/file autocomplete, live event consumption, tool expansion/diffs, provider model controls, and broader terminal hardening remain later-phase work.

### Phase 2: Evented TUI Runtime

Purpose: make the TUI a live event consumer instead of a blocking callback result renderer.

Backend dependency: the backend evented runtime and protocol foundations from `docs/roadmap/backend.md`. Those foundations are implemented for headless clients; this phase is the frontend integration work needed for the TUI to consume the same stream live.

Scope:

- Subscribe the TUI to the shared backend event stream used by print/RPC modes.
- Maintain TUI state from event envelopes: run start/end, message deltas, tool start/update/end, permission requested/replied, question requested/replied, cancellation requested/done, errors, and terminal outcomes.
- Split interactive state into completed transcript, pending assistant text, pending tools, active prompt, status/footer, and command palette state.
- Track pending tools by backend tool call id so argument streaming, execution start, partial result updates, final result, and errors update the same visible card.
- Render assistant streaming deltas live without exposing raw protocol noise.
- Render user, assistant, inline thinking, command-output, error, and audit entries through the same transcript model so reload/replay produces the same visible story as the live run.
- Render common markdown consistently in assistant and command-output entries. Code fences must wrap safely at narrow widths. Basic tables may render when feasible, but complex or wide tables should degrade to readable text instead of blocking event-stream work.
- Render tool lifecycle changes live, including running state and partial result summaries where backend events provide them.
- Make cancellation visible and cooperative: user interrupt updates the active run state and eventually renders a terminal cancellation event.
- Keep the initial implementation single-threaded or narrowly synchronized. Do not add a broad async UI framework unless event handling cannot be made safe locally.

Likely files:

- `src/ava/app/events.*`
- `src/ava/app/runtime.*`
- `src/ava/tui/runtime.cpp`
- `src/ava/tui/composer.h`
- `src/ava/tui/composer_transcript.cpp`
- `src/ava/tui/composer_input.cpp`
- `tests/core_tests.cpp` or future TUI-focused tests

Acceptance criteria:

- TUI and RPC observe the same logical events for one prompt turn.
- Assistant streaming text appears incrementally in the TUI.
- Tool start and completion are visible before the full provider turn finishes.
- Permission and question prompts can appear during an active evented run without corrupting transcript state.
- Cancellation during provider work, tool work, permission wait, and question wait has deterministic UI outcomes.
- Tests cover event replay into TUI state without requiring a real terminal.

Minimum backend event mapping for the TUI:

| Backend event | TUI state |
| --- | --- |
| `session_start` or equivalent session metadata | Footer/session/provider display |
| `message_start`, `message_update`, `message_end` | Pending assistant text and completed assistant transcript entries |
| `thinking_update` | Inline thinking block attached to the active assistant turn, never the bottom composer/status area |
| `tool_start`, `tool_update`, `tool_end` | Pending tool card lifecycle and completed tool transcript entries |
| `permission_requested`, `permission_replied` | Permission dock state and audit transcript marker |
| `question_requested`, `question_replied` | Question prompt state and answer transcript marker |
| `compaction`, `retry`, `cancellation`, `error`, `done` | Status/footer changes and terminal transcript markers |

Thinking block invariants:

- Thinking/reasoning content is part of the active assistant turn.
- It may be collapsed or expanded when long, but it must never render as bottom composer text, generic status text, or a detached footer line.
- `/thinking` or an equivalent command controls display visibility only. It does not enable provider reasoning; provider/model capability state owns that.
- `Ctrl+T` cycling is a provider/model control and may be inert until backend-declared reasoning variants exist.

### Phase 3: Long-Session Visibility

Purpose: make sustained coding sessions understandable and controllable.

Backend dependency: backend-owned session metadata, context source metadata, compaction entries, usage/cost aggregation when available, retry/backoff events, export/import APIs, and session stats APIs described in `docs/roadmap/backend.md` and related product docs.

Scope:

Footer and status:

- Replace the single opaque status line with structured footer data: cwd/workspace, git branch when cheap, session id/name, mode, provider/model, context percentage/window, token usage, cost when available, and compaction status when available.
- Prefer a footer data-provider boundary over ad hoc render-time polling. Footer state should be cheap to render and updated by backend/session/git/context events.
- Footer usage should be cumulative for the session, not just currently visible post-compaction transcript rows. Prefer input/output token counts plus cache read/write, reasoning tokens, and cost only when the backend provides them.
- If backend auth semantics distinguish token billing from subscription/OAuth billing, show a compact cost/subscription indicator such as `$0.123` or `$0.123 (sub)` only when the data is trustworthy.
- Footer model display should include provider when useful and the active thinking/reasoning mode when the active backend model supports it.
- Keep the composer input clear of heavyweight metrics. A tiny right-side metadata slot is acceptable for mode/model hints, but token counts, cost, and context pressure should stay in the footer/status surface.
- If context usage is unknown after compaction, display an explicit unknown state such as `?/<window>` until the backend reports fresh usage.
- Color context pressure conservatively: normal below warning threshold, warning near the limit, error when the next turn is likely to fail without compaction.

Session, context, and compaction:

- Add a compact context/status command that shows loaded context sources from backend-owned metadata, including path, source type, size when available, and load/skipped/error status.
- Show context/resource diagnostics when backend/context loading reports them: duplicate names, skipped sources, load errors, winner/loser conflict paths, and source scope such as project vs user.
- Add `/session` or `/stats` transcript output for deeper state: session name/id/path, message/tool counts, token totals, cost when known, current provider/model/thinking mode, compaction count, and export/resume hints.
- Add `/name` when backend session metadata supports display names. This should update the footer/session display and write through backend-owned session metadata.
- Update the terminal title from backend-owned session state when safe: app name, session display name when present, and workspace basename. Do not expose secrets or long paths in the title.
- Render compaction entries as transcript events with collapsed summary and expandable details.
- Add `/compact` UX that calls backend compaction and renders success, unavailable, nothing-to-compact, or failure outcomes clearly. Optional custom compaction instructions are allowed only if the backend supports and persists them.

Import, export, retry, and queues:

- Add `/export` UX that collects destination/options and delegates formatting to backend export APIs.
- Add `/import` only if backend session import exists. It should confirm replacing/switching the current session, validate JSONL/session headers through backend APIs, and render actionable path/version/cwd errors.
- Add `/new`, `/resume`, and `/reload` only through backend-owned session/context APIs. `/reload` should refresh loaded project/global instructions and report changed, skipped, or failed context sources.
- Improve `/sessions` for recent sessions and switching without implementing full tree/fork/timeline UI.
- `/resume` or `/sessions` should start as a flat recent-session selector with cwd/session-name/model/time metadata. Tree/fork navigation remains out of scope.
- Show retry/backoff state when backend emits it: spinner/countdown, retry reason, and a clear escape/interrupt path.
- Retry/backoff UI should show attempt count, max attempts, delay countdown, reason, and cancel key. Intermediate retry failures should not look like final turn failure unless the backend reports retry exhaustion.
- During active streaming or compaction, show queued steering/follow-up messages in a small pending region with a deterministic action to restore them to the composer before they are sent.
- During compaction, queue user input explicitly rather than dropping it or submitting against stale context. The pending region should distinguish steering vs follow-up where backend semantics do.

Likely files:

- `src/ava/tui/composer_input.cpp`
- `src/ava/tui/composer_transcript.cpp`
- `src/ava/tui/composer_palette.cpp`
- `src/ava/tui/runtime.cpp`
- `src/ava/app/line_shell.cpp`
- `src/ava/session/*` integration points only through public APIs
- `docs/USAGE.md`

Acceptance criteria:

- A user can tell which session, model, mode, directory, and approximate context state they are in without leaving the TUI.
- Loaded context sources are visible through a TUI command or compact startup/status event without parsing prompt text.
- `/session` or `/stats` can render a concise transcript block with message/tool counts and cumulative usage.
- Session name changes, new session, resume, reload, and login/logout commands update footer/status through backend-owned state.
- `/import`, if shipped, validates and switches sessions through backend APIs and reports malformed, missing, or incompatible files without TUI-side parsing.
- Queued steering/follow-up messages are visible and recoverable from the composer before submission.
- Context unknown-after-compaction state is visible and recovers when backend usage data becomes available again.
- Manual compaction has an obvious before/after transcript marker.
- Export uses backend formatting and does not duplicate markdown serialization in the TUI.
- Long transcript render tests cover compaction entries and footer truncation at narrow widths.

### Phase 4: Tool And Edit Workflow

Purpose: make tool execution safe and inspectable from the user's point of view.

Backend dependency: backend tool-quality work, especially unified diffs, streaming tool updates, search semantics, spill files, and stronger cancellation/output bounds. Tool display must rely on backend tool metadata and contracts from `docs/product/tooling-plan.md` plus permission semantics from the product/backend roadmap, not TUI-local parsing.

Scope:

- Add a tool details toggle similar to OpenCode's `/details`, defaulting to compact summaries but allowing expanded output for selected or all tool cards.
- Render truncated output with explicit affordances: omitted byte/line counts, spill-file path when backend provides one, and guidance for opening or exporting detail.
- Distinguish backend/context truncation from frontend visual truncation. The UI can collapse long output for readability, but it must not imply the model saw content that backend omitted from context.
- Sanitize binary and non-printable tool/shell output before display, preferably using visible replacement rather than silently dropping dangerous bytes.
- Add diff preview for edit/apply_patch-style mutations when backend provides a unified diff.
- Keep v1 diff preview simple: unified diff, line numbers, add/remove/context colors, width-aware wrapping, scrollable or paged if needed, explicit allow/deny. Intra-line highlighting is allowed only when cheap and deterministic. Rich split diff remains non-goal.
- Render search and shell progress updates when backend emits them, without spamming completed transcript history.
- Surface tool errors with operation, path/tool name, underlying cause, retry state when applicable, and any backend-provided structured error code.
- Show detailed error context progressively: concise error first, dimmed details when useful, and stack/debug data only in explicit debug or expanded views.
- Use a consistent expandable output pattern for tools, bash/shell output, and long errors: preview by default, explicit omitted counts, deterministic expand/collapse action, and no silent truncation.

Likely files:

- `src/ava/tui/composer_transcript.cpp`
- `src/ava/tui/composer_permission.cpp`
- `src/ava/tui/runtime.cpp`
- `src/ava/tools/*` only as needed for display summaries exposed through backend contracts
- `tests/core_tests.cpp` or future TUI-focused tests

Acceptance criteria:

- File mutation prompts can show a backend-provided unified diff before approval.
- Tool cards can be expanded/collapsed through deterministic keyboard commands or slash commands.
- Long-running tools visibly progress and then settle into a completed transcript item.
- Truncation is visible and actionable instead of silently summarized away.
- Tool/shell output cannot inject terminal control state or binary garbage into the TUI.
- Diff previews never parse or infer mutations independently of backend-provided diff/metadata.
- Tests cover diff rendering at narrow/wide widths, expansion state, truncation display, and error cards.

### Phase 5: Provider And Model Controls

Purpose: expose provider/model capability once the backend supports more than the current OpenAI-first path.

Backend dependency: provider registry, model catalog, model switching, usage/cost extraction, and reasoning controls where supported by the active provider. The TUI reads backend-declared capabilities; it does not hardcode provider variant lists.

Scope:

- Add `/models` or `/model` selector backed by provider/model metadata.
- `/model` should accept optional search text and expose argument completions from backend-declared provider/model metadata.
- Bind `Ctrl+T` to the semantic `variant_cycle` action by default. It should rotate thinking modes using only backend-declared variants for the active provider/model, and it should explain or no-op safely when no variants exist.
- Optionally add `/variants`, `/thinking-modes`, or an equivalent selector backed by provider/model metadata for direct selection in a modal. This is secondary to `Ctrl+T` rotation.
- Show capability-relevant metadata without clutter: context window, tool support, streaming support, reasoning support, cost when known, and provider auth state.
- Support mid-session model switches through backend-owned commands and session entries.
- Add thinking/reasoning visibility controls only after backend emits thinking/reasoning lifecycle events. Follow the Phase 2 thinking block invariants: display visibility is separate from provider reasoning capability, and thinking content belongs with the active assistant turn.
- Keep provider login/setup UI minimal. TUI may route to existing backend auth flows but should not own provider credential semantics.

Acceptance criteria:

- Model selection only lists models the backend says are usable or explains why a model is unavailable.
- `Ctrl+T` cycles backend-declared thinking modes deterministically, records the backend-owned model/thinking-level change in session state, and updates the footer/provider display.
- Any modal selector for variants/thinking modes only lists backend-declared variants for the active provider/model. OpenAI-like levels such as `none`, `minimal`, `low`, `medium`, `high`, and `xhigh` are examples, not hardcoded TUI assumptions.
- Switching models records session state through backend APIs.
- Footer/provider display reflects the active model and reasoning state.
- Thinking visibility does not imply reasoning is enabled unless backend capability state confirms it.
- Thinking-block placement tests prove reasoning text appears in the transcript/pending assistant region, not at the bottom composer/status area.

### Phase 6: V1 Hardening And Terminal Quality

Purpose: make the TUI reliable across real terminals and long sessions.

Backend dependency: all backend capabilities that feed interactive behavior, especially event streaming, prompt resolution, session/context metadata, tool metadata, provider/model capability data, and cancellation/retry events.

Scope:

- Stress test transcript size, wide Unicode, combining marks, CJK width, resize behavior, mouse wheel handling, and slow terminal redraw.
- Include IME/hardware-cursor-sensitive cases in terminal validation so CJK input and composed text do not corrupt cursor placement.
- Test escape-sequence buffering and bracketed paste so split terminal input does not leak raw control bytes into the composer.
- Escape-sequence buffering should cover incomplete CSI/OSC/APC/DCS-style sequences with timeout-based fallback, plus bracketed paste start/end markers.
- Add width-overflow diagnostics for render surfaces so any rendered line wider than the terminal is caught in tests before it corrupts the display.
- In debug/test builds, width overflow should fail loudly with useful diagnostics; in release builds, the final compositor should truncate or otherwise contain overflow safely.
- Add a measured rendering strategy after live streaming: keep full redraw if it is fast enough; add dirty-region or differential redraw only if profiling shows user-visible issues.
- Preserve readable behavior on narrow terminals and fail clearly below minimum usable dimensions.
- Audit keyboard behavior for conflicts between composer input, history, slash palette, permission prompt, question prompt, diff preview, and tool details.
- Keep terminal output sanitized: model text, tool output, paths, and shell text are untrusted and must not inject uncontrolled terminal state.
- Coordinate with the `cpp-ncurses-tests` branch work before adding overlapping terminal wrapper abstractions. Its ncurses experiments cover initialization, screen size/resize, panels, wrapping, attributes, complex characters, session RAII, and window wrappers.
- Update docs and screenshots/examples if present.

Acceptance criteria:

- Normal build, sanitizer build, test suite, and `git --no-pager diff --check` pass.
- Ncurses test harness work from `cpp-ncurses-tests` is either merged, integrated, or explicitly superseded before adding overlapping terminal wrapper abstractions.
- TUI state tests cover event replay, prompts, footer, compaction, tool expansion, diff preview, and cancellation.
- Fake-provider tests cover streamed text, thinking blocks, tool calls, tool partial/final results, retry/backoff, cancellation, and usage reporting without live provider calls.
- Manual smoke verifies interactive chat, slash commands, permission ask/deny, question prompt, long output, resize, export, compact, session resume, and cancellation.
- `docs/USAGE.md` accurately reflects shipped keybindings, commands, and current limits.

## Explicit Non-Goals For V1

- Replacing ncursesw with FTXUI or another dependency without a specific blocker and decision record.
- Replacing ncursesw or bypassing it with a raw-ANSI renderer solely because PI uses raw terminal output.
- Full theme system or theme marketplace.
- Plugin-extensible sidebars, arbitrary TUI slots, route plugins, or custom renderer plugins.
- Dynamic extension slash commands, `/skill:*` commands, command invocation-name conflict rewriting, or plugin-provided command handlers.
- Full session tree, branch/fork timeline, or visual merge workflow.
- PI-style `/fork`, `/clone`, and `/tree` branch workflows.
- External sharing commands such as `/share` to GitHub Gist or any other network publishing flow.
- Clipboard commands such as `/copy` unless a separate terminal/clipboard permission and portability decision is made.
- Full settings UI, `/settings`, `/scoped-models`, or `/changelog`; prefer focused commands and backend-owned config flows until a settings surface is justified.
- External editor integration, file-drop attachments, and large-paste marker collapsing unless they become necessary for a concrete v1 usability blocker.
- OSC133 shell integration zones or terminal-specific clickable message boundaries.
- Terminal image protocols, image clipboard paste, and rich media rendering.
- Persistent cross-session prompt history unless privacy/storage semantics are explicitly accepted.
- Rich split diff review with syntax-highlighted side-by-side editing.
- Image rendering, clipboard integration, desktop/web UI, MCP UI, LSP UI, or subagent UI.
- HTTP/server daemon mode or web-based UI.
- Full non-TTY parity for TUI affordances. Line-mode and redirected stdin/stdout flows should remain headless-compatible, but they do not need to replicate interactive prompts, live visual streaming, footer UI, or terminal-specific command palette behavior.
- TUI-hardcoded provider reasoning variant lists. Provider/model metadata owns the available levels and request payloads.
- Persistent permission-rule management UI unless backend permission semantics are finalized earlier than expected.
- TUI-owned parsing or execution semantics for backend commands.

## Validation Plan

Every frontend phase should leave the TUI more testable than it found it.

Preferred validation layers:

- Pure render tests for composer, transcript, footer, command palette, prompts, diff preview, and narrow/wide layouts.
- Event replay tests that feed backend event envelopes into TUI state without requiring ncurses.
- Fake-provider integration tests for streaming assistant text, tool calls, permission prompts, question prompts, cancellation, compaction, and export.
- Virtual-terminal or mock-terminal tests for resize, cursor placement, differential/full redraw decisions, bracketed paste, escape-sequence buffering, and width overflow diagnostics.
- Unicode/width regression tests for CJK, combining marks, emoji, regional indicators, tabs, ANSI-styled text, and truncation/wrapping boundaries.
- Manual terminal smoke for real ncurses behavior: resize, input editing, mouse wheel, UTF-8 input, Ctrl-C/Ctrl-D, and non-TTY fallback.
- Ncurses-focused harnesses, potentially informed by `cpp-ncurses-tests`, for initialization, resize, panels/windows, attributes/colors, wrapping, complex characters, and cursor placement.

Per-phase coverage expectations:

| Phase | Required coverage before handoff |
| --- | --- |
| Phase 1 | Pure render/input tests for permission/question prompts, slash metadata, semantic action dispatch, bracketed paste basics, history/editing behavior, and narrow-width layouts. |
| Phase 2 | Event replay tests for streamed assistant text, inline thinking placement, tool lifecycle updates, prompt requests, cancellation, and transcript replay parity with live state. |
| Phase 3 | Footer/status render tests, context/session command output tests, compaction transcript tests, retry/backoff event tests, queued message tests, and import/export command-path tests when shipped. |
| Phase 4 | Tool expansion, truncation display, binary/control sanitization, progressive error, and backend-provided unified diff render tests at narrow and wide widths. |
| Phase 5 | Model selector, unavailable model display, `Ctrl+T` no-op/cycle behavior, footer reasoning-state display, and thinking visibility tests using backend-declared metadata. |
| Phase 6 | Virtual/mock terminal, Unicode width, resize, IME/cursor, escape buffering, bracketed paste, long transcript, sanitizer, and manual ncurses smoke coverage. |

Baseline commands before handoff:

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
git --no-pager diff --check
```

For safety-sensitive TUI/runtime changes, also run the sanitizer workflow from `AGENTS.md`.

## V1 Completion Criteria

The frontend roadmap is complete for AVA 1.0 when:

- TUI and RPC share the same backend event semantics.
- A normal coding session can run for a long time with visible context pressure, usage/session state, compaction, and export.
- Tool execution and file mutations are clear enough that users can understand what happened and approve risky operations deliberately.
- Permission and question prompts work interactively and match headless fail-closed semantics.
- Provider/model controls reflect backend capabilities without TUI-specific special cases.
- The custom terminal UI is stable under resize, long output, narrow widths, Unicode input, and cancellation.
