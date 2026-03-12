# Sprint 63 Execution Checklist

> Last updated: 2026-03-12
> Preferred implementation model: background agents using Codex 5.3 Spark for development passes (mapped to `openai/gpt-5.3-codex` unless a Spark-specific model ID is configured), with final verification on `master`

## Goal

Implement Sprint 63 as parallel backend foundation work without expanding the default 6-tool surface.

## Scope

- `B61` Dev tooling setup
- `B65` Pluggable backend operations
- `B39` Background agents on branches
- `B71` Skill discovery
- `B45` File watcher mode

## Recommended Worktree Strategy

- Keep `master` as the integration branch.
- Give each workstream its own short-lived branch/worktree.
- Merge low-risk additive work first, then land the backend abstraction before branch-isolated background agents.

## Background-Agent Workstreams

### Workstream A - Dev Tooling (`B61`)

Agent focus:

- Standardize verification tooling with `nextest`, coverage helpers, hooks, and developer scripts
- Keep everything CI-friendly and non-interactive
- Document the local setup clearly

Likely files:

- workspace config files
- `.github/workflows/`
- `scripts/`
- `docs/development/`

Verification:

```bash
bash scripts/check.sh
cargo nextest run --workspace
cargo clippy --workspace
```

Exit criteria:

- Local verification is easier to run and matches CI expectations.
- New tooling/scripts are documented and executable.

### Workstream B - Backend Abstraction (`B65`)

Agent focus:

- Introduce a clear backend boundary for file and command execution
- Keep the first backend local-first and compatible with current behavior
- Make tool execution work against an injected backend rather than one implicit local path
- Extend the existing `ava-platform` crate rather than creating a new platform crate from scratch

Likely files:

- `crates/ava-platform/`
- `crates/ava-tools/`
- `crates/ava-agent/`
- `crates/ava-praxis/`
- `crates/ava-tui/`

Verification:

```bash
cargo check --workspace
cargo test -p ava-platform backend
cargo test -p ava-tools
cargo clippy --workspace -- -D warnings
```

Exit criteria:

- Execution backends have a documented, testable boundary.
- Existing tools still behave correctly through the new abstraction.

### Workstream C - Branch-Isolated Background Agents (`B39`)

Depends on: Workstream B

Agent focus:

- Run background agents on isolated worktrees/branches
- Keep task visibility and status reporting intact
- Prevent background work from mutating the active worktree

Likely files:

- `crates/ava-praxis/`
- `crates/ava-tui/`
- git/worktree helper modules

Verification:

```bash
cargo test -p ava-tui worktree
cargo test -p ava-tui background_worktree
git worktree list
```

Exit criteria:

- Background work is isolated from the active branch/worktree.
- Merge-back or discard flows are explicit and test-covered.

### Workstream D - Skill Discovery (`B71`)

Agent focus:

- Extend instruction discovery to skill directories with clear precedence
- Support project and global skill folders conservatively
- Make the result reusable for a future `/skills` command

Likely files:

- `crates/ava-agent/src/instructions.rs`
- `crates/ava-config/`

Verification:

```bash
cargo test -p ava-agent skill
cargo test -p ava-agent instructions
cargo test -p ava-config
```

Exit criteria:

- Skill discovery works across supported directories without regressing existing instruction loading.
- At least one non-zero skill-discovery-focused test runs.

### Workstream E - File Watcher Mode (`B45`)

Agent focus:

- Add opt-in watcher support for headless and TUI usage
- Support comment/directive-driven triggers first
- Avoid runaway self-trigger loops

Likely files:

- `crates/ava-tui/`
- `crates/ava-agent/`
- config/docs

Verification:

```bash
cargo test -p ava-tui watcher
cargo test -p ava-tui comment_directive
cargo run --bin ava -- --help
```

Exit criteria:

- Watcher mode is opt-in, documented, and avoids noisy recursive triggering.
- At least one watcher-focused test runs with non-zero coverage.

## Dependency Order

1. Start Workstreams A, B, D, and E in parallel.
2. Start Workstream C after Workstream B stabilizes.
3. Merge additive/smaller work first, then the backend abstraction, then branch-isolated background execution.

## Suggested Agent Roles

- Agent 1: dev tooling / CI helper specialist for `B61`
- Agent 2: backend/runtime architect for `B65`
- Agent 3: background execution/worktree specialist for `B39`
- Agent 4: instruction loading / config specialist for `B71`
- Agent 5: watcher/event loop specialist for `B45`
- Integrator: merge, conflict resolution, and final verification on `master`

## Codex 5.3 Spark Usage Guidance

- Use Codex 5.3 Spark for implementation loops and crate-scoped edits.
- In current AVA/OpenRouter docs, treat this as `openai/gpt-5.3-codex` unless you explicitly configure a different Spark-specific target.
- Keep prompts narrow and require each agent to report files changed, tests run, risks, and follow-up needed before merge.

## Sprint 63 Integration Verification

```bash
cargo check --workspace
cargo test --workspace
cargo clippy --workspace
cargo test -p ava-tools -- default_tools_gives_6_tools --exact
npm run lint
npm run format:check
npx tsc --noEmit
npm run test:run
```

## CLI/Headless Validation

```bash
cargo run --bin ava -- "List the files in this directory" --headless --provider openrouter --model openai/gpt-5.3-codex --max-turns 3
cargo run --bin ava -- "Read the README and summarize it" --headless --multi-agent --provider openrouter --model openai/gpt-5.3-codex --max-turns 5
```

## Risks To Watch

- The backend abstraction is the critical path and touches many tool call sites.
- Branch-isolated background work depends on all tools respecting the backend working directory.
- Skill discovery can bloat prompt context if too many large skills are injected.
- File watcher mode must avoid re-trigger loops from AVA-authored file writes.
- Keep everything Rust-first and avoid inventing new default tools.

## Test Filter Note

If you use crate test-name filters such as `backend`, `skill`, `watcher`, or `worktree`, confirm the command actually ran matching tests rather than reporting `0 tests`.
