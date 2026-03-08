# Sprint 50e: Documentation & Release Polish

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Ensure all documentation is accurate, the README is current, CLAUDE.md reflects the true architecture, and the project is ready for external users.

## Key Files to Read

```
CLAUDE.md                                # Primary reference — MUST match reality
AGENTS.md                               # AI agent instructions
README.md                               # Project README
docs/README.md                          # Docs index
docs/development/roadmap.md             # Sprint roadmap
docs/architecture/components.md         # Architecture docs
docs/architecture/data-flow.md
docs/troubleshooting/                   # Troubleshooting guides

crates/ava-tui/src/config/cli.rs         # CLI args — source of truth for --help
crates/ava-tools/src/core/mod.rs         # Tool registration — source of truth for tool count
crates/ava-tools/src/registry.rs         # Tool trait
```

## Story 1: CLAUDE.md Accuracy

Verify every section of CLAUDE.md matches the actual codebase.

**Checklist:**

| Section | Verify |
|---------|--------|
| Quick Commands | All commands work |
| Architecture | Crate descriptions match reality |
| Project Structure | Directory tree is current |
| Tool Surface | Tool count and groups are accurate |
| Extensions Map | Extension count is correct |
| Rust-First Rule | Still accurate |
| Middleware Priority | Middleware list matches code |
| Code Style | Conventions still apply |
| Common Workflows | Steps are current (e.g., "Add a Tool" workflow) |
| Documentation Priority | Links are valid |

**Implementation:**
- Read each section
- Cross-reference with actual code
- Fix any discrepancies
- Update tool counts, crate counts, file paths

**Acceptance criteria:**
- Every section of CLAUDE.md is verified accurate
- Tool surface count updated (was ~41, now ~53+)
- Crate count updated if changed
- All referenced file paths exist

## Story 2: README Update

Ensure the project README is current and useful for new users.

**Check and update:**
- [ ] Project description matches current state
- [ ] Installation instructions work
- [ ] CLI usage examples are correct (include new flags like `--workflow`, `--voice`, `review`)
- [ ] Feature list matches implemented features
- [ ] Architecture diagram/description is current
- [ ] Links to documentation are valid
- [ ] Configuration example is correct and complete

**New sections to add if missing:**
- Quick start guide (3 steps: install, configure, run)
- Feature highlights (tools, TUI, multi-agent, code review, voice)
- Configuration reference (config.yaml, credentials.json, mcp.json)

**Acceptance criteria:**
- New user can follow README and get AVA running
- All examples work
- No references to deprecated features

## Story 3: docs/ Index Update

Ensure `docs/README.md` index is current and all links work.

**Check:**
- [ ] All sprint docs are in the right place (archives vs active)
- [ ] Architecture docs are current
- [ ] No broken links
- [ ] No stale references to old sprints

**Acceptance criteria:**
- `docs/README.md` is accurate
- No dead links
- Sprint docs properly archived

## Story 4: `--help` Output Review

Verify `ava --help` shows all flags with clear descriptions.

**Check:**
- [ ] All CLI flags present
- [ ] Descriptions are helpful
- [ ] Subcommands listed (`review`)
- [ ] Examples in help text (if any)

**Implementation:**
- Run `cargo run --bin ava -- --help` and `cargo run --bin ava -- review --help`
- Compare against CLAUDE.md and README
- Fix any mismatches in either direction

**Acceptance criteria:**
- `--help` is complete and accurate
- Matches documentation

## Constraints

- **NO new features** — only documentation and accuracy fixes
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Don't delete or restructure code — only update docs and comments
- Keep CLAUDE.md concise (it's already the primary reference)

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
cargo run --bin ava -- --help
cargo run --bin ava -- review --help
```
