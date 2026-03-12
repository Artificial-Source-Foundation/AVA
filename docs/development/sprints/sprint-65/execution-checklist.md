# Sprint 65 Execution Checklist

> Last updated: 2026-03-12
> Preferred implementation model: background agents using Codex 5.3 Spark for development passes (mapped to `openai/gpt-5.3-codex` unless a Spark-specific model ID is configured), with final verification on `master`

## Goal

Implement Sprint 65 as backend coordination infrastructure before returning to heavier Praxis/TUI orchestration work.

## Scope

- `B49` Spec-driven development
- `B59` Agent artifacts system
- `B50` Agent team peer communication
- `B76` Agent Client Protocol (ACP)

## Recommended Worktree Strategy

- Keep `master` as the integration branch.
- Give each workstream its own short-lived branch/worktree.
- Merge the backend primitives first, then land ACP on top of them.

## Background-Agent Workstreams

### Workstream A - Spec Workflow Objects (`B49`)

Agent focus:

- Model specs/tasks as structured backend workflow objects
- Keep first iteration compatible with existing Plan mode
- Emit clear Praxis events around spec lifecycle changes

Likely files:

- `crates/ava-praxis/src/spec.rs`
- `crates/ava-praxis/src/spec_workflow.rs`
- `crates/ava-praxis/src/workflow.rs`
- `crates/ava-praxis/src/events.rs`

Verification:

```bash
cargo test -p ava-praxis spec
cargo test -p ava-praxis workflow
cargo clippy -p ava-praxis -- -D warnings
```

Exit criteria:

- Specs exist as structured backend objects and integrate with workflow execution.
- At least one non-zero spec-focused test runs.

### Workstream B - Agent Artifacts (`B59`)

Agent focus:

- Persist agent outputs as first-class artifacts
- Support in-memory plus durable storage paths
- Make artifact creation a normal workflow outcome rather than an afterthought

Likely files:

- `crates/ava-praxis/src/artifact.rs`
- `crates/ava-praxis/src/artifact_store.rs`
- `crates/ava-praxis/src/workflow.rs`
- optionally session/DB integration helpers

Verification:

```bash
cargo test -p ava-praxis artifact
cargo test -p ava-praxis artifact_store
cargo clippy -p ava-praxis -- -D warnings
```

Exit criteria:

- Artifacts are persisted and queryable in a testable backend path.
- At least one non-zero artifact-focused test runs.

### Workstream C - Peer Communication (`B50`)

Agent focus:

- Add safe mailbox/message primitives between workers
- Detect and surface task/file conflicts explicitly
- Keep the first version auditable and conservative

Likely files:

- `crates/ava-praxis/src/mailbox.rs`
- `crates/ava-praxis/src/conflict.rs`
- `crates/ava-praxis/src/lib.rs`
- `crates/ava-praxis/src/events.rs`

Verification:

```bash
cargo test -p ava-praxis mailbox
cargo test -p ava-praxis conflict
cargo clippy -p ava-praxis -- -D warnings
```

Exit criteria:

- Agents can exchange limited structured data safely.
- Conflict detection/reporting is test-covered.

### Workstream D - ACP First Slice (`B76`)

Depends on: Workstreams A, B, and C

Agent focus:

- Define a small, testable in-process protocol surface
- Keep transport scope intentionally narrow in v1
- Expose protocol methods that map cleanly onto specs, artifacts, and peer messaging

Likely files:

- `crates/ava-praxis/src/acp.rs`
- `crates/ava-praxis/src/acp_handler.rs`
- `crates/ava-praxis/src/acp_transport.rs`
- `docs/architecture/acp-v1.md`

Verification:

```bash
cargo test -p ava-praxis acp
cargo test -p ava-praxis acp_handler
cargo test -p ava-praxis acp_transport
test -f docs/architecture/acp-v1.md
```

Exit criteria:

- ACP has a concrete documented first slice with working in-process handling.
- At least one non-zero ACP-focused test runs.

## Dependency Order

1. Start Workstreams A, B, and C in parallel.
2. Start Workstream D after A/B/C stabilize and are ready to be consumed.
3. Merge A/B/C first, then land ACP on top.

## Suggested Agent Roles

- Agent 1: workflow/spec specialist for `B49`
- Agent 2: storage/artifact specialist for `B59`
- Agent 3: concurrency/mailbox specialist for `B50`
- Agent 4: protocol boundary specialist for `B76`
- Integrator: merge, conflict resolution, and final verification on `master`

## Codex 5.3 Spark Usage Guidance

- Use Codex 5.3 Spark for implementation loops and crate-scoped edits.
- In current AVA/OpenRouter docs, treat this as `openai/gpt-5.3-codex` unless you explicitly configure a different Spark-specific target.
- Keep prompts narrow and require each agent to report files changed, tests run, risks, and follow-up needed before merge.

## Sprint 65 Integration Verification

```bash
cargo check --workspace
cargo test --workspace
cargo clippy --workspace
npm run lint
npm run format:check
npx tsc --noEmit
npm run test:run
```

## CLI/Headless Validation

```bash
cargo run --bin ava -- "list files in current directory" --headless --provider openrouter --model openai/gpt-5.3-codex --max-turns 3
```

## Risks To Watch

- `events.rs`, `workflow.rs`, and `lib.rs` are likely merge hotspots across A/B/C.
- Artifact persistence must fit the existing session/database story cleanly.
- Mailbox/concurrency primitives should stay conservative for v1.
- ACP should avoid transport sprawl and stay in-process first.
- Keep everything Rust-first and avoid new default tools.

## Test Filter Note

If you use crate test-name filters such as `spec`, `artifact`, `mailbox`, `conflict`, or `acp`, confirm the command actually ran matching tests rather than reporting `0 tests`.
