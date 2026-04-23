---
title: "C++ M1 Event-Stream Parity Checklist"
description: "Concrete headless JSON event-stream checklist required by C++ Milestone 1 signoff."
order: 12
updated: "2026-04-22"
---

# C++ M1 Event-Stream Parity Checklist

This is a planning/signoff artifact for C++ Milestone 1. It does not represent implemented C++ runtime work.

## Canonical Rust Anchor

1. Primary emission path: `crates/ava-tui/src/headless/single.rs`
2. Supporting behavioral anchors: `crates/ava-tui/src/headless/mod.rs`
3. Canonical event taxonomy source: `crates/ava-control-plane/src/events.rs`

## Contract Boundary (Canonical vs Headless NDJSON)

1. Canonical control-plane event contract ownership is `crates/ava-control-plane/src/events.rs`.
2. Headless `--json` output from `crates/ava-tui/src/headless/single.rs` is a consumer-facing NDJSON emitter surface, not a full canonical event-envelope mirror.
3. C++ Milestone 1 requires canonical tag spelling parity for overlapping lifecycle tags in headless JSON (`complete`, `error`, `subagent_complete`).
4. C++ Milestone 1 does **not** require full canonical field-shape equivalence for the headless NDJSON emitter; known differences must be explicitly documented and stable.

## Required Headless JSON Output Checks

1. Output remains newline-delimited JSON in `--json` mode.
2. Terminal lifecycle always ends in either `complete` or `error` output.
3. Correlation-critical IDs remain present where required by the backend contract (`run_id`, interactive request IDs, tool-call identifiers).
4. Tool execution lifecycle remains visible through `tool_call` and `tool_result` output.
5. Streaming/progress lifecycle remains visible through the current output kinds emitted by `headless/single.rs`, including `text`, `thinking`, `progress`, `tool_stats`, `token_usage`, `budget_warning`, `subagent_complete`, `diff_preview`, `mcp_tools_changed`, `retry_heartbeat`, and `fallback_model_switch` when those paths are active.
6. Canonical control-plane event tags and headless-emitter-only JSON output kinds are tracked separately during signoff: `complete`, `error`, and `subagent_complete` are canonical backend contract tags, while `text`, `thinking`, `tool_stats`, and similar convenience output kinds are headless-emitter surface details.
7. Frozen, documented headless-emitter field-shape differences for overlapping canonical tags are accepted for C++ Milestone 1 (no full field-shape equivalence requirement):
   - `complete`: headless emits a terminal automation-oriented completion envelope rather than the full canonical backend `complete` event field set.
   - `error`: headless emits a terminal automation-oriented error envelope rather than the full canonical backend `error` event field set.
   - `subagent_complete`: headless emits a delegated-run summary envelope rather than the full canonical backend `subagent_complete` event field set.
8. Canonical backend events not emitted as headless JSON kinds are explicitly tracked and accepted for this milestone: `plan_step_complete`, `streaming_edit_progress`.
9. Interactive lifecycle output remains represented in JSON mode even when headless auto-approval resolves the request quickly.
10. Queue-driven follow-up behavior does not suppress the final terminal event or reorder terminal output around deferred queue promotion.

## Required Rust Test/Code Anchors

1. `crates/ava-tui/src/headless/mod.rs`
   - `headless_auto_approves_safe_requests`
   - `headless_rejects_dangerous_requests`
   - `headless_keeps_critical_requests_blocked`
   - `test_parse_json_accepts_canonical_queue_command_names`
   - `test_parse_json_post_complete_with_group`
   - `test_parse_json_defaults_group_to_1`
2. `crates/ava-tui/src/headless/single.rs`
   - `headless_compact_slash_returns_system_message`
   - `headless_help_slash_returns_help_text_not_agent_goal`
   - `headless_unknown_slash_returns_error`
   - `headless_help_slash_uses_production_dispatch_path`
   - `headless_skills_slash_uses_lightweight_dispatch_path`
   - `headless_unknown_slash_uses_production_dispatch_path`
   - `headless_rejects_tui_only_slash_in_production_dispatch_path`
   - `apply_headless_session_metadata_sets_resume_fields`
   - `apply_headless_session_metadata_clears_primary_agent_fields_when_absent`
3. `crates/ava-control-plane/src/events.rs`
   - `ws2_event_inventory_matches_control_plane_contract`
   - `required_events_have_expected_required_fields`
   - `event_fixture_serializes_required_fields`

## Signoff Rule

1. C++ Milestone 1 is not signed off until this checklist is reviewed alongside [cpp-contract-freeze-m1.md](cpp-contract-freeze-m1.md).
2. Signoff requires no undocumented canonical-tag divergence for overlapping lifecycle tags (`complete`, `error`, `subagent_complete`) and explicit documentation of accepted headless-emitter differences.
3. If the Rust headless JSON event surface changes while the freeze is active, update this checklist in the same freeze-lift PR.
