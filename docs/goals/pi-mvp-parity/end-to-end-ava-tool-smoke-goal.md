# End-To-End AVA Tool Smoke Goal

## Goal Objective

Add a release-grade end-to-end smoke that proves the whole `ava` tool works as a coding agent, not just isolated modules. The smoke must exercise the real binary boundary, provider request/response handling, agent loop, tool dispatch, permissions, session persistence, RPC events, and clean exit.

The default path must be deterministic and run in normal CTest with the fake provider. A live-model variant must be opt-in and credential-gated so real provider behavior can be dogfooded without making release verification depend on secrets or network access.

Suggested Codex command:

```text
/goal Execute docs/goals/pi-mvp-parity/end-to-end-ava-tool-smoke-goal.md. Implement a deterministic fake-provider end-to-end smoke for the full ava binary, add an opt-in live-model dogfood variant, update release/testing docs and evidence, then verify with targeted CTest, full CTest, live-smoke skip/pass classification, and git --no-pager diff --check.
```

## Read First

| Topic | Paths |
| --- | --- |
| Goal package rules | `docs/goals/pi-mvp-parity/index.md`, `docs/goals/pi-mvp-parity/codex-goal-workflow.md` |
| Release evidence | `docs/goals/pi-mvp-parity/testing-release-quality.md`, `docs/product/mvp-coverage-ledger.md`, `docs/operations/testing.md` |
| Headless protocol | `docs/headless-protocol.md` |
| Existing headless smokes | `tests/cli_headless_rpc_*.cmake`, `tests/cli_headless_print_*.cmake`, `tests/cli_headless_performance_smoke.cmake` |
| Fake provider | `tests/support/fake_provider_server.cpp` |
| Live provider smoke | `tests/provider_live_smoke_tests.cpp` |
| Runtime/agent/session path | `src/ava/app/rpc_mode.cpp`, `src/ava/app/runtime.cpp`, `src/ava/agent/agent_loop.cpp`, `src/ava/session/` |
| CTest registration | `tests/CMakeLists.txt` |

Reference repos remain behavior-only inputs. Do not modify `docs/reference-code/pi/` or `docs/reference-code/opencode/`, and do not include either in builds, formatting, broad searches, or generated artifacts.

## Why This Exists

Current evidence is broad but mostly sliced by subsystem:

| Existing evidence | What it proves | What it does not prove alone |
| --- | --- | --- |
| `ava_tests.*` | In-process provider parsing, agent loop, tool dispatch, sessions, permissions, plugins, MCP, TUI reducers | Full CLI/RPC binary startup and a realistic multi-tool conversation in one run |
| `ava_cli.headless_rpc_*` | Individual headless/RPC flows against fake provider | A complete multi-turn coding task that chains tools and validates persisted continuation |
| `ava_tests.provider_live_smoke` | Real provider connectivity when credentials exist | Full tool-using coding-agent behavior with real model output |
| TUI smokes | Real terminal rendering and cleanup | Provider/tool/session end-to-end model flow |

The missing proof is a single realistic smoke that starts `ava`, drives a provider-backed agent task through multiple tool calls, checks emitted RPC events, verifies session structure, and confirms replay through `--continue` or equivalent session reopen.

## 100 Percent Criteria

| Criterion | Required state |
| --- | --- |
| Deterministic fake-provider E2E | Default CTest includes a new full-binary smoke that starts `ava --rpc`, points it at `ava_fake_provider_server`, and drives one realistic multi-turn coding task. |
| Multi-tool workflow | The fake-provider scenario must include at least read/search/edit/process behavior, such as `read_file`, `grep`, `glob` or `list_directory`, `write_file` or `apply_patch`, and `bash`. |
| Permission path | The smoke must prove permission handling for mutating/process tools through RPC permission replies or persistent permission rules, and must assert permission audit/session evidence. Headless allow flags only cover read/search-style tools; do not rely on them for `bash`, `write_file`, `edit_file`, `apply_patch`, or `question`. |
| Session evidence | The smoke must validate persisted entries: user message, assistant message, tool calls, tool results, permission audits where relevant, stats, and replay validation. |
| Provider continuation | Provider request logs must prove tool results are included in follow-up provider requests before the final assistant answer. RPC/session messages may supplement this evidence, but they cannot replace the provider-log assertion. |
| Replay | A second run using `--continue`, `--session`, or RPC session reopen must load the created session and validate it. |
| Live-model dogfood | A credential-gated full-binary live smoke or documented manual command must run a safe temporary-workspace prompt against at least one configured provider and classify the result as pass/skip/auth-blocked/rate-limited/network-blocked/AVA regression. Existing provider transport live smokes remain useful connectivity checks, not substitutes for the full-binary dogfood path. |
| Evidence docs | `docs/operations/testing.md`, `docs/product/mvp-coverage-ledger.md`, and this file record the command, latest local result, skip conditions, and any residual risks. |
| Release command integration | The new fake-provider E2E is part of the normal release verification command set or an explicitly named release-candidate subset. |

## Deterministic Fake-Provider Scenario

Add one scripted fake-provider scenario, for example `end-to-end-workflow`, that produces a stable coding-agent conversation:

1. User prompt: `Fix the TODO and verify the build.`
2. Provider requests `read_file` for a seeded source file.
3. Provider requests `grep` for `TODO` or another seeded marker.
4. Provider requests `glob` or `list_directory` to inspect project shape.
5. Provider requests `write_file`, `edit_file`, or `apply_patch` to change a file in the temporary workspace.
6. Provider requests `bash` for a safe command, preferably a deterministic command that proves the edited file exists or writes a marker under the temp workspace.
7. Provider returns final assistant text summarizing the completed task.

The scenario must use a temporary workspace outside the repository. It must not mutate the checkout. Keep file contents small and assertions semantic rather than byte-for-byte fragile.

## Default CTest Smoke

Implement a new CTest target, tentatively named `ava_cli.headless_e2e_model_smoke`.

Recommended structure:

| File | Expected change |
| --- | --- |
| `tests/support/fake_provider_server.cpp` | Add the deterministic scenario and request log details needed for assertions. Extend the fake-provider argv contract additively if the scenario needs multiple per-tool paths or a workspace root; existing callers using `PORT_FILE REQUEST_LOG DELAY_MS [SCENARIO TARGET_PATH]` must remain valid. |
| `tests/cli_headless_e2e_model_smoke.cmake` | New CMake driver that starts fake provider, starts `ava --rpc`, sends prompt, drains permission prompts in order, waits for final response, and asserts events/session state. |
| `tests/CMakeLists.txt` | Register the smoke with `AVA_EXE`, `AVA_FAKE_PROVIDER_EXE`, isolated `AVA_CLI_TEST_ROOT`, and a conservative timeout. |

Use `--allow read-only` only to reduce prompts for read/search tools. For mutating/process tools, loop until the run completes: poll for each new `permission_requested` event, send the matching `permission_reply`, and continue polling so later prompts cannot be missed or raced.

The smoke should assert visible protocol behavior, not only exit code:

- `tool_start` and `tool_result` events appear for every expected tool.
- Tool result statuses are `success` unless a specific denial branch is intentionally tested.
- Final assistant text appears.
- `get_session_stats`, `get_messages`, `validate_session`, or existing RPC equivalents prove the session is structurally valid.
- Provider request log proves at least one continuation request contained prior tool results.
- Replay through `--continue`, `--session`, or RPC reopen succeeds and still validates.

## Live-Model Dogfood Variant

Add an opt-in live-model path using existing credential gates:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure
AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-model-dogfood.sh
```

The primary live dogfood evidence should start the `ava` binary through a CMake-driver or documented manual command so it covers CLI/RPC startup, provider use, agent loop, tools, permissions, and session persistence. If any code is added inside `tests/provider_live_smoke_tests.cpp`, keep it as a provider-connectivity check unless it explicitly constructs the same agent/tool/session path:

- Use a temporary workspace.
- Use a model profile that supports tools.
- Prefer read/search-only unless mutating behavior is explicitly isolated under the temp workspace.
- Assert non-empty assistant response, no session validation errors, and at least one meaningful provider round trip.
- Do not print credentials or auth files.
- If the selected model does not call tools reliably, classify as inconclusive/provider-behavior and keep the deterministic fake-provider E2E as the release gate.

If code-level live E2E is too flaky or too provider-dependent, document a manual dogfood command in `docs/operations/testing.md` with exact setup, expected evidence, and classification rules.

## Optional Manual/TUI Evidence

This goal is backend/headless-first. TUI evidence is optional unless the implementation touches TUI code.

If TUI behavior is affected, run:

```sh
AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -j 13 -R '^ava_tui\.tmux_smoke_' --output-on-failure
```

Record the relevant capture paths or exact skip reason. Do not add new visual requirements unless code changes affect terminal-visible behavior.

## Non-Goals Unless Approved

| Item | Reason |
| --- | --- |
| Making live credentials mandatory | Deterministic fake-provider E2E is the release gate; live providers are opt-in. |
| Broad provider ecosystem work | Provider breadth is tracked separately. This smoke should use existing provider paths. |
| Full Pi/OpenCode server/API dogfood | AVA's MVP automation contract is stdio JSONL RPC. HTTP/server surfaces remain deferred. |
| TUI rewrite or new visual harness | Existing TUI smokes cover terminal behavior; this goal proves the model/tool/session pipeline. |
| Parallel tool execution | Public/default parallel tools remain deferred for this smoke. The internal read/search AgentLoop opt-in has no CLI/RPC/TUI surface yet, so a full-binary smoke should wait until a public/headless opt-in is added. |

## Validation Commands

Run targeted checks first, then the full release bar:

```sh
cmake --preset dev
cmake --build --preset dev --target ava ava_fake_provider_server ava_tests
ctest --test-dir build -R '^ava_cli\.headless_e2e_model_smoke$' --output-on-failure
ctest --test-dir build -R '^ava_tests\.provider_live_smoke$' --output-on-failure
ctest --preset dev --output-on-failure
git --no-pager diff --check
```

Credential-gated live run:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R '^ava_tests\.provider_live_smoke$' --output-on-failure
AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-model-dogfood.sh
```

## Stop Conditions

Stop and ask only if:

- The smoke needs real credentials, network access, cloud upload, or paid-provider behavior to be a required release gate.
- The deterministic fake provider needs a protocol shape incompatible with current provider parsers.
- A proposed smoke would mutate files outside the temp workspace.
- The live-model dogfood repeatedly fails for provider nondeterminism and no conservative assertion can distinguish provider behavior from AVA regression.

Otherwise, choose the smallest deterministic AVA-native path, document decisions here, and continue.

## Progress Log

| Date | Status | Notes |
| --- | --- | --- |
| 2026-07-04 | Goal added | Created after full parity audit to make whole-tool dogfood a dedicated follow-up instead of relying only on subsystem tests and visual smokes. |
| 2026-07-05 | Fake-provider E2E implemented | Added `end-to-end-workflow` to `ava_fake_provider_server`, registered `ava_cli.headless_e2e_model_smoke`, and verified the focused CTest target locally. The smoke starts the full `ava --rpc` binary, drives read/search/list/apply_patch/bash, answers edit and process permission prompts through RPC, validates persisted session stats/messages/permission decisions, asserts provider continuation requests contain prior tool results, and reopens the session through `--continue`. |
| 2026-07-05 | Live dogfood path added | Added `scripts/live-model-dogfood.sh` as the opt-in full-binary live-model dogfood command. Local result with `AVA_LIVE_PROVIDER_SMOKE=1` was `classification=skipped/no credential`; no provider credentials were present. |
| 2026-07-05 | Verification passed | `cmake --preset dev`, `cmake --build --preset dev --target ava ava_fake_provider_server ava_tests`, `ctest --test-dir build -R '^ava_cli\.headless_e2e_model_smoke$' --output-on-failure`, ungated and credential-gated `ctest --test-dir build -R '^ava_tests\.provider_live_smoke$' --output-on-failure`, `AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-model-dogfood.sh`, `ctest --preset dev --output-on-failure`, and `git --no-pager diff --check` passed or skipped with documented prerequisites. Full default CTest passed 59/59 with expected provider-live and opt-in TUI PTY skips. |

## Residual Risks To Track

- A scripted fake provider proves AVA's plumbing, not model judgment quality.
- Live model tool-use behavior can vary by provider/model and may need classification rather than hard pass/fail.
- Evidence under `build/` is regenerated and ignored by git unless copied into docs or explicitly committed elsewhere.
- The live dogfood script classifies model non-compliance with the requested read-only tool call as `provider-behavior/inconclusive`; the deterministic fake-provider E2E remains the release gate.
