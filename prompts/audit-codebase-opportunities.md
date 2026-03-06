# Codebase Audit: Opportunities & Technical Debt

> For AI coding agent. This is a READ-ONLY audit — do NOT modify any files.
> Output: A structured report at `docs/audits/codebase-audit-YYYY-MM-DD.md`

---

## Role

You are a senior Rust/TypeScript systems architect performing a deep audit of AVA, a multi-agent AI coding assistant. Your job is to find issues, opportunities, and risks the team may have missed.

Read these files first:
- `CLAUDE.md` (architecture, conventions)
- `AGENTS.md` (project overview)

**CRITICAL: Do NOT modify any source files. This is analysis only. Write your findings to a single report file.**

---

## Audit Scope

### 1. Rust Crate Health (Priority: HIGH)

For each crate in `crates/`:

- **Read** `Cargo.toml` and `src/lib.rs` (or `src/main.rs`)
- **Check**: Are dependencies up to date? Any deprecated crates?
- **Check**: Is `unsafe` used anywhere? Is it justified?
- **Check**: Error handling — are errors propagated properly or swallowed?
- **Check**: Are there `unwrap()` / `expect()` calls in non-test code? These are crash risks.
- **Check**: Are there `todo!()` / `unimplemented!()` markers left behind?
- **Check**: Dead code — are there `#[allow(dead_code)]` annotations hiding unused code?
- **Check**: Test coverage — does each crate have tests? Are they meaningful or just stubs?
- **Check**: Public API surface — are internal types accidentally exposed?

Report format per crate:
```
### crate-name
- LOC: X
- Dependencies: [list any concerning ones]
- Unwrap count: N (list locations)
- Todo/unimplemented count: N
- Dead code: [any #[allow(dead_code)]]
- Test quality: [none / stubs / basic / solid]
- Issues: [list]
- Opportunities: [list]
```

### 2. Cross-Crate Architecture (Priority: HIGH)

- **Dependency graph**: Map which crates depend on which. Are there circular dependencies?
- **Type duplication**: Are the same types defined in multiple crates? (e.g., Message, Session, ToolResult)
- **Trait coherence**: Are traits like `LLMProvider`, `Tool`, `Middleware` consistent in their error handling?
- **Feature flags**: Are there any Cargo features that should exist but don't? (e.g., optional TUI deps)
- **Compilation time**: Which crates are heaviest? Could any be split?

### 3. TypeScript Layer Health (Priority: MEDIUM)

For `packages/core-v2/` and `packages/extensions/`:

- **Check**: Any `any` types that shouldn't be there?
- **Check**: Circular imports?
- **Check**: Files over 300 lines?
- **Check**: Dead code or unused exports?
- **Check**: Extension modules that have no tests?
- **Check**: Are there TypeScript features that should have been migrated to Rust but weren't?

### 4. Security Audit (Priority: HIGH)

- **Command injection**: Does `BashTool` properly sanitize inputs? Check shell quoting.
- **Path traversal**: Can `ReadTool` / `WriteTool` / `EditTool` escape the working directory?
- **Credential exposure**: Could API keys leak into logs, session storage, or error messages?
- **Sandbox escapes**: Review `ava-sandbox` — are there gaps in the bwrap/sandbox-exec policies?
- **Permission bypasses**: Can the permission system be circumvented?
- **Supply chain**: Any dependencies with known vulnerabilities? Run `cargo audit` mentally.

### 5. Performance Opportunities (Priority: MEDIUM)

- **Allocations**: Are there hot paths doing unnecessary String allocations or clones?
- **Async overhead**: Are there blocking calls inside async functions?
- **Serialization**: Is there unnecessary JSON serialization between Rust crates?
- **Database**: Is SQLite being used efficiently? Are there missing indexes in ava-session/ava-db?
- **Caching**: Are there opportunities for caching (e.g., syntax highlighting themes, compiled regexes)?

### 6. Missing Functionality (Priority: MEDIUM)

Compare AVA's tool surface against competitors. What's missing?

Reference: `docs/research/tui-comparison-matrix.md`

- **Tools**: What tools do Claude Code / Codex CLI / Gemini CLI have that AVA doesn't?
- **Features**: What agent features are missing? (e.g., context window management, auto-compact, web browsing)
- **Developer experience**: What would make AVA nicer to use? (e.g., better error messages, progress indicators)

### 7. Documentation Gaps (Priority: LOW)

- Are there crates with no doc comments on public items?
- Is the README accurate?
- Are there stale docs referencing old architecture?

---

## Output Format

Create: `docs/audits/codebase-audit-2026-03-06.md`

Structure:
```markdown
# AVA Codebase Audit — 2026-03-06

## Executive Summary
[3-5 bullet points of the most critical findings]

## Critical Issues (fix immediately)
[Security vulnerabilities, crash risks, data loss risks]

## High Priority (fix soon)
[Architecture issues, significant tech debt, missing error handling]

## Opportunities (nice to have)
[Performance wins, missing features, DX improvements]

## Rust Crate Report
[Per-crate breakdown]

## TypeScript Layer Report
[Per-package breakdown]

## Security Findings
[Detailed security analysis]

## Dependency Health
[Outdated deps, vulnerability risks]

## Recommended Next Sprints
[Based on findings, suggest 3-5 sprint themes]
```

---

## Process

1. Read `CLAUDE.md` and `AGENTS.md`
2. `ls crates/` to get full crate list
3. For each crate: read Cargo.toml + lib.rs + key source files
4. Grep for `unwrap()`, `todo!()`, `unsafe`, `#[allow(dead_code)]`, `any` (in TS)
5. Read `packages/core-v2/` key files
6. Read `packages/extensions/` — check each extension has tests
7. Review security-sensitive code (bash, sandbox, permissions, credentials)
8. Check `docs/research/tui-comparison-matrix.md` for feature gaps
9. Write the report
10. Commit: `git commit -m "docs(audit): codebase opportunity analysis 2026-03-06"`
