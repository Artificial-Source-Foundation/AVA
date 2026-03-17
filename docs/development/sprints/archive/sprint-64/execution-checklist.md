# Sprint 64 Execution Checklist

> Last updated: 2026-03-12
> Preferred implementation model: background agents using Codex 5.3 Spark for development passes (mapped to `openai/gpt-5.3-codex` unless a Spark-specific model ID is configured), with final verification on `master`

## Goal

Implement Sprint 64 as a Rust-first knowledge/context sprint without changing the default tool surface.

## Scope

- `B38` Auto-learned project memories
- `B57` Multi-repo context
- `B58` Semantic codebase indexing
- `B48` Change impact analysis

## Recommended Worktree Strategy

- Keep `master` as the integration branch.
- Give each workstream its own short-lived branch/worktree.
- Merge the multi-repo substrate before semantic indexing and impact work if interface drift appears.

## Background-Agent Workstreams

### Workstream A - Auto-Learned Project Memories (`B38`)

Agent focus:

- Add a conservative learned-memory pipeline with confidence/review controls
- Keep learned state local, inspectable, and low-noise
- Inject only confirmed memories into prompts

Likely files:

- `crates/ava-memory/`
- `crates/ava-agent/`
- `crates/ava-config/`

Verification:

```bash
cargo test -p ava-memory learned
cargo test -p ava-memory pattern_detector
cargo test -p ava-agent memory
```

Exit criteria:

- Learned memories are conservative, reviewable, and do not spam prompt context.
- At least one non-zero learned-memory-focused test runs.

### Workstream B - Multi-Repo Context (`B57`)

Agent focus:

- Support explicit multi-repo workspaces without breaking single-repo defaults
- Qualify retrieval results by repo
- Keep path handling and permissions explicit

Likely files:

- `crates/ava-codebase/`
- `crates/ava-agent/`
- `crates/ava-config/`

Verification:

```bash
cargo test -p ava-codebase workspace
cargo test -p ava-codebase search
cargo test -p ava-agent stack
```

Exit criteria:

- Multi-repo retrieval works while preserving single-repo behavior as the default.
- Results clearly identify the repo they came from.

### Workstream C - Semantic Codebase Indexing (`B58`)

Depends on: Workstream B stabilizing shared codebase/index interfaces

Agent focus:

- Add semantic retrieval as an opt-in capability on top of lexical indexing
- Keep the feature flag disciplined and non-default
- Make hybrid retrieval degrade cleanly when semantic indexing is unavailable

Likely files:

- `crates/ava-codebase/`

Verification:

```bash
cargo test -p ava-codebase
cargo test -p ava-codebase --features semantic semantic
cargo clippy -p ava-codebase --features semantic
```

Exit criteria:

- Semantic indexing is opt-in and does not break default builds.
- At least one non-zero semantic-search-focused test runs.

### Workstream D - Change Impact Analysis (`B48`)

Agent focus:

- Produce conservative, explainable impact summaries for changed files/tests/dependencies
- Reuse existing graph/index intelligence
- Prefer summaries that help the agent reason without overclaiming precision

Likely files:

- `crates/ava-codebase/`
- `crates/ava-agent/`
- optionally `crates/ava-tools/` if an Extended impact tool is added

Verification:

```bash
cargo test -p ava-codebase impact
cargo test -p ava-codebase graph
cargo test -p ava-agent tool_execution
```

Exit criteria:

- Impact reports are explainable and useful on representative repos.
- At least one non-zero impact-analysis-focused test runs.

## Dependency Order

1. Start Workstreams A and B in parallel.
2. Start Workstream C after Workstream B stabilizes enough to expose shared indexing interfaces.
3. Start Workstream D in parallel with C, or immediately if it can target the existing graph/index layer cleanly.
4. Integrate all branches and resolve `stack.rs` carefully.

## Suggested Agent Roles

- Agent 1: memory/pattern-learning specialist for `B38`
- Agent 2: codebase/workspace indexing specialist for `B57`
- Agent 3: retrieval/feature-flag specialist for `B58`
- Agent 4: graph-analysis specialist for `B48`
- Integrator: merge, conflict resolution, and final verification on `master`

## Codex 5.3 Spark Usage Guidance

- Use Codex 5.3 Spark for implementation loops and crate-scoped edits.
- In current AVA/OpenRouter docs, treat this as `openai/gpt-5.3-codex` unless you explicitly configure a different Spark-specific target.
- Keep prompts narrow and require each agent to report files changed, tests run, risks, and follow-up needed before merge.

## Sprint 64 Integration Verification

```bash
cargo test --workspace
cargo clippy --workspace
cargo test --workspace --features ava-codebase/semantic
npm run lint
npm run format:check
npx tsc --noEmit
npm run test:run
```

## CLI/Headless Validation

```bash
cargo run --bin ava -- "List all Rust files and summarize the project structure" --headless --provider openrouter --model openai/gpt-5.3-codex --max-turns 5
```

## Risks To Watch

- `stack.rs` is a likely merge hotspot across memory, workspace, and impact work.
- Semantic indexing may introduce native/build complexity and must stay feature-gated.
- Learned memories can degrade quality if the detector is too aggressive.
- Multi-repo path display must stay unambiguous while tool execution remains absolute-path safe.
- Keep everything Rust-first and avoid default-tool expansion.

## Test Filter Note

If you use crate test-name filters such as `learned`, `memory`, `workspace`, `stack`, `semantic`, or `impact`, confirm the command actually ran matching tests rather than reporting `0 tests`.
