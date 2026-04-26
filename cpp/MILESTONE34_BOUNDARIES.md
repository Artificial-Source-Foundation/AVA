# C++ Milestone 34 Boundaries — Hardening, Soak, And Final Evidence

Milestone 34 is the final scoped hardening pass for the current C++ backend/headless migration lane. It should make the codebase look intentional to an expert C++ reviewer: fix avoidable god files when safe, document larger refactors explicitly, avoid milestone-labelled runtime data, avoid vague names in new seams, and avoid unsupported parity claims.

## In Scope

1. **Production-code shape review:** inspect active C++ production files for god-function growth, weak ownership boundaries, duplicated helpers, magic milestone strings, and vague naming. Fix safe issues directly and document larger refactors that should not be rushed.
2. **Runtime hotspot containment:** specifically review `AgentRuntime`, provider streaming code, headless CLI composition, and core-tool implementations for local maintainability and narrow owner seams.
3. **Focused soak evidence:** run the full C++ debug build and focused non-live test lanes covering app, agent, orchestration, TUI, tools, config, LLM, MCP, session, and control-plane surfaces as locally feasible.
4. **Contract evidence sweep:** verify milestone boundary docs, backlog notes, changelog entries, and known deferred parity buckets are consistent and do not overclaim full Rust/web/desktop parity.
5. **Final residual-risk list:** leave a concise list of intentional deferrals and high-value next refactors, especially any remaining god-function/module concerns.

## Out Of Scope

1. New feature scope beyond fixes required by quality review or failing evidence.
2. Broad Rust parity closure for web, desktop, plugin, OAuth/keychain, long-tail providers, semantic compaction, JSONL audit logging, or async/background runtime scheduling.
3. Large risky rewrites without focused test coverage. If `AgentRuntime::run()` needs a deep extraction, M34 may document it as a required follow-up unless it can be done safely within the evidence window.
4. Live-provider/network soak by default; live tests remain opt-in through existing environment gates.

## Validation Commands

```bash
git diff --check
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ctest --preset cpp-debug --output-on-failure
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_integration_tests "~[live]"

# Focused non-live evidence lanes when narrowing a failure:
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_tests "[ava_app]"
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_agent_tests "[ava_agent]"
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_llm_tests "[ava_llm]"
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_tools_tests "[ava_tools]"
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_mcp_tests
```

## Evidence Checklist

1. Full C++ debug build succeeds after M31-M34 changes.
2. Non-live app-boundary integration lane stays green with live-provider tests excluded by `~[live]`.
3. Focused app, agent, LLM, tools, and MCP suites stay green after hardening edits.
4. Code-shape review either fixes tiny high-impact issues or records larger refactors as explicit follow-up work.
5. Boundary docs, backlog, changelog, and README describe the same scoped C++ backend/headless claim without implying web/desktop/full Rust parity.
6. Residual risks below are accepted deferrals, not accidental omissions.

## Residual Risks And Follow-Up Refactors

1. Web, desktop, plugin runtime, OAuth/keychain, browser tools, custom TOML tools, MCP HTTP/SSE/OAuth, and long-tail providers remain outside this scoped C++ backend/headless lane.
2. Semantic/LLM summarization compaction, JSONL audit logging, bookmarks/search, and cross-runtime UUID normalization remain deferred.
3. Live-provider soak remains opt-in; default evidence uses deterministic mock/provider-free lanes.
4. `AgentRunLoop` is split out of `runtime.cpp` and stack-allocated, but deeper phase-level extraction remains a future refactor if the loop grows again.
5. `core_tools.cpp` remains a large multi-tool translation unit; splitting tools by file is a future maintainability refactor.
6. SQLite session operations still open short-lived connections per operation; a persistent connection/pool is deferred until there is soak evidence that the current path is insufficient.
7. Shell execution still uses the portable shell wrapper rather than a shared direct process-runner abstraction; stdout/stderr split and fully portable timeout enforcement remain future work.
8. Signal handling remains cooperative cancellation, not hard-kill crash recovery.

## Acceptance Bar

M34 is complete only when the scoped C++ backend/headless claim is evidence-backed, documentation matches code, and remaining gaps are explicit deferrals rather than accidental omissions. Code that would look careless to an expert C++ reader should either be fixed or called out as an intentional follow-up with a narrow reason.
