# Context, Prompts, Skills, Extensions, MCP, And LSP

## Goal Objective

Close Pi parity for context loading, prompt resources, skills, extensions/plugins, and related developer ergonomics, while preserving AVA's out-of-process plugin isolation, MCP permissions, and LSP safety boundaries.

Suggested Codex command:

```text
/goal Bring AVA context/prompts/extensions/MCP/LSP parity to documented closure using docs/goals/pi-mvp-parity/context-extensions-mcp-lsp.md. First inspect Pi resource loading and extension docs plus AVA context/plugin/MCP/LSP code, then implement or defer every 100 percent criterion with tests and docs. Stop for approval before adding remote installs, dynamic provider plugins, or project-local executable authority without trust policy.
```

## Pi References To Inspect First

| Topic | Pi paths |
| --- | --- |
| Resource loading | `docs/reference-code/pi/packages/coding-agent/src/core/resource-loader.ts` |
| System prompt | `docs/reference-code/pi/packages/coding-agent/src/core/system-prompt.ts` |
| Prompt templates | `docs/reference-code/pi/packages/coding-agent/src/core/prompt-templates.ts`, `docs/prompt-templates.md` |
| Skills | `docs/reference-code/pi/packages/coding-agent/src/core/skills.ts`, `docs/skills.md` |
| Extensions | `docs/reference-code/pi/packages/coding-agent/src/core/extensions/`, `docs/extensions.md` |
| Package resources | `docs/reference-code/pi/packages/coding-agent/src/core/package-manager.ts`, `docs/packages.md` |
| Project trust | `docs/reference-code/pi/packages/coding-agent/src/core/project-trust.ts`, `trust-manager.ts` |
| Tests | Pi extension, compaction-extension, prompt-template, skills, and settings tests |

## AVA References To Inspect First

| Topic | AVA paths |
| --- | --- |
| Context | `src/ava/context/context_loader.cpp`, `src/ava/context/skill_loader.cpp` |
| Prompt config | `src/ava/config/prompt_config.cpp`, `src/ava/app/prompt_resources.cpp` if present |
| Command registry | `src/ava/app/command_registry.cpp`, `command_catalog.cpp`, `command_plugins.cpp` |
| Plugins | `src/ava/plugin/` |
| Plugin events | `src/ava/app/plugin_event_hooks.cpp` |
| MCP | `src/ava/mcp/` |
| LSP | `src/ava/lsp/`, `src/ava/tools/lsp_tools.cpp` |
| Project trust | `src/ava/app/project_trust.cpp` |
| Tests | `tests/config_context_auth_oauth_tests.cpp`, `tests/plugin_tests.cpp`, `tests/mcp_tests.cpp`, `tests/lsp_tests.cpp`, `tests/app_command_registry_tests.cpp` if present |

## Current Gap Summary

AVA has strong context, skills, plugin, MCP, and LSP foundations. Gaps are mostly Pi product ergonomics: package/resource manager, plugin command/prompt/skill model-tool exposure decisions, MCP prompt exposure decisions, persistent plugin/MCP process pooling decisions, prompt/template UX, and trust docs for project-local resources.

## 100 Percent Criteria

| Criterion | Required AVA State |
| --- | --- |
| Context files | Global/project context file behavior is documented, tested, bounded, and trust-aware where execution or prompt authority changes. |
| Prompt resources | Pi-style `SYSTEM.md`, `APPEND_SYSTEM.md`, prompt commands/templates, CLI overrides, and plugin prompt resources are implemented or explicitly deferred. |
| Skill UX | Skills are discoverable, loadable, bounded, trust-aware, and documented. The model-visible `skill` tool behavior is tested. |
| Plugin parity decision | Pi extension capabilities are mapped to AVA plugin equivalents: tools, commands, prompts, skills, events, UI, keybindings, custom providers. Each is implemented, AVA-superior, deferred, or excluded. |
| MCP parity decision | Tools/resources/prompts are documented and tested. Advanced transports/OAuth/subscriptions/sampling/templates/binary resources are implemented or explicitly deferred. |
| LSP maturity | Diagnostics, symbols, definition, references, config, server launch permissions, and TUI/headless display are covered. Automatic recipes/unsaved sync are implemented or deferred. |
| Trust policy | Project-local executable/config resources cannot become active without explicit outside-workspace trust. |
| Tests | Plugin/MCP/LSP/context changes have unit tests plus CLI/RPC smoke coverage where user-visible. |

## Implementation Slices

| Slice | Work |
| --- | --- |
| E1. Resource matrix | Map Pi resources to AVA: context, prompt templates, skills, extensions, packages, themes, custom providers, MCP/LSP. |
| E2. Prompt/template closure | Finish docs/tests for prompt resources, command args, context freshness, and deferred shell-output injection. |
| E3. Plugin capability closure | Decide whether plugin commands/prompts/skills become model-visible tools or stay CLI/RPC only. Document performance tradeoff of process-per-call. |
| E4. MCP prompt/resource closure | Decide and implement or defer MCP prompt exposure as model tools. Keep blob/binary safeguards. |
| E5. LSP maturity | Add automatic server recipes only if product-approved. Otherwise document explicit config as MVP. |
| E6. Trust docs | Make project trust implications visible in `docs/CONFIG.md`, `docs/plugin-system.md`, and product baseline. |

## Non-Goals Unless Approved

| Item | Reason |
| --- | --- |
| Remote plugin/package marketplace | Requires trust, signing, provenance, compatibility, and rollback design. |
| Custom provider plugins | Provider auth/model metadata are security-sensitive. Defer to provider area. |
| Persistent plugin/MCP daemon pooling | May improve performance but changes isolation and lifecycle semantics. |
| Advanced MCP HTTP/OAuth/subscription support | Valuable later, not required for bounded local MVP. |

## Verification

Targeted commands:

```sh
ctest --test-dir build -R 'ava_tests\.(config_context_auth_oauth|plugin|mcp|lsp|agent_tool_dispatcher|app_runtime)$' --output-on-failure
ctest --test-dir build -R 'ava_cli\.headless_rpc_(plugin|mcp|command|context)' --output-on-failure
```

Before area completion:

```sh
cmake --build --preset dev
ctest --preset dev --output-on-failure
git --no-pager diff --check
```

## Progress Log

- 2026-07-03: Initial goal file created. Current status: core systems implemented; package/resource and exposure decisions remain open.

### 2026-07-03 Area Execution

#### Checkpoint Plan

1. Inspect the listed Pi resource loader/system prompt/prompt-template/skills/extensions/package/trust paths and the matching AVA context/plugin/MCP/LSP paths/tests.
2. Close minimal implementation gaps that are AVA-native and safe: Pi-compatible context file aliases and any trust-boundary holes found during review.
3. Document durable product decisions for plugin capabilities, MCP prompt/resource exposure, LSP recipes, trust policy, and package/remote install deferrals.
4. Update product docs/coverage ledgers and run targeted context/plugin/MCP/LSP validation plus full CTest and diff check.

#### Pi/AVA Inspection Summary

- Pi references inspected: `resource-loader.ts`, `system-prompt.ts`, `prompt-templates.ts`, `skills.ts`, `core/extensions/`, `package-manager.ts`, `project-trust.ts`, `trust-manager.ts`, and the related Pi docs for prompt templates, skills, extensions, packages, and security.
- AVA references inspected: `src/ava/context/context_loader.cpp`, `skill_loader.cpp`, `src/ava/config/prompt_config.cpp`, command registry/catalog/plugin command files, `src/ava/plugin/`, `plugin_event_hooks.cpp`, `src/ava/mcp/`, `src/ava/lsp/`, `src/ava/tools/lsp_tools.cpp`, `src/ava/app/project_trust.cpp`, and the listed context/plugin/MCP/LSP/command-registry tests.

#### Completed Work

- Added Pi-compatible context file aliases in `src/ava/context/context_loader.cpp`: per directory AVA now uses first-present priority `AGENTS.md`, `AGENTS.MD`, `CLAUDE.md`, `CLAUDE.MD`; global context falls back to sibling aliases when the default `AGENTS.md` path is absent.
- Added `tests/config_context_auth_oauth_tests.cpp` coverage for workspace `CLAUDE.md`, same-directory `AGENTS.MD` priority over `CLAUDE.md`, and global `CLAUDE.MD` fallback.
- Added a trust-boundary hardening after security review:
  - `src/ava/lsp/configured_provider.cpp` rejects workspace-relative executable/script argv entries from global LSP config; project LSP config can still use them after project trust gates the config.
  - `src/ava/mcp/config.cpp` rejects workspace-relative executable/script command or arg entries from global MCP config; project MCP config can still use them after project trust gates the config.
  - Added LSP/MCP tests for those global-reject/project-allowed paths.
- Added `docs/mcp.md` as a standalone MCP support/safety summary covering config, supported stdio/tool/resource/prompt surface, permission categories, text-only resource reads, and advanced deferred scope.
- Updated `docs/plugin-system.md` with a Pi extension capability disposition matrix: tools, commands, prompts, skills, events implemented; UI slots, plugin keybindings, plugin themes, custom providers/request interception, packages/remote install deferred.
- Updated `docs/CONFIG.md`, `docs/USAGE.md`, `docs/product/mvp-baseline.md`, and `docs/product/mvp-coverage-ledger.md` for `AGENTS.md`/`CLAUDE.md` context behavior, MCP docs evidence, and LSP/MCP global-vs-project executable path rules.

#### Changed Files

- `src/ava/context/context_loader.cpp`
- `src/ava/lsp/configured_provider.cpp`
- `src/ava/mcp/config.cpp`
- `tests/config_context_auth_oauth_tests.cpp`
- `tests/lsp_tests.cpp`
- `tests/mcp_tests.cpp`
- `docs/CONFIG.md`
- `docs/USAGE.md`
- `docs/mcp.md`
- `docs/plugin-system.md`
- `docs/product/mvp-baseline.md`
- `docs/product/mvp-coverage-ledger.md`

#### Decisions / Deferrals / Exclusions

- Plugin capabilities: AVA keeps plugin tools/commands/prompts/skills/events implemented through out-of-process manifests/protocol. Plugin UI render slots, plugin keybindings, plugin themes, custom providers, and provider/request interception remain deferred until isolation, input-conflict, trust, and provider-auth contracts exist.
- MCP prompts: implemented as command-registry slash/RPC commands, not automatic model-visible tools. MCP resources stay opaque no-argument read-style tools requiring `mcp.resource.read`; binary/blob/template/subscription/sampling/HTTP/OAuth surfaces are deferred.
- LSP: explicit config remains the MVP path. Automatic server recipes and broader unsaved/incremental sync are deferred; diagnostics, symbols, definitions, references, launch permission, and output bounds remain covered.
- Project trust: project-local executable/config resources (`.ava/commands`, skills, plugins, MCP/LSP config, `SYSTEM.md`, `APPEND_SYSTEM.md`) remain inactive until `/trust project`. Context instruction files are visible instructions and load without trust.
- Packages/remote install/custom provider packages remain deferred pending provenance, signing, compatibility, rollback, and trust design.

#### Validation

- `cmake --build --preset dev --target ava_tests` — passed.
- `ctest --test-dir build -R 'ava_tests\.(config_context_auth_oauth|plugin|mcp|lsp|agent_tool_dispatcher|app_runtime|app_command_registry)$|ava_cli\.headless_rpc_(plugin_commands|sample_plugin|mcp_commands|mcp_config_errors|command_registry|context_export)$' --output-on-failure` — passed, 13/13.
- `ctest --preset dev --output-on-failure` — passed, 57/57 with credential/TTY-gated skips for `ava_tests.provider_live_smoke`, `ava_tui.tmux_smoke`, `ava_tui.kitty_image_smoke`, and `ava_tui.osc8_smoke`.
- `git --no-pager diff --check` — passed.

#### Material Review Findings

- Correctness/DX/code-quality review found no material findings for context alias ordering, docs/code consistency, MCP prompt/resource disposition, or plugin capability docs.
- Initial security review found global MCP/LSP config could launch explicit workspace-relative code because subprocesses `chdir` to the workspace before `execvp`. Fixed first by rejecting workspace-relative executable/script command/argv entries in global MCP/LSP config and documenting that project-local server code belongs in trusted project config.
- No remaining material findings after that initial pass; the later backend aggregate review below found and fixed the remaining process-CWD and file-read safety gaps before closure.

#### Residual Risks / Pending Questions

- No blocker for the next area.
- Advanced plugin/MCP/package/provider-extension surfaces remain intentionally deferred and documented.
- Opt-in TUI/PTY and live-provider smokes remained skipped by default gates in this non-visual area; full default CTest passed.

### 2026-07-04 Backend Aggregate Closure Audit

- Re-audited this area against the backend aggregate goal after the CLI/session and settings/package areas were closed. Every 100 percent criterion above is either implemented/AVA-superior with evidence or explicitly deferred/excluded with rationale; no frontend/TUI/editor implementation is required for this backend-only closure.
- Current aggregate verification rerun before material-review fixes: `cmake --preset dev`, `cmake --build --preset dev`, the context/plugin/MCP/LSP targeted CTest command, `ctest --preset dev --output-on-failure`, and `git --no-pager diff --check` all passed locally. Default-gated live-provider and opt-in TUI/PTY smokes remained skipped, which is acceptable for this non-visual backend area.
- Aggregate material review found two blocking backend safety gaps and both were fixed before closure: MCP/LSP global process launch now shares the workspace-relative argv detector and runs global servers from a safe non-workspace config CWD, with project-scoped servers still allowed to use trusted workspace CWD; LSP project-relative coverage was added to mirror MCP. The same review exposed a real file-read DoS risk in the already closed tools area; the smallest fix rejects symlink/non-regular/oversized read paths before opening and is recorded in `agent-tools-permissions.md`. Fix files: `src/ava/core/process_args.*`, `src/ava/mcp/stdio_client*`, `src/ava/lsp/*`, `src/ava/tools/file_io.cpp`, matching fake servers/tests, `docs/CONFIG.md`, and `docs/mcp.md`.
- Final verification after material-review fixes: `cmake --preset dev`, `cmake --build --preset dev`, all three backend-area targeted CTest commands, the focused safety regression CTest command, `ctest --preset dev --output-on-failure`, and `git --no-pager diff --check` passed locally.
