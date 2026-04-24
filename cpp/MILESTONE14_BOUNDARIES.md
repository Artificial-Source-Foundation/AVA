# C++ Milestone 14 Boundaries (First Narrow Pass)

This note records the first narrow Milestone 14 slice after accepted Milestone 13.

## Implemented in this M14 pass

1. Added a minimal control-plane-owned interactive lifecycle seam in C++ (`ava_control_plane`):
   - typed interactive kinds: `approval`, `question`, `plan`
   - request handle fields: `request_id`, `run_id`, `kind`, `state`
   - terminal states: `resolved`, `cancelled`, `timeout`
   - pending queue snapshot + request lookup helpers for adapter/runtime integration
2. Added a smallest orchestration-owned interactive bridge (`ava_orchestration::InteractiveBridge`) that:
   - reuses the existing tool approval middleware seam via `ApprovalBridge`
   - tracks approval/question/plan requests through the new control-plane store
   - supports bounded resolver hooks for approval/question/plan without moving ownership into TUI
3. Wired shared runtime composition (`compose_runtime`) to always attach this bridge and expose it on `RuntimeComposition`, keeping backend/orchestration ownership central.

## Explicitly Deferred (still out of this M14 pass)

1. Streaming or async interactive behavior (M15+ scope).
2. Full foreground/background interactive arbitration parity.
3. Cancellation/watchdog promotion semantics parity across all adapters.
4. MCP/plugin/background parity surfaces.

This pass is intentionally minimal and scoped to foreground interactive lifecycle ownership seams and bridge wiring.
