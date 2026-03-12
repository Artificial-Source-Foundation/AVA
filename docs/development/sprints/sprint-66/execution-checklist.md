# Sprint 66 Execution Checklist

> Last updated: 2026-03-12
> Preferred implementation model: background agents using Codex 5.3 Spark for development passes (mapped to `openai/gpt-5.3-codex` unless a Spark-specific model ID is configured), with final verification on `master`

## Goal

Implement Sprint 66 as opt-in Extended/backend capability work without changing AVA's default 6-tool surface.

## Scope

- `B44` Web search capability
- `B52` AST-aware operations
- `B53` Full LSP exposure to agent
- `B69` Code search tool

## Recommended Worktree Strategy

- Keep `master` as the integration branch.
- Give each workstream its own short-lived branch/worktree.
- Merge the smallest/additive Extended tools first, then land the higher-risk AST/LSP work.

## Background-Agent Workstreams

### Workstream A - Web Search (`B44`)

Agent focus:

- Add a configurable Extended web-search backend
- Keep the initial provider path low-friction and well-guarded
- Make parse/provider failures explicit rather than silent

Likely files:

- `crates/ava-tools/src/core/web_search.rs`
- `crates/ava-tools/src/core/mod.rs`
- `crates/ava-config/`

Verification:

```bash
cargo test -p ava-tools web_search
cargo test -p ava-tools extended_registration
cargo clippy -p ava-tools
```

Exit criteria:

- Web search is registered as Extended-only and is test-covered.
- At least one non-zero web-search-focused test runs.

### Workstream B - AST-Aware Operations (`B52`)

Agent focus:

- Add structural matching/editing through a narrow, dependable first slice
- Keep external binary/dependency errors explicit and non-crashing
- Preserve write-permission safety when AST edits mutate files

Likely files:

- `crates/ava-tools/src/core/ast_ops.rs`
- `crates/ava-tools/src/core/mod.rs`
- `crates/ava-tools/Cargo.toml`

Verification:

```bash
cargo test -p ava-tools ast_ops
cargo test -p ava-tools extended_registration
cargo clippy -p ava-tools
```

Exit criteria:

- AST operations are Extended-only, documented, and fail safely when required tooling is missing.
- At least one non-zero AST-focused test runs.

### Workstream C - LSP Exposure (`B53`)

Agent focus:

- Add a narrow read-only first slice of LSP-backed intelligence
- Keep server resolution/config explicit
- Treat missing language servers as normal recoverable errors

Likely files:

- `crates/ava-tools/src/core/lsp_ops.rs`
- `crates/ava-tools/src/core/mod.rs`
- `crates/ava-config/`

Verification:

```bash
cargo test -p ava-tools lsp_ops
cargo test -p ava-tools extended_registration
cargo clippy -p ava-tools
```

Exit criteria:

- LSP operations are Extended-only and clearly gated by config/runtime availability.
- At least one non-zero LSP-focused test runs.

### Workstream D - Code Search (`B69`)

Agent focus:

- Expose richer indexed search ergonomics over the existing codebase substrate
- Keep the capability optional and composable with prior indexing work
- Reuse `ava-codebase` rather than re-inventing search internals

Likely files:

- `crates/ava-tools/src/core/code_search.rs`
- `crates/ava-tools/src/core/mod.rs`
- `crates/ava-tools/Cargo.toml`
- `crates/ava-codebase/`

Verification:

```bash
cargo test -p ava-tools code_search
cargo test -p ava-codebase
cargo clippy -p ava-tools -p ava-codebase
```

Exit criteria:

- Code search is Extended-only, useful, and test-covered.
- At least one non-zero code-search-focused test runs.

## Dependency Order

1. Start Workstreams A, B, C, and D in parallel.
2. Merge the smallest/additive work first.
3. Resolve `core/mod.rs` registrations carefully during integration.
4. Update tool-count invariants and docs only after all Extended work lands.

## Suggested Agent Roles

- Agent 1: HTTP/provider/search specialist for `B44`
- Agent 2: structural-editing specialist for `B52`
- Agent 3: LSP/protocol specialist for `B53`
- Agent 4: indexed-search specialist for `B69`
- Integrator: merge, conflict resolution, and final verification on `master`

## Codex 5.3 Spark Usage Guidance

- Use Codex 5.3 Spark for implementation loops and crate-scoped edits.
- In current AVA/OpenRouter docs, treat this as `openai/gpt-5.3-codex` unless you explicitly configure a different Spark-specific target.
- Keep prompts narrow and require each agent to report files changed, tests run, risks, and follow-up needed before merge.

## Sprint 66 Integration Verification

```bash
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
cargo run --bin ava -- "List the tools you have available" --headless --provider openrouter --model openai/gpt-5.3-codex --max-turns 2
```

## Risks To Watch

- `core/mod.rs` and Extended tool count tests are the main merge hotspots.
- AST/LSP work depends on external binaries/servers and must fail gracefully.
- B69 may tempt overreach into MCP/plugin territory; keep the first slice pragmatic.
- Default tool count must remain exactly 6 even after Extended growth.
- Keep everything Rust-first and avoid frontend creep in this sprint.

## Test Filter Note

If you use crate test-name filters such as `web_search`, `ast_ops`, `lsp_ops`, or `code_search`, confirm the command actually ran matching tests rather than reporting `0 tests`.
