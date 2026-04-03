# Contributing To AVA

This file is the human-friendly contributor guide for working on AVA.

Read these first if you are changing code in this repo:

1. `README.md` — product overview and install paths
2. `CLAUDE.md` — architecture and contributor conventions
3. `AGENTS.md` — repo rules used by AI coding agents; still useful for humans because it contains the current required checks and doc-update rules

## What Are You Trying To Do?

- Fix a typo or small docs issue: edit the relevant Markdown file directly and open a small PR
- Add or change backend behavior: implement it in Rust, not TypeScript
- Add or change a tool: start in `crates/ava-tools/src/core/`
- Add or change a desktop feature: expect changes in both `src/` and `src-tauri/`
- Add a provider or model integration: start in `crates/ava-llm/`
- Not sure where something belongs: check `CODEBASE_STRUCTURE.md`, then `CLAUDE.md`

## Quick Start

AVA is a Rust-first project with a Rust workspace plus a Tauri desktop frontend.

- Backend logic belongs in Rust
- Desktop UI lives in `src/` and talks to Rust through Tauri IPC in `src-tauri/`
- The main CLI/TUI app lives in `crates/ava-tui/`

Clone and install dependencies:

```bash
git clone https://github.com/ASF-GROUP/AVA.git
cd AVA
pnpm install
```

## Common Dev Flows

CLI/TUI:

```bash
cargo run --bin ava
```

Headless CLI:

```bash
cargo run --bin ava -- "fix the login bug"
```

Desktop app:

```bash
pnpm tauri dev
```

Build the CLI without installing globally:

```bash
cargo build --bin ava
./target/debug/ava --version
```

## Development Prerequisites

You will usually want these installed locally:

- Rust toolchain via `rustup`
- `pnpm`
- Node.js
- Tauri desktop prerequisites for your platform if you are touching the desktop app

For desktop setup details and platform-specific install notes, see:

- `docs/install.md`

## Where To Put Code

- New tools: `crates/ava-tools/src/core/`
- Agent runtime work: `crates/ava-agent/`
- LLM providers: `crates/ava-llm/src/providers/`
- TUI features: `crates/ava-tui/`
- Desktop Rust commands: `src-tauri/src/commands/`
- Desktop SolidJS UI: `src/`
- Config schema and loading: `crates/ava-config/`

Useful repo map:

- `CODEBASE_STRUCTURE.md`

## Issues Vs Discussions

Use the right channel so maintainers can triage efficiently.

- Open an issue for: reproducible bugs, specific feature requests, regressions, broken docs, or concrete follow-up work
- Use discussions or other public project conversation channels for: open-ended questions, architecture brainstorming, usage help, or early idea validation

If you are proposing a non-trivial feature, it is better to discuss the shape first than to implement a large surprise PR.

## Rules That Matter

These are the contributor rules that most often affect reviews:

1. All new backend features must be Rust.
2. Do not add TypeScript backend logic.
3. Keep the default tool surface capped at 9.
4. Wire features in fully; do not leave dead modules behind.
5. Keep docs in sync when the code changes.

## Good First Contributions

Good low-risk starting points:

1. documentation fixes or small README/docs clarifications
2. test improvements for an existing crate
3. small TUI polish changes
4. narrow bug fixes that stay within one crate or one desktop screen
5. small provider/model catalog updates that follow existing patterns

Higher-risk changes include:

1. permission system changes
2. sandbox or command-classifier changes
3. core agent loop behavior
4. broad tool-surface changes
5. cross-cutting Tauri + frontend + agent runtime refactors

## Checks Before Opening A PR

Preferred all-in-one check:

```bash
just check
```

If `just` is not installed, run the main checks directly:

```bash
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
pnpm lint
pnpm format:check
pnpm typecheck
```

If you touched desktop Rust code, also verify:

```bash
cd src-tauri
cargo check
```

If you changed user-facing frontend code, also run the relevant UI flow manually.

## Required Docs Updates

After every significant feature, fix, or refactor, update the relevant docs:

1. `CHANGELOG.md` — add an entry under the current version section
2. `docs/backlog.md` — note the completed work
3. `CLAUDE.md` — update architecture/tooling counts if they changed
4. `CODEBASE_STRUCTURE.md` — update the repo map if structure changed materially

Do not let docs drift from the code.

## Pull Requests

Good PRs for AVA usually:

1. stay focused on one feature/fix
2. include tests or a concrete verification note
3. explain user-visible behavior changes
4. mention any follow-up work or limitations clearly

### PR Checklist

Before you open or mark a PR ready for review, make sure you have:

- run the required checks locally
- updated docs that changed with the code
- added tests or included a clear verification note
- explained user-visible behavior changes in the PR description
- avoided unrelated drive-by changes unless they were required to pass checks
- kept backend logic in Rust

### Commit And Branch Guidance

Prefer focused branches and concise commit messages that match the existing style, for example:

- `feat: add adaptive LSP runtime`
- `fix: restore workspace verification after LSP runtime`

Keep commits small enough that a reviewer can understand the intent from the diff.

### Review Expectations

AVA maintainer response time is not guaranteed, but good contributors help the process by:

1. keeping PRs focused
2. responding to review comments directly in the thread
3. explaining why a change was made, not just what changed
4. avoiding private side-channel review requests unless sensitive information is involved

If a PR sits for a while, a polite public follow-up on the PR is appropriate.

## Notes For Desktop/LSP Work

- Tauri IPC types should use `serde(rename_all = "camelCase")`
- Register new desktop commands in both `src-tauri/src/commands/mod.rs` and `src-tauri/src/lib.rs`
- For frontend code in `src/`, keep strict typing and avoid `any`
- Preserve the tool-tier policy: LSP-related helpers can be available to agents without expanding the official default 9-tool tier

## AI Tool Use

Using AI tools to help with contributions is acceptable, but you are still responsible for the result.

1. review all generated code before submitting it
2. run the required checks yourself
3. verify security-sensitive changes carefully
4. do not treat AI output as proof that the code is correct

For this repo, pay extra attention to:

- sandboxing
- permissions
- command execution
- credential handling
- Tauri IPC types and frontend/backend boundaries

## Common Contributor Pitfalls

Things that commonly go wrong in this repo:

1. adding backend behavior in TypeScript instead of Rust
2. forgetting to register a new Tauri command in both `mod.rs` and `lib.rs`
3. passing local checks in one layer but not the full Rust + frontend set
4. changing code without updating `CHANGELOG.md` and `docs/backlog.md`
5. expanding the default tool surface when the repo policy says to keep it capped at 9

## Troubleshooting

If setup or checks fail, start here:

1. `just check` missing: run the equivalent raw commands from this file instead
2. `pnpm typecheck` or `pnpm tauri dev` fails: confirm `pnpm install` completed and desktop prerequisites are installed
3. `cargo check` passes but the desktop app fails: verify the Tauri command is registered and the frontend invoke types match
4. LSP-related work behaves differently between terminal and desktop: check local binary discovery and user-local install paths such as `~/.local/bin`, `~/.cargo/bin`, and `~/.local/node_modules/.bin`
5. provider or auth issues: use `ava auth list` and `ava auth test <provider>`

## If You Are Unsure

Use these as the source of truth:

1. `CLAUDE.md` for architecture and conventions
2. `AGENTS.md` for required checks and repo rules
3. `CODEBASE_STRUCTURE.md` for where things live
