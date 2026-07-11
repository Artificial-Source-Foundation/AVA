# Agent Tools Permissions Goal

## Objective

Bring AVA agent/tools/permissions parity to documented closure using `docs/goals/pi-mvp-parity/agent-tools-permissions.md`.

Pi remains the primary coding-agent baseline. OpenCode at `docs/reference-code/opencode` is a required co-primary reference for agent/tool permission UX, approval modes, saved rules, per-agent permissions, permission prompt state, and manual permission-prompt behavior.

## Read First

Read these before implementing:

| Purpose | Path |
| --- | --- |
| Goal index | `docs/goals/pi-mvp-parity/index.md` |
| Codex goal workflow | `docs/goals/pi-mvp-parity/codex-goal-workflow.md` |
| Area goal and progress log | `docs/goals/pi-mvp-parity/agent-tools-permissions.md` |
| Product baseline | `docs/product/mvp-baseline.md` |
| Coverage ledger | `docs/product/mvp-coverage-ledger.md` |
| Existing root plan | `goals/ava-mvp-baseline-pi-tui/plan.md` |
| Existing work ledger | `goals/ava-mvp-baseline-pi-tui/mvp-work-ledger.md` |

## Reference Repositories

Inspect Pi paths listed in `docs/goals/pi-mvp-parity/agent-tools-permissions.md`.

Inspect OpenCode paths listed in `docs/goals/pi-mvp-parity/agent-tools-permissions.md`, especially:

| Purpose | Path |
| --- | --- |
| Permission docs | `docs/reference-code/opencode/packages/web/src/content/docs/permissions.mdx` |
| Permission service | `docs/reference-code/opencode/packages/core/src/permission.ts` |
| Permission schema | `docs/reference-code/opencode/packages/schema/src/permission.ts` |
| Permission UI state machine | `docs/reference-code/opencode/packages/opencode/src/cli/cmd/run/permission.shared.ts` |
| Permission footer UI | `docs/reference-code/opencode/packages/opencode/src/cli/cmd/run/footer.permission.tsx` |
| Tool permission display | `docs/reference-code/opencode/packages/opencode/src/cli/cmd/run/tool.ts` |
| Subagent permissions | `docs/reference-code/opencode/packages/opencode/src/agent/subagent-permissions.ts` |
| Tool architecture | `docs/reference-code/opencode/packages/core/src/tool/` |
| Permission tests | `docs/reference-code/opencode/packages/core/test/permission.test.ts`, `docs/reference-code/opencode/packages/opencode/test/cli/run/permission.shared.test.ts` |

Use reference repos for behavior only. Do not copy Pi or OpenCode source or architecture into AVA. Do not include `docs/reference-code` in AVA builds, formatting, or broad source searches except for explicit reference analysis.

## Scope

Work one area only: agent/tools/permissions.

Do not move to the next goal file. Close every item in `agent-tools-permissions.md` by implementing, marking AVA-superior, deferring, or excluding with rationale. Preserve AVA's stricter safety model even when Pi or OpenCode is looser.

## Required Closure Topics

| Topic | Required closure |
| --- | --- |
| Agent control language | TUI, RPC, print/headless, and docs clearly distinguish stop, cancel, interrupt, steer-now, queue-next, restore draft, and resume-later behavior. |
| Tool parity matrix | Map Pi, OpenCode, and AVA tools and permission categories. Document AVA-only tools and safety constraints. |
| Permission model matrix | Compare Pi trust, OpenCode `allow`/`ask`/`deny`, and AVA `Operation`/`PermissionRule`/`PermissionResolver` flows. |
| Permission prompt wording | Use OpenCode as UX reference for `Permission required`, `Allow once`, `Always`, `Reject`, reject-message, and external-directory wording while preserving AVA safety. |
| Rule persistence UX | Verify remembered rules, deny-wins behavior, hard-deny behavior, headless behavior, and safe failure modes. |
| Tool cards | Ensure running, success, failure, denied, canceled, changed paths, diffs, spill/truncation, plain/headless, and metadata representations are covered. |
| Side-effect checklist | Cover filesystem, shell, network, plugin, MCP, LSP, credential, provider, session, and package/resource side effects. |
| Parallel tool decision | Implement only with proven replay/order/cancel/permission semantics, otherwise explicitly defer. |
| Manual AVA testing | Required after implementation for changed user-facing flows. CTest alone is not enough. |
| Manual OpenCode reference testing | Required when feasible, or document the exact blocker. |

## Implementation Rules

Use subagents in parallel only for non-overlapping exploration, review, or implementation inside this area.

Keep implementation minimal and C++23-native. Preserve backend safety boundaries. Policy belongs in permissions/tools/app policy layers, not only in TUI.

Avoid broad rewrites, copied reference architecture, god files, duplicate policy logic, weakened secret/destructive/external path checks, hidden shell execution paths, or UI-only safety decisions.

Do not add parallel tool execution unless permission order, audit order, output ordering, cancellation, mutation ordering, and session replay semantics are explicitly designed and tested.

## Required Documentation Updates

Update `docs/goals/pi-mvp-parity/agent-tools-permissions.md` as the durable progress log. Record completed work, changed files, Pi/OpenCode/AVA comparison notes, decisions, deferrals/exclusions, validation commands, manual evidence, review findings, residual risks, and pending questions.

If the file gets too large, split phase notes into `docs/goals/pi-mvp-parity/agent-tools-permissions.phase-01.md` and link them from the main file.

Also update `docs/product/mvp-baseline.md` and `docs/product/mvp-coverage-ledger.md` whenever status or evidence changes.

## Targeted AVA Validation

Run targeted AVA validation:

```sh
cmake --build --preset dev --target ava_tests
ctest --test-dir build -R 'ava_tests\.(agent_loop|agent_loop_resilience|agent_tool_dispatcher|tools|permission_rules|app_rpc|app_rpc_resolver|app_rpc_queue|tui_composer)$' --output-on-failure
ctest --preset dev --output-on-failure -R 'permission|tool_visibility|tool_failure|question|bash_process_cleanup|active_run'
```

## Manual AVA Validation

Manual AVA validation is required for changed user-facing flows. Capture or document:

| Flow | Evidence |
| --- | --- |
| Permission prompt | Allow-once, deny, request id/risk/target visibility, and terminal cleanup. |
| Remembered permission rule | Matching request behavior and documented rule scope. |
| Deny-wins/hard-deny | Unsafe operations still blocked even when remembered rules exist. |
| Tool cards | Running, success, failure, denied, canceled, changed paths, diff preview, spill/truncation, and plain/headless fallback where affected. |
| Agent control | Interrupt/cancel plus steer-now or queue-next/restore-draft if affected. |
| RPC/headless | Permission request/reply/grant lifecycle with request ids and structured result links. |

Use PTY/tmux/Python evidence where feasible:

```sh
AVA_TUI_TMUX_SMOKE=1 ctest --preset dev --output-on-failure -R tui
```

If a real terminal, tmux, provider credential, or interactive prompt cannot be exercised, record the exact blocker in the goal file, provide the command or script that should be run next, and compensate with deterministic CTest/RPC coverage plus source-level review.

## Manual OpenCode Reference Validation

If dependencies are available, run OpenCode tests from the package directory, not the repo root:

```sh
cd docs/reference-code/opencode/packages/opencode
bun test test/cli/run/permission.shared.test.ts test/permission-task.test.ts test/acp/permission.test.ts
```

If feasible, run a manual OpenCode TUI reference smoke and capture the permission UI:

```sh
tmux new-session -d -s opencode-permission 'cd docs/reference-code/opencode/packages/opencode && bun dev'
tmux capture-pane -pt opencode-permission
tmux kill-session -t opencode-permission
```

For a complete manual permission comparison, capture or describe at least one OpenCode permission prompt flow: initial permission request, `Allow once`, `Always` confirmation, `Reject` message entry, and escape/reject behavior.

If OpenCode dependencies, model credentials, real terminal, tmux, or an interactive permission prompt cannot be exercised, document the exact blocker and supplement with OpenCode source/test inspection plus AVA manual/PTY/RPC evidence.

## Final Verification

Before area completion:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
git --no-pager diff --check
```

Before marking complete, do a material review for correctness, permission bypass risk, destructive operation risk, DX, code quality, architecture boundaries, test adequacy, manual evidence, and docs consistency.

Keep going until this area is closed with evidence or explicit documented deferrals/exclusions.
