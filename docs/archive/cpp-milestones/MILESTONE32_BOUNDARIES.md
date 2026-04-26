# C++ Milestone 32 Boundaries — Headless CLI/Tool Polish Narrow Slice

## In Scope

1. Add daily-use headless CLI/config parity for `--cwd`, `--agent`, and `--trust`.
2. Respect automation environment defaults for `AVA_WORKING_DIRECTORY`, `AVA_PROVIDER`, `AVA_MODEL`, and `AVA_AGENT` when explicit CLI flags are absent.
3. Route the resolved workspace root through shared runtime composition and persist it in headless metadata.
4. Persist trusted workspaces through the existing trusted-project store when `--trust` is used.
5. Validate `--agent` against existing built-in C++ agent templates and apply that template's max-turn default when `--max-turns` is not explicit.
6. Improve low-risk core-tool metadata parity: parameter descriptions, search hints, read line-number formatting, read size limit, truncation hints, git read-only error guidance, and edit preflight ordering.
7. Add focused CLI/orchestration/tool unit coverage for the new surface.

## Out Of Scope

1. Full YAML `config.yaml` loading for primary agents, provider defaults, and thinking settings.
2. Custom TOML tool execution, MCP HTTP/SSE/OAuth, plugin runtime parity, and web/browser tools.
3. New `auth`, `serve`, `plugin`, benchmark, or multimodal CLI subcommands/flags.
4. Long-tail provider implementation breadth beyond the existing C++ provider runtime.
5. Full Rust primary-agent prompt layering; this slice only validates/selects built-in agent IDs and applies simple max-turn defaults.
6. Reworking shell execution to split stdout/stderr or to share a process-runner abstraction.

## Validation Commands

```bash
git diff --check
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_tests "[ava_app]"
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_tools_tests "[ava_tools]"
```

## Decision Point For M33

M33 should move from polish into a headless integration proof lane: exercise the C++ CLI with real runtime composition, scripted tool loops, workspace override/trust behavior, and session persistence evidence without broadening into web/desktop parity.
