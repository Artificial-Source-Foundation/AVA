# C++ Milestone 29 Boundaries

Milestone 29 closes the first long-run runtime parity gap in the C++ backend/headless lane with a smallest-honest budget, queue, and structural compaction slice. It gives the C++ runtime practical controls for runaway cost, queued follow-up work, and oversized context growth without claiming full Rust condenser parity.

## In Scope

1. **Budget tracking and stop behavior:** `AgentRuntime` now accumulates provider token usage, estimates cost through the active provider, emits budget warnings at 50/75/90 percent, persists budget metadata, and stops before more LLM work when the configured budget is exhausted.
2. **Headless budget CLI surface:** the C++ CLI accepts `--max-budget`, stores the runtime selection metadata, and reports `budget_exceeded` as a terminal reason.
3. **Follow-up/post-complete queue promotion:** the runtime now drains `follow_up` messages after a natural assistant completion and then drains post-complete groups in order, while respecting budget exhaustion before promoting queued work.
4. **Structural runtime compaction:** when `auto_compact` is enabled and visible context exceeds the configured threshold, the runtime marks older active-branch messages `agent_visible=false`, records compaction metadata, and omits compacted messages from future provider prompts.
5. **Backend event projection:** headless event rendering now knows about `budget_warning` and `context_compacted` events; TUI state and native subagent summaries understand `budget_exceeded` as a completion reason.
6. **Focused tests:** `ava_agent_tests` covers budget accumulation, warnings, budget stop/skipped queued work, follow-up/post-complete promotion, and structural compaction prompt filtering. App/orchestration tests keep the CLI and composition seams exercised.

## Out of Scope

1. Full Rust hybrid/LLM summarization compaction, relevance scoring, context-overflow retry recovery, and provider-specific overflow parsing.
2. JSONL session logging, checkpoint recovery, session repair, interrupted-tool cleanup, bookmarks, and broader session persistence hardening.
3. Subagent or background-run budget rollups. M29 tracks the foreground runtime only.
4. Desktop/web/TUI budget UX beyond the minimal event/state compatibility needed to compile and avoid stale status handling.
5. Durable queue scheduling outside the blocking backend/headless runtime. M29 promotes already-populated queue entries in-process.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_agent_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_orchestration_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_integration_tests "~[live]"
git diff --check
```

## Decision Point

After M29, the remaining long-run runtime decision is whether to add semantic LLM summarization/context-overflow retry in C++ before session recovery (M30), or keep compaction structural until the session repair and provider-streaming milestones are closed.
