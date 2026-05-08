# Frontend Phase 2 Design Comparison

Milestone 0 comparison for planning the next AVA frontend/TUI phase.

## Goal

AVA should move from a mostly final-result TUI to a live evented TUI while taking interaction direction and implementation discipline from external behavior material.

## Frontend Interaction Lessons

Useful local reference material lives under `docs/reference-code/`; do not copy its code or architecture.

What to adapt:

- A calm session layout with chat as the main surface and a right-side sidebar for session/context status when the terminal is wide enough.
- A sidebar that feels like a supporting panel, not a second transcript: session identity, workspace/status, context or usage summaries, and recently changed files once backend data exists.
- A visible agent activity checklist using AVA's active run/tool state first and richer plan/task state only when backend semantics exist.
- A `Modified Files` section with file names and green/red add/remove counts when diff metadata is available; until then, the sidebar should be ready for that data and can show safe modified-file summaries from backend/tool metadata.
- A footer-style sidebar tail with current path, git branch when cheap/safe, and AVA version.
- Live tool rows that update in place while a turn is running, then settle into compact completed cards.
- Dialogs and command surfaces that use visual highlighting instead of printing internal state labels.
- Footer/status information that is structured and stable, not ad hoc log text in the composer.

What to avoid:

- Plugin slot systems, route architectures, web-style theme marketplaces, and sidebar extension models.
- Mouse-heavy interactions as the primary path; AVA should stay keyboard-first with mouse support as a convenience.
- Copying external UI architecture. AVA stays C++23/ncurses and keeps backend semantics outside the TUI.

## Runtime Reference Lessons

Useful local reference material lives under `docs/reference-code/`; do not copy its code or architecture.

What to adapt:

- State changes should arrive as events, then render from a clear UI state model.
- Pending assistant text, pending tools, completed transcript, active prompts, and footer/sidebar data should be separate state buckets.
- Rendering should be testable without a live provider or paid tokens by replaying event sequences into pure state.
- Footer/sidebar metrics should come from backend/session data, not render-time scraping.
- Future render optimization should be measured. Keep full redraw until event streaming proves it is too slow.

What to avoid:

- Replacing ncurses with another renderer solely to match external internals.
- Porting an extension system, image protocol support, or complex component runtime.
- Implementing token/cost/context metrics in the TUI before the backend can provide trustworthy values.

## AVA Phase 2 Direction

Phase 2 should prioritize live event consumption before large visual expansion.

Planned adaptation path:

1. Add a pure event-to-TUI-state layer that can be tested by replaying backend `RuntimeEvent`s. Status: implemented, with a shared `EventEnvelope` reducer path for the stream used by print/RPC.
2. Wire the current TUI submit flow so backend events are queued from the worker thread and drained/rendered on the main ncurses thread. Status: implemented through the TUI event queue.
3. Render pending assistant text and pending tool cards live. Status: implemented for assistant deltas, thinking deltas, provider tool-call phases, execution progress, completed tool settlement, backend-provided mutation diff previews, and backend-owned compaction/retry/cancellation markers including bounded attempt totals, retry delays, and retry countdown ticks where emitted.
4. Add a sidebar shell only after live state exists, using currently safe data first: mode, provider/model, session id, workspace, active run state, pending tool count, AVA version, cwd/branch, and a modified-files section fed by backend/tool metadata where available. Status: partially implemented with trusted known/unknown handling.
5. Defer rich sidebar data such as token/cost/context pressure, model variants, LSP status, and full diff navigation until the backend exposes those as explicit data or events. Status: still deferred for fields the backend does not emit.

## Mapping To Milestones

| Reference lesson | AVA milestone |
| --- | --- |
| Event/state separation | Event-to-TUI state foundation |
| Replayable tests | Event replay tests before runtime wiring |
| Live assistant/tool feel | Live streaming and tool lifecycle milestones |
| Sidebar layout | Sidebar-ready layout shell after live state |
| Modified-files/todo/version sidebar | Sidebar activity and modified-file sections, fed by safe backend/tool data |
| Footer/status discipline | Structured sidebar/footer data, no composer log text |
| Both systems' prompt consistency | Preserve existing main-thread permission/question prompt queue |
