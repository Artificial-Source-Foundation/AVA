# Agent Loop, Tools, Permissions, And Safety

## Goal Objective

Preserve AVA's stronger backend safety model while closing Pi user-facing parity gaps in agent control, tool behavior, tool cards, permission diagnostics, and safety documentation. Pi remains the primary coding-agent baseline; OpenCode is now a required co-primary reference for agent/tool permission UX, approval modes, saved rules, and manual permission-prompt behavior.

Suggested Codex command:

```text
/goal Bring AVA agent/tools/permissions parity to documented closure using docs/goals/pi-mvp-parity/agent-tools-permissions.md. First inspect Pi agent/tool behavior, OpenCode permission/tool UX under docs/reference-code/opencode, and AVA agent/tool/permission modules, then implement or defer every 100 percent criterion with safety tests, docs, and manual/PTY evidence. Stop for approval before adding parallel tools, new side-effect classes, or weaker permission defaults.
```

## Pi References To Inspect First

| Topic | Pi paths |
| --- | --- |
| Agent loop | `docs/reference-code/pi/packages/agent/src/agent-loop.ts`, `agent.ts`, `types.ts` |
| Harness/session runtime | `docs/reference-code/pi/packages/agent/src/harness/`, `docs/reference-code/pi/packages/coding-agent/src/core/agent-session.ts` |
| Tools | `docs/reference-code/pi/packages/coding-agent/src/core/tools/` |
| Bash executor | `docs/reference-code/pi/packages/coding-agent/src/core/bash-executor.ts` |
| Tool UI | `docs/reference-code/pi/packages/coding-agent/src/modes/interactive/components/tool-execution.ts` |
| Permissions/trust | `docs/reference-code/pi/packages/coding-agent/src/core/project-trust.ts`, `trust-manager.ts`, `docs/security.md` |
| Tests | Pi agent tests, coding-agent tool tests, trust tests, tool-execution component tests |

## OpenCode References To Inspect First

OpenCode was cloned from `https://github.com/anomalyco/opencode` into `docs/reference-code/opencode/` on 2026-07-04 for behavior comparison. Treat it as a reference repo only: do not include it in AVA builds, source searches outside this goal, formatting, or tests unless the task explicitly asks for reference analysis.

| Topic | OpenCode paths |
| --- | --- |
| Permission docs and config semantics | `docs/reference-code/opencode/packages/web/src/content/docs/permissions.mdx` |
| Current permission service | `docs/reference-code/opencode/packages/core/src/permission.ts` |
| Permission schema/events | `docs/reference-code/opencode/packages/schema/src/permission.ts`, `docs/reference-code/opencode/packages/schema/src/permission-saved.ts` |
| CLI/TUI permission state machine | `docs/reference-code/opencode/packages/opencode/src/cli/cmd/run/permission.shared.ts` |
| CLI/TUI permission footer | `docs/reference-code/opencode/packages/opencode/src/cli/cmd/run/footer.permission.tsx` |
| Tool-specific permission display | `docs/reference-code/opencode/packages/opencode/src/cli/cmd/run/tool.ts` |
| Agent/subagent permission inheritance | `docs/reference-code/opencode/packages/opencode/src/agent/subagent-permissions.ts`, `docs/reference-code/opencode/packages/web/src/content/docs/agents.mdx` |
| Tool architecture | `docs/reference-code/opencode/packages/core/src/tool/tool.ts`, `application-tools.ts`, `tools.ts`, `registry.ts` |
| Built-in tool docs | `docs/reference-code/opencode/packages/web/src/content/docs/tools.mdx`, `custom-tools.mdx`, `specs/v2/tools.md` |
| Permission tests | `docs/reference-code/opencode/packages/core/test/permission.test.ts`, `packages/opencode/test/permission-task.test.ts`, `packages/opencode/test/cli/run/permission.shared.test.ts`, `packages/opencode/test/acp/permission.test.ts` |
| Tool tests | `docs/reference-code/opencode/packages/core/test/tool-*.test.ts`, `session-runner-tool-events.test.ts`, `session-runner-tool-registry.test.ts` |
| Manual TUI guidance | `docs/reference-code/opencode/packages/opencode/AGENTS.md` |

## AVA References To Inspect First

| Topic | AVA paths |
| --- | --- |
| Agent loop | `src/ava/agent/agent_loop.cpp`, `agent_loop.h`, `assistant_turn.h`, `tool_dispatcher.cpp` |
| Tool metadata/registry | `src/ava/agent/tool_metadata.h`, `tool_registry.h`, `tool_visibility.h` |
| Tools | `src/ava/tools/` |
| Permissions | `src/ava/permissions/permission.cpp`, `permission_rules.cpp`, headers |
| Runtime events | `src/ava/app/events.h`, `src/ava/tui/event_state.cpp` |
| TUI cards/prompts | `src/ava/tui/tool_cards.cpp`, `composer_permission.cpp` |
| Tests | `tests/agent_loop_tests.cpp`, `tests/agent_loop_resilience_tests.cpp`, `tests/agent_tool_dispatcher_tests.cpp`, `tests/tools_*_tests.cpp`, `tests/permission_rules_tests.cpp`, `tests/tui_composer_tests.cpp` |

## Current Gap Summary

AVA is ahead of Pi in safety: granular permissions, audit, durable rules, protected policy files, MCP/LSP/tool boundaries, web tools, `question`, and `apply_patch`. OpenCode adds useful reference behavior for permissions as a first-class product surface: `allow`/`ask`/`deny` rules, `once`/`always`/`reject` replies, saved session/project patterns, auto-approve with explicit-deny enforcement, per-agent permission overrides, external-directory prompts, doom-loop prompts, and a tested permission UI state machine. This area closes the product-clarity gaps for the parallel-tool decision, user-facing interruption vocabulary, per-tool cards, denial wording, permission rule-management guidance, and side-effect review checklists through the progress log below.

## 100 Percent Criteria

| Criterion | Required AVA State |
| --- | --- |
| Agent control language | TUI, RPC, print/headless, and docs clearly distinguish stop, cancel, interrupt, steer-now, queue-next, restore draft, and resume-later behavior. |
| Tool parity matrix | Pi tools `read`, `write`, `edit`, `bash`, `grep`, `find`, `ls` and OpenCode tool/permission categories map cleanly to AVA built-ins or aliases. AVA-only tools are documented with safety constraints. |
| Tool cards | Every built-in model-visible tool has a consistent card or plain/headless representation for running, success, failure, canceled, permission denied, truncation/spill, changed paths, and diff where applicable. |
| Permission diagnostics | Denials include operation, target, risk, reason, resolution source, request id, rule link when applicable, and safe follow-up commands. Compare AVA wording against OpenCode's `Permission required`, `Allow once`, `Always`, `Reject`, reject-message, and external-directory wording. |
| Rule management | `/permissions` list/audit/explain/add/remove/diagnose/export flows are documented and tested. Hard denies cannot be upgraded by rules. |
| Side-effect checklist | New filesystem, shell, network, plugin, MCP, LSP, credential, provider, session, and package side effects have a required security review checklist in docs. |
| Parallel tools decision | Parallel/configurable tool execution is implemented with replay-safe semantics or explicitly deferred with rationale. |
| Tests | Every changed tool/permission path has deterministic tests, plus CLI/RPC smokes where user-visible, plus manual/PTY evidence for AVA permission/tool-card behavior after implementation and at least one manual OpenCode reference run or documented environment blocker. |

## Implementation Slices

| Slice | Work |
| --- | --- |
| A0. OpenCode reference audit | Inspect OpenCode permission docs, core service, schema, TUI state machine, subagent inheritance, tool docs/tests, and capture the product behaviors AVA should match or intentionally exceed. |
| A1. Control vocabulary | Update UI/headless/RPC docs and any status text for stop/cancel/queue/restore semantics. Add tests for active-run queue edge cases. |
| A2. Tool card completeness | Audit all 17 built-in tools and plugin/MCP tools for metadata and card output. Add missing summaries and plain-mode rows. |
| A3. Permission diagnostics | Extend structured permission result payloads and TUI/headless display without changing policy decisions. |
| A4. Rule UX | Finish `/permissions` docs and tests for list/audit/explain/diagnose/add/remove/export. |
| A5. Side-effect checklist | Add or update an engineering doc requiring permission, audit, cancellation, output-bound, test, and rollback design for new side effects. |
| A6. Parallel tool decision | Compare Pi steering/follow-up/parallel settings. Defer unless replay and permission ordering semantics are solved. |

## Non-Goals Unless Approved

| Item | Reason |
| --- | --- |
| Weaker Pi-style no-popup permissions | AVA's safety model is a product advantage and should remain. |
| Parallel tools without replay semantics | Could corrupt order, permissions, cancellation, and session replay. |
| New shell capabilities outside `bash` tool policy | Command execution must remain behind permissioned process tooling. |

## Verification

Targeted commands:

```sh
ctest --test-dir build -R 'ava_tests\.(agent_loop|agent_loop_resilience|agent_tool_dispatcher|tools|permission_rules|tui_composer)$' --output-on-failure
ctest --test-dir build -R 'ava_cli\.headless_rpc_(permission|tool|question|cancel|bash)' --output-on-failure
```

Manual AVA validation is required after any implementation that changes agent control, tools, permission prompts, permission rules, or tool cards. CTest alone is not enough for this goal. Capture or document these flows with tmux/PTY/Python evidence where feasible:

| AVA manual flow | Required evidence |
| --- | --- |
| Permission prompt | Trigger at least one `Ask` flow, show the prompt wording, allow-once behavior, deny behavior, request id/risk/target visibility, and terminal cleanup after response. |
| Permission rules | Add or exercise a remembered rule, verify the next matching request is handled as documented, and verify deny-wins or hard-deny behavior still blocks unsafe operations. |
| Tool cards | Run representative read/search/edit/bash/network or denied-tool flows and capture running/success/failure/denied/canceled cards, changed paths, diff preview, spill/truncation, and plain/headless fallback where affected. |
| Agent control | Exercise interrupt/cancel plus steer-now or queue-next/restore-draft behavior if those paths changed. |
| RPC/headless | Run the relevant permission request/reply/grant lifecycle and confirm the JSONL contract exposes request ids and structured result links. |

Suggested AVA manual/PTY commands, adjusted for the specific slice:

```sh
AVA_TUI_TMUX_SMOKE=1 ctest --preset dev --output-on-failure -R tui
ctest --preset dev --output-on-failure -R 'permission|tool_failure|question|bash_process_cleanup|active_run'
```

If a real terminal, tmux, provider credential, or interactive prompt cannot be exercised in the environment, record the exact blocker in this file, provide the command/script that should be run next, and compensate with deterministic CTest/RPC coverage plus source-level review. Do not mark a user-facing permission/tool-card change complete without either captured AVA manual evidence or an explicit documented blocker.

OpenCode reference checks are not AVA validation, but this goal must manually verify the reference behavior when feasible. From `docs/reference-code/opencode/packages/opencode/`, tests must not be run from the repo root. Use the package-local commands below if dependencies are available; otherwise record the exact blocker and rely on source/doc inspection plus AVA-side manual evidence.

```sh
bun test test/cli/run/permission.shared.test.ts test/permission-task.test.ts test/acp/permission.test.ts
```

Manual OpenCode TUI reference smoke, when dependencies and a terminal multiplexer are available:

```sh
tmux new-session -d -s opencode-permission 'cd docs/reference-code/opencode/packages/opencode && bun dev'
tmux capture-pane -pt opencode-permission
tmux kill-session -t opencode-permission
```

For a complete manual permission comparison, capture or describe at least one OpenCode permission prompt flow: initial permission request, `Allow once`, `Always` confirmation, `Reject` message entry, and escape/reject behavior. If model credentials or dependencies prevent triggering a real prompt, document the blocker and supplement with OpenCode permission state-machine tests plus AVA PTY/RPC permission prompt evidence.

Before area completion:

```sh
cmake --build --preset dev
ctest --preset dev --output-on-failure
git --no-pager diff --check
```

## Progress Log

- 2026-07-03: Initial goal file created. Current status: backend safety strong; UX language and per-tool completion remain active work.

### 2026-07-03 Area Execution

#### Checkpoint Plan

1. Inspect the listed Pi agent/tool/trust paths and AVA agent/tool/permission/TUI/test paths for current behavior.
2. Close product/documentation gaps for control vocabulary, Pi tool alias mapping, permission-rule storage/diagnostics, and parallel-tool disposition without weakening AVA permissions.
3. Add focused deterministic tests for uncovered permission boundaries: network deny paths, non-file persistent rules, write-file permission audits, and remembered-prompt behavior when rule storage is unavailable.
4. Update product baseline/coverage ledgers only for rows backed by tests/docs, then run targeted validation, full CTest, diff check, and material review.

#### Pi/AVA Inspection Summary

- Pi references inspected: `packages/agent/src/agent-loop.ts`, `agent.ts`, `types.ts`, `packages/agent/src/harness/`, `packages/coding-agent/src/core/agent-session.ts`, `packages/coding-agent/src/core/tools/`, `bash-executor.ts`, `modes/interactive/components/tool-execution.ts`, `project-trust.ts`, `trust-manager.ts`, and `docs/security.md` under `docs/reference-code/pi/`.
- AVA references inspected: `src/ava/agent/agent_loop.cpp`, `agent_loop.h`, `assistant_turn.h`, `tool_dispatcher.cpp`, `tool_metadata.h`, `tool_registry.h`, `tool_visibility.h`, `src/ava/tools/`, `src/ava/permissions/`, `src/ava/app/events.h`, `src/ava/tui/event_state.cpp`, `tool_cards.cpp`, `composer_permission.cpp`, and the listed agent/tool/permission/TUI tests.

#### Completed Work

- Documented AVA's agent-control vocabulary in `docs/USAGE.md`: cancel/interrupt, steer-now, queue-next/follow-up, restore draft, and resume-later now have explicit cross-mode semantics.
- Added a Pi-to-AVA tool parity matrix and AVA-only tool safety constraints in `docs/USAGE.md`; parallel/configurable tool execution is explicitly deferred pending replay-safe ordering, permission-audit ordering, and cancellation semantics.
- Expanded permission-rule documentation in `docs/CONFIG.md` with the real enforceable global and workspace-keyed paths, the authoritative `/permissions list`/RPC discovery path, protected-file rationale, command-rule matching, and hard-deny behavior.
- Expanded `docs/headless-protocol.md` permission resolver docs to show `permission_request_id`, `risk`, and `structured_result.permission_request_ids` linkage used by TUI/headless/RPC diagnostics.
- Added regression coverage:
  - `tests/tools_process_network_tests.cpp`: explicit resolver-deny checks for `webfetch` and `websearch`, asserting no transport use after denial.
  - `tests/permission_rules_tests.cpp`: exact command/tool persistent rule matching for non-file `bash` operations.
  - `tests/tools_file_tests.cpp`: `write_file` permission-audit metadata carries the `write_file` tool identity.
  - `tests/tui_composer_tests.cpp`: remembered-rule shortcut is ignored when rule storage is unavailable.
  - `tests/app_rpc_tests.cpp`: model-cycle expectation updated for the DeepSeek provider inserted before Kimi.
- Updated `docs/product/mvp-baseline.md` and `docs/product/mvp-coverage-ledger.md` to mark closed agent-control, tool-card, permission diagnostics/rule UX, and side-effect checklist rows with current evidence.

#### Changed Files

- `docs/USAGE.md`
- `docs/CONFIG.md`
- `docs/headless-protocol.md`
- `docs/product/mvp-baseline.md`
- `docs/product/mvp-coverage-ledger.md`
- `tests/tools_process_network_tests.cpp`
- `tests/permission_rules_tests.cpp`
- `tests/tools_file_tests.cpp`
- `tests/tui_composer_tests.cpp`
- `tests/app_rpc_tests.cpp`

#### Decisions / Deferrals / Exclusions

- Preserved AVA's stronger per-operation permission model; Pi-style no-popup trust remains excluded from AVA MVP behavior.
- Deferred parallel/configurable tool execution. Pi defaults to parallel tool execution, but AVA keeps sequential execution until permission order, audit order, output ordering, cancellation, and session replay semantics are explicitly designed.
- No new side-effect class was added; the existing side-effect checklist remains the required gate for future filesystem, shell, network, provider, credential, plugin, MCP, LSP, config, session, and package/resource changes.

#### Validation

- `cmake --build --preset dev --target ava_tests` — passed.
- `ctest --test-dir build -R 'ava_tests\.(app_rpc|app_rpc_queue|app_rpc_resolver|tools|permission_rules|agent_loop|agent_loop_resilience|agent_tool_dispatcher|tui_composer)$|ava_cli\.headless_rpc_(permission_reply|permission_grant|permission_grant_lifecycle|question_reply|cancel|bash_process_cleanup|tool_failure)$' --output-on-failure` — passed, 16/16.
- `ctest --preset dev --output-on-failure` — passed, 57/57 with credential/TTY-gated skips for `ava_tests.provider_live_smoke`, `ava_tui.tmux_smoke`, `ava_tui.kitty_image_smoke`, and `ava_tui.osc8_smoke`.
- `git --no-pager diff --check` — passed.

#### Material Review Findings

- Correctness/DX review found `ava_tests.app_rpc` failed because the new DeepSeek model changed the built-in model-cycle order while `tests/app_rpc_tests.cpp` still expected Kimi after Anthropic. Fixed by updating the expectation to DeepSeek and verified `ava_tests.app_rpc` plus full CTest.
- Security review found `docs/CONFIG.md` documented the wrong workspace permission-rule directory (`workspaces/<hash>` instead of `workspace-permission-rules/<hash>`), which could lead operators to place unenforced deny rules. Fixed the path and pointed users to `/permissions list`/RPC `permission_rules` as authoritative.
- No remaining material findings after fixes.

#### Residual Risks / Pending Questions

- No blocker for the next area.
- Parallel tool execution remains intentionally deferred and documented; revisit only with an approved replay/permission/cancellation design.
- Opt-in TUI/PTX and live-provider smokes remained skipped by default gates in this non-visual area; full default CTest passed.

### 2026-07-04 OpenCode Closure Pass

#### Reference Inspection Summary

- OpenCode references inspected: `packages/web/src/content/docs/permissions.mdx`, `packages/core/src/permission.ts`, `packages/schema/src/permission.ts`, `packages/schema/src/permission-saved.ts`, `packages/opencode/src/cli/cmd/run/permission.shared.ts`, `footer.permission.tsx`, `tool.ts`, `packages/opencode/src/agent/subagent-permissions.ts`, `packages/web/src/content/docs/agents.mdx`, `packages/core/src/tool/`, `packages/web/src/content/docs/tools.mdx`, `custom-tools.mdx`, `specs/v2/tools.md`, and the listed permission tests under `packages/core/test` and `packages/opencode/test`.
- Pi references rechecked: agent steering/follow-up queues remain one-at-a-time by default, Pi built-ins remain `read`/`write`/`edit`/`bash`/`grep`/`find`/`ls`, and Pi still defaults to parallel tool execution while providing no operation-level permission popup. AVA keeps sequential execution and granular permissions as AVA-superior safety behavior.
- AVA references rechecked: `agent_loop`, `tool_dispatcher`, `tool_metadata`, `tool_visibility`, `src/ava/tools`, `src/ava/permissions`, `src/ava/app/events`, `src/ava/tui/event_state.cpp`, `tool_cards.cpp`, `composer_permission.cpp`, and the matching tests.

#### OpenCode Behavior Notes Applied Or Intentionally Exceeded

- OpenCode's permission prompt vocabulary is `Permission required`, `Allow once`, `Allow always`/`Always`, and `Reject`, with a separate reject-message text stage. AVA adopted the user-facing prompt wording while preserving backend `deny` terminology in durable policy/audit contracts.
- OpenCode's external-directory prompt displays `Access external directory <path>`. AVA now uses that wording in the TUI permission dock for outside-workspace target prompts while keeping the backend reason `target is outside the workspace` for policy tests and structured RPC/headless output.
- OpenCode's `once`/`always`/`reject` replies map to AVA's one-shot allow, remembered exact workspace-scoped allow rule, and one-shot reject. AVA also supports remembered reject rules, which is intentionally stronger than OpenCode's saved-allow-only model. RPC denial `reason` remains AVA's reject-message equivalent; an interactive TUI reject-message editor is explicitly deferred because it would require a new prompt sub-state and model-feedback semantics.
- OpenCode deny-wins behavior and explicit deny enforcement match AVA's hard-deny and persistent-rule precedence: built-in hard denies are evaluated before rules, and matching persistent deny rules win over allows.
- OpenCode subagent permission inheritance is documented as reference-only. AVA has no MVP subagent/task-worker surface; multi-agent orchestration remains deferred in the product baseline.
- OpenCode tool display rules informed AVA's existing tool-card matrix. AVA cards continue to show lifecycle, success/error/canceled state, permission-denied audit details, truncation/spill, changed paths, diffs, and plain/copy-safe payloads where backend events provide them.

#### Closure Matrices

Permission model comparison:

| Concern | Pi | OpenCode | AVA closure |
| --- | --- | --- | --- |
| Project trust | Gates project resources/settings/extensions; not a sandbox and not an operation prompt. | Per-project permission config plus saved allow rules. | Project trust gates executable/model-influencing project resources; tool side effects still use backend `Operation` policy. |
| Policy effects | No built-in operation-level allow/ask/deny popup. | `allow`/`ask`/`deny` rules; deny blocks before prompts. | `PermissionAction::Allow/Ask/Deny` from `decide(PermissionRequest)` plus hard-deny paths for secrets, destructive commands, unsafe plan edits, and protected policy files. |
| Prompt resolver | Not applicable for ordinary tools. | Prompt replies are `once`, `always`, or `reject`. | `PermissionResolver` returns `Allow`, `AllowSessionGrant`, or `Deny`; TUI labels are `Allow once`, `Always`, and `Reject`, and RPC can include a denial reason. |
| Durable rules | Project trust decisions persist; tool permissions do not. | Saved project allow rules; configured denies still win. | Protected global/workspace `PermissionRule` files outside model-writable workspace paths; exact allow and deny rules; malformed/unsafe storage fails closed. |
| Precedence | Trust does not restrict tool execution after start. | Configured deny wins over saved allow. | Built-in hard denies run before persistent rules; persistent deny rules win over allows; headless flags and remembered rules cannot upgrade hard denies. |
| Diagnostics/audit | Tool output and trust docs; no per-operation audit id. | Permission request id and tool-specific prompt display. | Audit entries, `permission_request_id`, risk, reason, resolution source/rule id where applicable, `/permissions audit show`, `/permissions diagnose`, structured RPC/tool details, and TUI/tool-card links. |
| Headless behavior | Non-interactive trust prompts are skipped/controlled by startup config. | Permission requests can be handled by client/protocol. | Fail-closed for `ask` unless explicit safe `--allow`/`--allow-tool`, persistent rule, session grant, or RPC `permission_reply` resolves the request. |

Tool and permission category mapping:

| Capability | Pi | OpenCode reference category | AVA closure |
| --- | --- | --- | --- |
| File read | `read` | `read`, file permission display | `read_file`, `Operation::ReadFile`, bounded output, secret-path hard deny. |
| File write/edit | `write`, `edit` | `write`, `edit`, `apply_patch` diff prompt display | `write_file`, `edit_file`, `apply_patch`, `Operation::EditFile`, diff preview, changed paths, atomic/permissioned writes. |
| Shell | `bash` | `bash` permission/tool display | `bash`, `Operation::RunCommand`, conservative classifier, process cleanup, output caps, destructive/script hard denies. |
| Search/list | `grep`, `find`, `ls` | `grep`, `glob`, `list` | `grep`, `glob`, `list_directory`; Pi aliases `/find` and `/ls`; bounded results and path policy. |
| Network | None in Pi core | `webfetch`, `websearch` | `webfetch`, `websearch`, `network.fetch/search`, explicit approval and no credential forwarding by default. |
| User question | None in Pi core | `question` | `question` resolver in TUI/RPC; fails closed headless without `question_reply`. |
| Skills/resources | Pi skills/prompts/extensions | `skill`, custom/MCP tools | `skill`, plugins, MCP resources/tools/prompts behind trust, permission, audit, and output bounds. |
| LSP/code intel | Not a Pi core tool | `lsp` | Capability-gated LSP diagnostics/symbol/definition/reference tools with `lsp.query` and `lsp.server.launch`. |
| Subagents/tasks | Not AVA MVP | `task`, subagent permission inheritance | Deferred/excluded from AVA MVP until process/session/permission ownership is designed. |
| Parallel/batch tools | Pi default parallel batches | `batch`/tool registry supports concurrent surfaces | Deferred in AVA until replay, permission order, audit order, mutation order, and cancellation semantics are designed. |

#### Completed Work

- Added the durable permission-model and tool/category matrices above to close the explicit Pi/OpenCode/AVA comparison requirement.
- Updated TUI permission dock wording in `src/ava/tui/composer_permission.cpp` from all-caps/deny labels to OpenCode-aligned `Permission required`, `[Reject]`, `[Allow once]`, `[Always]`, and `Esc reject` while retaining `D` as the keyboard shortcut for the backend deny/reject action.
- Added `PermissionPromptView::request_id` in `src/ava/tui/composer.h`, populated it from backend `PermissionPrompt::permission_request_id` in `src/ava/tui/runtime.cpp`, and rendered it in prompt metadata alongside risk and reason.
- Added OpenCode-style external-directory summary wording for outside-workspace permission prompts without changing backend permission decisions or RPC reason strings.
- Updated `tests/tui_composer_tests.cpp` with deterministic assertions for prompt title/action wording, request-id visibility, external-directory wording, remembered `Always`/reject choices, and narrow-width reject controls.
- Updated `tests/tui_tmux_smoke.py` so the required real-terminal smoke asserts the current OpenCode-aligned `Permission required`, `[Reject rule]`, and `[Always]` wording instead of the pre-change all-caps/allow-rule labels.
- Updated `docs/USAGE.md`, `docs/product/mvp-baseline.md`, and `docs/product/mvp-coverage-ledger.md` to record the OpenCode-aligned prompt wording, request-id visibility, reject-message disposition, external-directory wording, and current evidence.

#### Changed Files In This Pass

- `src/ava/tui/composer.h`
- `src/ava/tui/composer_permission.cpp`
- `src/ava/tui/runtime.cpp`
- `tests/tui_composer_tests.cpp`
- `tests/tui_tmux_smoke.py`
- `docs/USAGE.md`
- `docs/product/mvp-baseline.md`
- `docs/product/mvp-coverage-ledger.md`
- `docs/goals/pi-mvp-parity/agent-tools-permissions.md`

#### Validation And Manual Evidence

- `cmake --preset dev` — passed.
- `cmake --build --preset dev --target ava_tests` — passed after the prompt wording changes.
- `ctest --test-dir build -R '^ava_tests\\.tui_composer$' --output-on-failure` — passed; covers OpenCode-aligned prompt title/buttons, request id, external-directory wording, remembered choices, diffs, truncation, permission cards, copy payloads, and narrow/plain rendering.
- `ctest --test-dir build -R 'ava_tests\\.(agent_loop|agent_loop_resilience|agent_tool_dispatcher|tools|permission_rules|app_rpc|app_rpc_resolver|app_rpc_queue|tui_composer)$' --output-on-failure` — passed, 9/9.
- `ctest --preset dev --output-on-failure -R 'permission|tool_visibility|tool_failure|question|bash_process_cleanup|active_run'` — passed, 11/11.
- `AVA_TUI_TMUX_SMOKE=1 ctest --preset dev --output-on-failure -R '^ava_tui\\.tmux_smoke$'` — initially exposed a stale smoke assertion that still expected `PERMISSION REQUIRED`, `Deny rule`, and `Allow rule`; after updating `tests/tui_tmux_smoke.py`, the same command passed, 1/1. This is the required real-terminal AVA evidence for changed permission/tool-card/user-facing flows; the smoke covers permission prompts/audit cards, request id/risk/target visibility, remembered permission rules, deny reuse without a second prompt, `/permissions` list/audit/summary/export/diagnose/show, `/copy permission`, tool/diff/copy cards after real writes, narrow `NO_COLOR` permission-denied detail rows after resize, active-run follow-up/restore, and terminal cleanup/quit behavior.
- OpenCode package-local test attempt: `cd docs/reference-code/opencode/packages/opencode && bun test test/cli/run/permission.shared.test.ts test/permission-task.test.ts test/acp/permission.test.ts` — failed before tests ran with `preload not found "@opentui/solid/preload"`, indicating reference dependencies are not installed in this checkout.
- OpenCode manual TUI attempt: `tmux new-session -d -s opencode-permission "bash -lc 'cd docs/reference-code/opencode/packages/opencode && bun dev; echo EXIT:$?; sleep 30'"` then `tmux capture-pane -pt opencode-permission` — did not reach a permission prompt; capture showed `error: ENOENT while resolving package '@jridgewell/gen-mapping'` and `script "dev" exited with code 1`. Exact next step if reference dependency installation and model credentials are approved: run `cd docs/reference-code/opencode && bun install`, rerun the package-local tests above, then start `bun dev` in tmux and trigger a tool permission prompt to capture `Allow once`, `Always`, `Reject`, reject-message, and escape/reject behavior.

#### Decisions / Deferrals / Exclusions

- TUI reject-message editor deferred with rationale: AVA already accepts bounded RPC denial reasons and preserves them in audit/tool details; adding a TUI text-entry sub-state would broaden prompt state and model-feedback semantics beyond this closure pass.
- Parallel/configurable tool execution remains deferred; no implementation was added because replay order, permission-audit order, output order, mutation ordering, and cancellation semantics are not designed.
- No new side-effect class was added. The side-effect checklist remains the gate for future filesystem, shell, network, provider, credential, plugin, MCP, LSP, config, session, and package/resource side effects.
- OpenCode desktop/web/SaaS/server/package surfaces and subagent/task-worker execution remain outside the AVA local-terminal MVP unless a future product decision scopes them.

#### Final Verification And Material Review

- `cmake --preset dev` — passed.
- `cmake --build --preset dev` — passed.
- `ctest --preset dev --output-on-failure` — passed, 58/58 with expected gated skips for `ava_tests.provider_live_smoke`, `ava_tui.tmux_smoke`, `ava_tui.kitty_image_smoke`, and `ava_tui.osc8_smoke` in the default run.
- `git --no-pager diff --check` — passed.
- Material review: checked correctness, permission bypass risk, destructive-operation risk, DX, code quality, architecture boundaries, test adequacy, manual evidence, and docs consistency against the goal criteria. No material implementation findings remained after the prompt wording/test updates.
- Reviewer subagent result: no material issues; confirmed prompt changes are display/wording-only, default reject behavior and deny/Esc/Ctrl-C/Ctrl-D resolution remain intact, remembered-rule creation still routes through backend-safe callbacks, and deterministic tests cover request ids, external-directory wording, remembered choices, and width bounds. Noted only non-blocking future polish: structured outside-workspace prompt metadata would avoid TUI substring matching, and duplicated runtime status strings could diverge later.
- Security review subagent result: no material security findings; confirmed the change is user-facing wording/request-id/external-directory display only and does not weaken backend permission policy, rule precedence, or trust boundaries.
- Docs audit subagent result: no material docs findings; confirmed all 13 closure topics are consistently covered, including agent control vocabulary, tool/permission matrices, OpenCode prompt wording, rule persistence, hard-deny/deny-wins, tool-card coverage, side-effect checklist, parallel-tools deferral, AVA manual evidence, and OpenCode reference-test blocker/next commands.

#### Residual Risks / Pending Questions

- OpenCode manual permission-prompt capture is blocked by missing reference dependencies and unavailable model/interactive setup in this checkout; the exact blocker and next commands are recorded above, and AVA-side deterministic plus tmux PTY evidence compensates for this goal.
- Broader non-tool denial wording and richer per-tool affordances remain future product polish, not unresolved MVP blockers.
- Parallel tool execution remains intentionally deferred and documented; revisit only with an approved replay/permission/cancellation design.

### 2026-07-04 Backend Aggregate Regression Fix

- During the backend aggregate review for `context-extensions-mcp-lsp.md`, the security pass exposed a real file-read safety gap in this already closed area: `read_file` could open symlinks/non-regular files and could scan oversized files while trying to compute totals.
- Fixed with the smallest backend change in `src/ava/tools/file_io.cpp`: file reads now inspect `symlink_status` before opening, reject symlinks and non-regular paths, and reject files over the safe read cap instead of scanning unbounded input. Added `tests/tools_file_tests.cpp` coverage for symlink rejection and oversized-file rejection.
- Targeted validation after the fix: `ctest --test-dir build -R 'ava_tests\.(core_json_permission|tools|lsp|mcp)$' --output-on-failure` passed. Full backend aggregate validation is recorded in the active context/CLI/settings area files and coverage ledger.
