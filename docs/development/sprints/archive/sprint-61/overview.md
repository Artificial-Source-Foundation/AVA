# Sprint 61: Reliable Edit Loop

Status: Implemented on `master` and archived. Backend/command-level verification completed; any remaining UX polish follows normal backlog flow.

## Goal

Make AVA's core edit -> validate -> commit workflow safer, more reliable, and easier to trust without expanding the default tool surface.

## Backlog Items

| ID | Priority | Name | Outcome |
|----|----------|------|---------|
| B67 | P2 | RelativeIndenter for edit matching | Improve edit matching resilience when exact line-based anchors drift |
| B54 | P2 | Auto lint+test after edits | Run post-edit validation automatically using existing Extended tools |
| B37 | P2 | Smart `/commit` | Generate commit-ready summaries/messages from repo state |
| B66 | P2 | Ghost snapshots | Create cheap rollback snapshots before edit-heavy operations |

## Why This Sprint

- Improves the core coding loop every user touches
- Adds confidence before broader feature work such as B26 Praxis-in-composer
- Delivers workflow gains without growing the 6-tool default surface

## Scope

### 1. Edit reliability (`B67`)

- Add RelativeIndenter-based matching as a fallback path for edit application
- Keep existing hashline and exact-match strategies intact
- Prefer minimal, explainable fallback logic over aggressive rewrites

Likely areas:

- `crates/ava-tools/` edit implementation and related tests
- edit strategy selection and matching helpers

### 2. Post-edit validation (`B54`)

- Automatically run configured lint/test steps after successful edits
- Reuse existing Extended-tier `lint` and `test_runner` capabilities rather than adding new tools
- Feed failures back into the conversation in a structured, compact way

Likely areas:

- `crates/ava-agent/` agent loop post-tool handling
- `crates/ava-tools/` lint/test runner integration boundaries
- `crates/ava-config/` for configuration knobs
- TUI messaging and status reporting in `crates/ava-tui/`

### 3. Commit UX (`B37`)

- Upgrade `/commit` from a git-status stub to a commit-prep workflow
- Generate a concise commit message from staged/unstaged repo context
- Keep actual commit execution explicit and safe
- Treat `/commit` as an optional workflow helper, not part of the 6-tool default surface

Likely areas:

- `crates/ava-tui/src/app/commands.rs`
- git/tool integration paths in `crates/ava-tools/`
- optional helper logic in `crates/ava-agent/` or shared TUI command handling

### 4. Safety snapshots (`B66`)

- Create lightweight hidden snapshots before write-heavy operations
- Make rollback discoverable for debugging/recovery without cluttering normal git history
- Keep implementation conservative and local-first

Likely areas:

- `crates/ava-agent/` or execution wrapper layers around edit/write operations
- git integration helpers in `crates/ava-tools/`

## Non-Goals

- No new default tools
- No broad AST/LSP surface expansion
- No B26 interactive Praxis work in this sprint

## Suggested Execution Order

1. `B67` RelativeIndenter
2. `B66` Ghost snapshots
3. `B54` Auto lint+test after edits
4. `B37` Smart `/commit`

## Verification

- Targeted crate tests for edit matching and snapshot behavior
- `cargo test -p ava-tools`, `cargo test -p ava-agent`, and targeted `ava-tui` command tests passed during implementation
- Headless provider-backed smoke test succeeded on `master` in a disposable git repo: file edit completed and a ghost snapshot ref was created
- Original sprint notes flagged `/commit` TUI UX for additional manual review; this now follows normal backlog/UX polish tracking
- Workspace checks for affected crates

## Exit Criteria

- Edit failures from shifted indentation/layout are materially reduced
- Automatic validation can be enabled without default-tool expansion
- `/commit` is meaningfully more useful than raw `git status`
- Snapshot recovery exists and is documented enough for developer use

## Implementation Notes

- `B37` landed as a TUI slash-command workflow helper and does not change the default 6-tool surface
- `B54` landed as opt-in backend plumbing and still needs clearer user-facing config wiring in a future sprint
