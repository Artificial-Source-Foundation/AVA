# Sprint 62: Cost and Runtime Foundations

## Goal

Give AVA stronger cost controls and more reliable provider/runtime behavior so longer sessions are predictable and cheaper to operate.

## Backlog Items

| ID | Priority | Name | Outcome |
|----|----------|------|---------|
| B64 | P2 | Thinking budget configuration | Bound reasoning spend with explicit config and UX |
| B63 | P2 | Dynamic API key resolution | Recover gracefully from expiring OAuth/API credentials |
| B47 | P2 | Cost-aware model routing | Route work to the cheapest capable model/provider |
| B40 | P2 | Budget alerts + cost dashboard | Surface spend clearly in TUI/CLI |

## Why This Sprint

- Builds directly on Sprint 60's provider and message-normalization work
- Improves trust for long-running and multi-model sessions
- Strengthens AVA's differentiation around smart cost management

## Scope

### 1. Thinking budgets (`B64`)

- Add per-provider/per-model reasoning budget configuration
- Expose a clear user-facing control that complements existing thinking modes
- Fail safely when a provider cannot honor the requested level/budget

Likely areas:

- `crates/ava-config/`
- `crates/ava-llm/`
- TUI controls/status in `crates/ava-tui/`

### 2. Dynamic key refresh (`B63`)

- Resolve provider credentials at request time when possible
- Support refresh flows for OAuth-backed providers without requiring app restart
- Keep static API-key paths working unchanged

Likely areas:

- `crates/ava-auth/`
- `crates/ava-config/`
- `crates/ava-llm/`
- provider settings/config plumbing

### 3. Cost-aware routing (`B47`)

- Add a model/provider selection policy that balances cost, capability, and task shape
- Keep routing explainable and overrideable
- Start with conservative heuristics before adding heavier automation

Likely areas:

- `crates/ava-agent/`
- `crates/ava-llm/`
- `crates/ava-config/`

### 4. Cost visibility (`B40`)

- Add budget alerts and a lightweight cost dashboard/summary
- Reuse existing usage/cost tracking infrastructure where available
- Keep the first version focused on practical visibility, not analytics depth

Likely areas:

- `crates/ava-tui/`
- `crates/ava-agent/`
- `crates/ava-session/` if persisted summaries are needed

## Non-Goals

- No new default tools
- No marketplace/plugin distribution work
- No deep semantic indexing work

## Suggested Execution Order

This is a priority order, not a strict serial dependency chain. `B64` and `B63` can run in parallel, then feed `B47`, with `B40` closing the loop in the UI.

1. `B64` Thinking budget configuration
2. `B63` Dynamic API key resolution
3. `B47` Cost-aware model routing
4. `B40` Budget alerts + cost dashboard

## Verification

- Provider-focused tests for budget/key-refresh paths
- Manual validation across at least one OAuth provider and one static-key provider
- Routing tests for explainability and override behavior
- TUI/manual checks for budget alerts and visible spend summaries

## Exit Criteria

- Users can cap reasoning spend intentionally
- Expiring provider credentials do not require app restart to recover
- Model routing can reduce cost while remaining understandable
- Spend is visible enough to support real budget-aware use
