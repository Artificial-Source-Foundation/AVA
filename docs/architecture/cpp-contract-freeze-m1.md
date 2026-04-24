---
title: "C++ Contract Freeze (C++ Milestone 1)"
description: "Concrete freeze scope, fixture anchors, and signoff gates before backend/TUI C++ porting."
order: 11
updated: "2026-04-23"
---

# C++ Contract Freeze (C++ Milestone 1)

This document defines the concrete planning/signoff contract-freeze baseline that must be signed off before any Rust-to-C++ backend/TUI translation starts.

## Canonical Source Files and Symbols to Freeze

### 1) Control-plane contract ownership

Files:

1. `crates/ava-control-plane/src/commands.rs`
2. `crates/ava-control-plane/src/events.rs`
3. `crates/ava-control-plane/src/interactive.rs`
4. `crates/ava-control-plane/src/sessions.rs`
5. `crates/ava-control-plane/src/queue.rs`
6. `crates/ava-control-plane/src/orchestration.rs`
7. `crates/ava-control-plane/src/lib.rs`

Primary symbols/contract seams:

1. `ControlPlaneCommand`, `canonical_command_specs`, `command_spec`
2. `CanonicalEventKind`, `canonical_event_specs`, `canonical_event_spec`
3. `InteractiveRequestStore` lifecycle semantics (ordering, request IDs, stale resolution, run ownership, timeout behavior)
4. Session replay/selection semantics (`load_prompt_context`, replay payload builders, session precedence helpers)
5. Queue clear/alias/deferred ownership semantics (`parse_clear_queue_target`, `clear_queue_semantics`, deferred queue session resolution, in-flight/deferred promotion helpers)

### 2) Shared backend runtime types

Files:

1. `crates/ava-types/src/lib.rs`
2. `crates/ava-types/src/message.rs`
3. `crates/ava-types/src/tool.rs`
4. `crates/ava-types/src/session.rs`

Primary symbols/contract seams:

1. Message/session/tool JSON shape and serde behavior
2. Replay-critical fields (`tool_calls`, `tool_call_id`, metadata continuity)

### 3) Backend control-plane helper seams that materially affect runtime behavior

Files:

1. `crates/ava-agent/src/control_plane/mod.rs`
2. `crates/ava-agent/src/control_plane/events.rs`
3. `crates/ava-agent/src/control_plane/sessions.rs`

Primary symbols/contract seams:

1. Backend event projection and session/run-context helper behavior consumed by TUI/headless and side surfaces.

### 4) Session persistence and continuity seam

Files:

1. `crates/ava-session/src/lib.rs`
2. `crates/ava-session/src/manager.rs`
3. `crates/ava-session/src/tree.rs`
4. `crates/ava-session/src/search.rs`
5. `crates/ava-session/src/helpers.rs`
6. `crates/ava-session/src/diff_tracking.rs`

Primary symbols/contract seams:

1. Resume/replay, search/tree continuity behavior, and SQLite compatibility semantics required by backend/TUI correctness.

### 5) Config/credentials/trust/model-catalog seam used by backend startup and routing

Files:

1. `crates/ava-config/src/lib.rs`
2. `crates/ava-config/src/credentials.rs`
3. `crates/ava-config/src/keychain.rs`
4. `crates/ava-config/src/trust.rs`
5. `crates/ava-config/src/agents.rs`
6. `crates/ava-config/src/routing.rs`
7. `crates/ava-config/src/thinking.rs`
8. `crates/ava-config/src/credential_commands.rs`
9. `crates/ava-config/src/model_catalog/{mod.rs,registry.rs,types.rs,fallback.rs}`

Primary symbols/contract seams:

1. Runtime config loading, credential/trust behavior, and model/routing defaults that materially affect backend/TUI behavior.

### 6) Tool registry schema surface

File:

1. `crates/ava-tools/src/registry.rs`

Primary symbols/contract seams:

1. `ToolRegistry::list_tools`, `list_tools_for_tiers`, `tool_parameters`
2. Tool schema exposure and tiering behavior for backend model/tool contracts

### 7) CLI/headless entry contract surface

Files:

1. `crates/ava-tui/src/config/cli.rs`
2. `crates/ava-tui/src/headless/mod.rs`
3. `crates/ava-tui/src/headless/single.rs`
4. `crates/ava-tui/src/headless/common.rs`
5. `crates/ava-tui/src/headless/input.rs`
6. `crates/ava-tui/src/lib.rs`
7. `crates/ava-tui/src/main.rs`

Primary symbols/contract seams:

1. `CliArgs` flags and parse behavior
2. Headless slash-command handling and run dispatch semantics
3. `headless/input.rs` queue population and stdin parsing behavior (`populate_queue_from_cli`, `parse_stdin_message`) for queued/follow-up/post-complete input handling
4. Main entrypoint routing between TUI/headless and cwd override behavior for the backend/TUI-relevant CLI/headless sub-surface only (not every branch in `main.rs`)
5. Included `main.rs` branches for the freeze: cwd override resolution/application, `is_tui` vs headless selection, `run_headless(cli)` dispatch, `App::new(cli)` / `app.run()` TUI startup, and CLI flag handoff that affects backend/TUI startup semantics (`--cwd`, `--headless`, `--json`, `--agent`, provider/model overrides, resume/session handoff)
6. Explicitly excluded `main.rs` branches for this milestone: `cli.trust` trust-marking side effect, background update checks / `--no_update_check`, `cli.acp_server`, `Command::Update` / `Command::SelfUpdate`, `Command::Review`, `Command::Auth`, `Command::Plugin`, `Command::Serve`, and benchmark-only routing

### 8) Backend approval policy/classification seam

Files:

1. `crates/ava-permissions/src/lib.rs`
2. `crates/ava-permissions/src/inspector.rs`
3. `crates/ava-permissions/src/policy.rs`
4. `crates/ava-permissions/src/tags.rs`
5. `crates/ava-tools/src/permission_middleware.rs`

Primary symbols/contract seams:

1. Dangerous-vs-safe classification and approval middleware behavior that governs backend approval semantics, with headless as the first freeze-critical consumer.

### 9) Runtime composition seam for backend/TUI startup ownership

Files:

1. `crates/ava-agent-orchestration/src/stack/mod.rs`
2. `crates/ava-agent/src/run_context.rs`

Primary symbols/contract seams:

1. `AgentStack` ownership of startup assembly: config manager, session manager, model router, permission system, tool registry, bridges, and bootstrap ordering.
2. `AgentRunContext` ownership of provider/model/thinking/compaction/todo/permission context handoff used by CLI/headless/TUI callers.

Compatibility note:

1. Internal/backend contract JSON remains `snake_case`.
2. Tauri IPC `camelCase` is intentionally out of scope for this milestone.

## Existing Tests/Fixtures Already Acting as Freeze Points

The freeze should prefer current Rust inline/golden-style fixtures and tests (no snapshot-strategy switch for C++ Milestone 1).

Current concrete anchors:

1. `commands.rs`
   - `ws1_command_fixture_covers_expected_inventory`
   - `command_specs_match_ws1_contract_behavior_end_to_end`
   - `command_fixture_serializes_correlation_requirements`
2. `events.rs`
   - `ws2_event_inventory_matches_control_plane_contract`
   - `required_events_have_expected_required_fields`
   - `event_fixture_serializes_required_fields`
   - note: canonical contract tag spelling is `subagent_complete`; C++ Milestone 1 signoff requires canonical tag spelling parity for overlapping headless JSON lifecycle tags (`complete`, `error`, `subagent_complete`)
3. `interactive.rs`
   - `request_ids_are_kind_prefixed_and_correlatable`
   - `stale_request_ids_are_rejected_without_consuming_current_request`
   - `resolve_requires_matching_front_request_id_when_multiple_requests_are_queued`
   - `different_runs_can_resolve_same_kind_requests_independently`
   - `actionable_request_for_run_requires_global_front_ownership`
   - `timeout_only_consumes_matching_current_request`
   - `queued_hidden_approval_timeout_waits_until_request_is_promoted`
   - `queued_hidden_question_timeout_waits_until_request_is_promoted`
   - `queued_hidden_plan_timeout_waits_until_request_is_promoted`
   - `watchdog_timeout_window_starts_after_hidden_request_is_promoted`
   - `watchdog_timeout_for_one_run_does_not_wait_for_other_runs`
   - `cancel_cleanup_clears_pending_request`
   - `run_correlation_survives_timeout_and_cancel_cleanup`
   - `canonical_timeout_policy_is_shared_across_request_kinds`
4. `sessions.rs`
   - `existing_session_precedence_prefers_requested_over_last_active`
   - `existing_session_precedence_falls_back_to_last_active`
   - `session_precedence_generates_new_when_needed`
   - `load_prompt_context_uses_latest_user_turn`
   - `load_prompt_context_defaults_when_session_has_no_user_messages`
   - `retry_and_regenerate_payloads_share_latest_user_context`
   - `edit_replay_payload_rejects_missing_or_non_user_targets`
5. `queue.rs` + `orchestration.rs`
   - `clear_queue_targets_accept_shared_aliases`
   - `clear_queue_semantics_follow_contract`
   - `deferred_queue_session_resolution_uses_active_owner_when_requested_matches`
   - `deferred_queue_session_resolution_rejects_cross_session_append`
   - `deferred_queue_session_resolution_requires_active_owner`
   - `queued_post_complete_group_parses_group_prefix`
   - `inactive_scoped_status_lookup_matches_expected_inactive_messages`
   - `sync_deferred_queues_for_progress_promotes_follow_up_messages`
   - `sync_deferred_queues_for_progress_promotes_post_complete_group`
   - `sync_deferred_queues_for_progress_ignores_non_queue_progress`
   - `restore_in_flight_deferred_requeues_messages_in_original_order`
   - `clear_preserved_deferred_removes_both_queue_views`
   - `concurrent_queue_helpers_complete_without_lock_inversion`
6. `config/cli.rs` + `headless/{mod.rs,single.rs}` + `main.rs`
   - `headless_compact_slash_returns_system_message`
   - `headless_help_slash_returns_help_text_not_agent_goal`
   - `headless_unknown_slash_returns_error`
   - `headless_help_slash_uses_production_dispatch_path`
   - `headless_skills_slash_uses_lightweight_dispatch_path`
   - `headless_unknown_slash_uses_production_dispatch_path`
   - `headless_rejects_tui_only_slash_in_production_dispatch_path`
   - `apply_headless_session_metadata_sets_resume_fields`
   - `apply_headless_session_metadata_clears_primary_agent_fields_when_absent`
   - `headless_auto_approves_safe_requests`
   - `headless_rejects_dangerous_requests`
   - `headless_keeps_critical_requests_blocked`
   - `headless_resume_restore_uses_session_metadata_without_cli_overrides`
   - `headless_resume_restore_skips_session_metadata_when_cli_agent_override_present`
   - `headless_resume_restore_skips_session_model_when_cli_provider_or_model_override_present`
   - `apply_headless_resume_metadata_updates_startup_selection`
   - `apply_headless_resume_metadata_respects_explicit_cli_overrides`
   - `test_parse_json_accepts_canonical_queue_command_names`
   - `test_parse_json_post_complete_with_group`
   - `test_parse_json_defaults_group_to_1`
   - `cwd_flag_takes_precedence_over_environment`
   - `environment_cwd_is_used_when_flag_missing`
7. `ava-permissions` + `permission_middleware.rs`
   - `permissive_allows_up_to_high`
   - `standard_allows_up_to_medium`
   - `strict_allows_only_safe`
   - `policy_serialization_roundtrip`
   - `core_profiles_have_expected_risk_levels`
   - `core_profiles_have_expected_tags`
   - `all_core_tools_present`
   - `risk_level_ordering`
   - `safety_tag_serialization_roundtrip`
   - `approval_bridge_allows_tool_execution`
   - `session_approval_persists_in_context`
   - `deny_propagates_as_permission_denied`
   - `ask_without_bridge_propagates_as_permission_denied`
   - `rejection_propagates_as_permission_denied`
   - `auto_approve_context_bypasses_bridge_for_allowed_tools`
   - `after_passthrough`
8. `crates/ava-session/src/manager.rs`
   - migration compatibility guards (`duplicate_column_migration_errors_are_ignored`)
   - persistence continuity checks (`save_incremental_survives_manager_restart`, message/tool-call update persistence tests)
9. Runtime composition seam
   - `crates/ava-agent-orchestration/src/stack/mod.rs` and `crates/ava-agent/src/run_context.rs` remain file-frozen for ownership/signoff in C++ Milestone 1 even though their current verification anchor is architecture review plus the existing stack integration coverage, not a single dedicated golden fixture.

## Drift Risks If C++ Work Starts Before Freeze Lock

1. Contract drift between Rust and C++ during active translation (especially command/event envelopes and required fields).
2. Interactive lifecycle mismatch (stale request handling, timeout ordering, per-run ownership) causing cross-surface regressions.
3. Session replay breakage from schema/metadata divergence or SQLite migration skew.
4. Queue semantics drift (deferred vs in-flight behavior) that only appears under concurrent/active-run conditions.
5. CLI/headless behavior divergence that breaks automation and benchmark comparability.

## Recommended Milestone Outputs and Signoff Criteria

Required outputs:

1. Frozen checklist mapping each freeze file to signed contract/test anchors.
2. Canonical acceptance gate sheet for:
   - command/event JSON schema
   - interactive lifecycle semantics
   - session continuity/replay semantics
   - queue semantics
   - CLI/headless contract surface
   - session/SQLite persistence compatibility
   - JSON event-stream parity
   - backend approval policy/classification semantics
   - runtime composition ownership (`AgentStack` / `AgentRunContext`)
3. Dedicated JSON event-stream parity checklist artifact: [cpp-m1-event-stream-parity-checklist.md](cpp-m1-event-stream-parity-checklist.md).
4. Explicit out-of-scope list recorded in the migration plan and backlog language.
5. Explicit canonical-vs-headless event-surface boundary note recorded in the parity checklist, including the frozen headless-emitter differences for overlapping canonical tags and canonical backend events not emitted as headless JSON kinds.

Signoff criteria:

1. All freeze anchors pass in Rust without changing contract shapes.
2. Freeze checklist reviewed and accepted as the Phase 1 entry gate.
3. No open ambiguity on snake_case backend contracts vs camelCase Tauri IPC scope boundaries.
4. No open ambiguity on included vs excluded `main.rs` routes or on backend approval/runtime composition ownership seams.
5. No undocumented canonical-tag divergence for overlapping headless JSON lifecycle tags (`complete`, `error`, `subagent_complete`), and no ambiguity between the canonical control-plane contract and the headless NDJSON emitter surface.
6. Known headless-emitter differences from canonical backend event field shapes are explicitly documented for C++ Milestone 1 signoff (rather than requiring full field-shape equivalence).

Post-signoff drift prevention:

1. Any change to a frozen file requires explicit freeze-lift review/signoff before merge: mark the PR `freeze-lift`, update this checklist and the parity checklist if relevant, and get approval from the backend-ownership reviewer before merge.
2. Existing Rust contract/fixture tests that anchor the freeze remain mandatory and must stay green for freeze-governed changes.

## Freeze Authority and Enforcement Surface (Implemented)

Authority:

1. Backend ownership authority for C++ Milestone 1 freeze-governed files is codified in `.github/CODEOWNERS` under the **C++ Milestone 1 contract-freeze authority** section.
2. The normative freeze source-of-truth remains this checklist plus [cpp-m1-event-stream-parity-checklist.md](cpp-m1-event-stream-parity-checklist.md).

Enforcement (lightweight governance lane):

1. CI now runs `.github/workflows/ci.yml` job **C++ M1 Freeze Guard** on pull requests.
2. Guard script: `scripts/dev/verify-cpp-m1-freeze.sh`.
3. If no freeze-governed files changed, the guard exits cleanly.
4. If freeze-governed files changed, the guard requires both:
   - PR label `freeze-lift` (wired via `AVA_CPP_M1_FREEZE_LIFT=1` in CI)
   - a same-PR update to this checklist and/or the parity checklist.
5. Direct pushes to protected branches must remain blocked by branch protection requiring pull-request CI; the freeze guard is intentionally PR-contextual because the `freeze-lift` approval signal is a PR label.
6. Local/manual verification entrypoint: `just freeze-m1-check [<git-range>]`.
