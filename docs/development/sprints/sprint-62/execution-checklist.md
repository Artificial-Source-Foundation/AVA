# Sprint 62 Execution Checklist

> Last updated: 2026-03-12
> Preferred implementation model: background agents using Codex 5.3 Spark for development passes (mapped to `openai/gpt-5.3-codex` unless a Spark-specific model ID is configured), with final verification on `master`

## Goal

Implement Sprint 62 in a way that is parallelizable, testable, and safe to merge back into the main branch.

## Scope

- `B64` Thinking budget configuration
- `B63` Dynamic API key resolution
- `B47` Cost-aware model routing
- `B40` Budget alerts and cost dashboard

## Recommended Worktree Strategy

- Keep `master` as the integration branch.
- Give each substantial workstream its own short-lived branch/worktree during implementation.
- Merge or cherry-pick validated work back only after the crate-level tests pass.
- Remove worktrees only after their changes are committed or intentionally discarded.

## Background-Agent Workstreams

### Workstream A - Thinking Budgets (`B64`)

Agent focus:

- Add config types for quantitative thinking budgets
- Map those budgets into provider/runtime request settings
- Expose safe fallback behavior when a provider cannot honor the requested budget

Likely files:

- `crates/ava-config/`
- `crates/ava-llm/`
- `crates/ava-tui/` for user-facing controls/status

Verification:

```bash
cargo test -p ava-config
cargo test -p ava-llm thinking
npx tsc --noEmit
```

Exit criteria:

- Budget configuration is persisted in config and reaches the runtime/provider request layer.
- At least one non-zero thinking-budget-focused test runs in `ava-llm`.

### Workstream B - Dynamic Key Refresh (`B63`)

Agent focus:

- Resolve credentials closer to request time
- Support refreshable OAuth-backed providers without restart
- Preserve existing static API-key behavior

Likely files:

- `crates/ava-auth/`
- `crates/ava-config/`
- `crates/ava-llm/`

Verification:

```bash
cargo test -p ava-auth
cargo test -p ava-llm providers
cargo test -p ava-config credentials
```

Exit criteria:

- Refreshable providers resolve credentials at request time without breaking static API-key providers.
- At least one non-zero credential-refresh-focused test runs in `ava-auth`, `ava-llm`, or `ava-config`.

### Workstream C - Cost-Aware Routing (`B47`)

Depends on: Workstream A and the message/cost normalization (`B62`) already on `master`

Agent focus:

- Add routing policy/config for cheap vs capable model selection
- Keep the routing explainable and overrideable
- Surface route decisions clearly enough for debugging

Likely files:

- `crates/ava-agent/`
- `crates/ava-config/`
- `crates/ava-llm/`

Verification:

```bash
cargo test -p ava-agent
cargo test -p ava-llm router
cargo test -p ava-config routing
```

Exit criteria:

- Routing policy is configurable, explainable, and overrideable.
- At least one non-zero routing-focused test runs in `ava-agent`, `ava-llm`, or `ava-config`.

### Workstream D - Cost Visibility (`B40`)

Depends on: Workstream C for routing data shape and any new budget metadata from Workstream A

Agent focus:

- Add budget alerts and summary cost visibility
- Keep first-pass UX lightweight and useful, not analytics-heavy
- Reuse existing usage accounting where possible

Likely files:

- `crates/ava-tui/`
- `crates/ava-agent/`
- `crates/ava-session/` if summaries are persisted

Verification:

```bash
cargo test -p ava-tui
cargo test -p ava-agent
```

Exit criteria:

- Budget alerts and cost summaries read from shared usage state rather than duplicating accounting logic.
- At least one TUI-facing cost/budget test or integration test runs with non-zero coverage.

## Dependency Order

1. Start Workstream A and Workstream B in parallel.
2. Start Workstream C after A stabilizes enough to expose budget/cost inputs.
3. Start Workstream D after A and C stabilize the metadata that the UI must show.
4. Integrate on `master`, then run the full Sprint 62 verification pass.

## Suggested Agent Roles

- Agent 1: config/runtime specialist for `B64`
- Agent 2: auth/provider specialist for `B63`
- Agent 3: agent/runtime routing specialist for `B47`
- Agent 4: TUI/session UX specialist for `B40`
- Integrator: final merge, conflict resolution, and verification on `master`

## Codex 5.3 Spark Usage Guidance

- Use Codex 5.3 Spark for implementation loops and crate-scoped edits.
- In current AVA/OpenRouter docs, treat this as `openai/gpt-5.3-codex` unless you explicitly configure a different Spark-specific target.
- Keep prompts narrow and file-scoped so each background agent owns one workstream.
- Require each agent to return:
  - files changed
  - tests run
  - open risks
  - follow-up needed before merge

## Sprint 62 Integration Verification

Run after merging the validated workstreams back together:

```bash
cargo test --workspace
cargo clippy --workspace
npm run lint
npm run format:check
npx tsc --noEmit
npm run test:run
```

## CLI/Headless Validation

At least one real provider smoke test should run after integration:

```bash
cargo run --bin ava -- "explain current thinking budget config" --headless --provider openrouter --model openai/gpt-5.3-codex
```

And one credential-refresh or provider-fallback path should be manually exercised with a refreshable provider.

## Risks To Watch

- Provider-specific thinking settings may not map cleanly across Anthropic, Gemini, OpenAI-compatible, and Copilot flows.
- Request-time credential refresh can accidentally create hidden blocking or inconsistent caching.
- Routing logic can become opaque if explanation strings are not first-class output.
- TUI cost visibility can drift from actual accounting if it re-derives numbers instead of reading shared usage state.
- Keep everything Rust-first; do not move core Sprint 62 logic into `packages/`.

## Test Filter Note

Some crate-level commands above use name filters such as `thinking`, `providers`, `credentials`, and `routing`. When using those filters, confirm the command actually ran matching tests rather than reporting `0 tests`.
