# Sprint 99: Codebase Housekeeping & Documentation Cleanup

> **Low-priority background sprint** — run in parallel with feature sprints. No code changes to Rust crates.

## IMPORTANT: Use Subagents

This sprint covers a LOT of ground. Use subagents to parallelize:
- **Subagent 1**: Research docs cleanup (docs/development/research/)
- **Subagent 2**: Architecture docs cleanup (docs/architecture/)
- **Subagent 3**: Archives audit (docs/archives/)
- **Subagent 4**: Reference code audit (docs/reference-code/)
- **Subagent 5**: Root docs + CLAUDE.md + AGENTS.md accuracy check
- **Subagent 6**: Crate-level README and module docs audit

## Goal

Make the repo clean, navigable, and welcoming for new developers (human or AI). A new contributor should be able to:
1. Read `CLAUDE.md` → understand the project in 5 minutes
2. Read `docs/development/roadmap.md` → know what to work on
3. Find any file in `docs/` without guessing
4. Read any crate's `lib.rs` or `mod.rs` and understand what it does

## Part 1: Research Docs Cleanup

**Location**: `docs/development/research/`

This folder has grown organically and has a lot of redundancy.

### Tasks:
1. **Inventory**: List every file in `research/`, `research/audits/`, `research/backend-analysis/`, `research/cline/`, `research/gemini-cli/`, `research/opencode/`
2. **Identify duplicates**: Many competitors have BOTH a short summary AND a detailed audit AND backend analysis notes — combine into one file per competitor
3. **Consolidate per-competitor**: For each competitor (aider, cline, codex-cli, continue, gemini-cli, goose, opencode, openhands, pi-mono, plandex, swe-agent, zed):
   - If there are multiple files, combine into a single `{competitor}.md` in `research/competitors/`
   - Keep the most useful content, drop redundant sections
   - Each file should have: Architecture summary, key patterns, what AVA can learn
4. **Top-level summaries**: Keep these as-is (they reference competitors):
   - `competitive-analysis-2026-03.md`
   - `rust-competitive-analysis-2026-03.md`
   - `tui-comparison-matrix.md`
   - `tui-implementation-research.md`
   - `SOTA-gap-analysis-2026-03.md`
5. **Delete**: Remove files that are fully superseded by consolidated versions
6. **Index**: Update or create `research/README.md` with a table of contents

### Target structure:
```
docs/development/research/
├── README.md                              # Index
├── competitive-analysis-2026-03.md        # TypeScript-era analysis
├── rust-competitive-analysis-2026-03.md   # Rust-era analysis (Sprint 24)
├── tui-comparison-matrix.md               # TUI feature matrix
├── tui-implementation-research.md         # Sprint 34a research
├── SOTA-gap-analysis-2026-03.md           # State of the art gaps
├── competitors/                           # One file per competitor
│   ├── aider.md
│   ├── cline.md
│   ├── codex-cli.md
│   ├── continue.md
│   ├── gemini-cli.md
│   ├── goose.md
│   ├── opencode.md
│   ├── openhands.md
│   ├── plandex.md
│   ├── swe-agent.md
│   └── zed.md
└── (delete: audits/, backend-analysis/, cline/, gemini-cli/, opencode/, pi-mono/)
```

## Part 2: Architecture Docs Cleanup

**Location**: `docs/architecture/`

### Tasks:
1. **Audit each file** — is it still accurate for the Rust-first architecture?
2. Many files reference the old TypeScript `packages/` architecture. Either:
   - Update to reflect current Rust crates
   - Move to `docs/archives/` if fully outdated
3. **Check for duplicates**: `backlog.md` vs `BACKLOG.md` (case conflict!) — merge or delete
4. **Backlogs**: `backlog.md`, `BACKLOG.md`, `backlog-providers.md`, `backlog-skills-rules.md` — if these are old desktop-era backlogs, archive them
5. **Keep**: `components.md`, `data-flow.md`, `praxis.md`, `database-schema.md` (if accurate)
6. **Update README.md** with current table of contents

## Part 3: Archives Audit

**Location**: `docs/archives/`

### Tasks:
1. Check if any loose files in `archives/` should be in `completed-epics/` or `completed-sprints/`
2. Move stray files into the right subfolder
3. Verify `archives/README.md` explains what this folder is
4. Don't spend time reading/analyzing content — just organize the files

## Part 4: Reference Code Audit

**Location**: `docs/reference-code/`

### Tasks:
1. Check each competitor folder — does it have useful content or just stubs?
2. If `pi-mono/` is no longer relevant (pi-mono seems to be a niche project), consider archiving
3. Update `reference-code/README.md` with a table: competitor name, what's in the folder, relevance level
4. If any competitor folder is empty or has only a README, delete it

## Part 5: Root Docs & CLAUDE.md Accuracy

### Tasks:
1. **CLAUDE.md**: Read the entire file. Check every claim against reality:
   - Are the crate counts accurate? (says ~20, verify)
   - Are the tool counts accurate? (says ~41, verify)
   - Are the extension counts accurate?
   - Are the quick commands still correct?
   - Is the project structure diagram accurate?
   - Does the "Common Workflows" section reflect current code?
   - Update anything outdated
2. **AGENTS.md**: Same accuracy check
   - Are model recommendations current?
   - Are the CLI commands correct?
   - Is the documentation priority list pointing to files that exist?
3. **docs/README.md**: Verify all links work (no broken references)

## Part 6: Crate-Level Documentation

### Tasks:
1. For each crate in `crates/`, check if `src/lib.rs` has a doc comment (`//!`) explaining what the crate does
2. If missing, add a 1-2 line doc comment based on what the crate actually does
3. Check `Cargo.toml` has a `description` field
4. **Do NOT** modify any logic or tests — only add doc comments and descriptions
5. Priority crates (most likely to be read by new devs):
   - `ava-agent` — agent loop
   - `ava-llm` — LLM providers
   - `ava-tools` — tool system
   - `ava-tui` — TUI binary
   - `ava-commander` — multi-agent
   - `ava-types` — shared types

### Example:
```rust
//! AVA Agent — core agent execution loop with tool calling and stuck detection.
//!
//! This crate implements the main agent loop that:
//! - Sends messages to LLM providers via `ava-llm`
//! - Parses and executes tool calls via `ava-tools`
//! - Detects stuck states and terminates gracefully
```

## Part 7: Old Sprint Prompts

### Tasks:
1. Check `docs/development/sprints/` for sprint prompts that are already completed
2. Sprint 32 (integration benchmark) — completed, move to archives
3. Sprint 33 (bugfix) — check if completed, if so move to archives
4. Keep only active/upcoming sprint prompts in `sprints/`
5. Create `docs/archives/completed-sprints/rust-era/` for Rust-era sprint prompts (separate from the old TypeScript-era ones already in archives)

## Constraints

- **Do NOT modify Rust source code** (except adding doc comments in Part 6)
- **Do NOT delete research content** — consolidate/merge instead
- **Do NOT modify tests**
- Prefer merging over deleting — information that took effort to produce should be preserved
- If unsure whether something is outdated, keep it but add a note: `> ⚠️ This may be outdated — last verified [date]`
- Update file counts and references in CLAUDE.md/AGENTS.md to be accurate

## Validation

```bash
# Verify no broken doc links (spot check)
# Verify CLAUDE.md crate count matches reality
ls crates/ | wc -l

# Verify tool count
grep -r "fn name\(&self\)" crates/ava-tools/src/core/ | wc -l

# Verify docs structure is clean
tree docs/ --dirsfirst -L 3

# Verify no code was broken
cargo test --workspace
cargo clippy --workspace
```
