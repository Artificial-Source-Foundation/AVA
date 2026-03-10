# Sprint 56: Codebase Quality Audit

## Context

AVA is a Rust-first AI coding agent (~21 crates, Ratatui TUI, Tokio async). See `CLAUDE.md` for conventions. The Rust codebase lives in `crates/` with ~47,000 lines across ~291 source files.

**This is a READ-ONLY audit.** Do NOT modify any source files. All output goes to `docs/development/sprints/sprint-56/results/`.

## Execution Strategy

You MUST use the Agent tool to spawn **6 parallel sub-agents** for the audit dimensions below. Launch all 6 simultaneously. Each sub-agent writes its findings to a results file. After all complete, do a final synthesis pass.

**IMPORTANT**: Each sub-agent must be thorough. Use Grep, Glob, and Read tools extensively. Do not sample — scan ALL files in ALL crates. The point is comprehensive coverage so nothing is missed.

---

## Sub-Agent 1: Unwrap/Expect Audit

**Output**: Write results to `docs/development/sprints/sprint-56/results/01-unwrap-audit.md`

**Instructions for the sub-agent prompt**:

```
You are auditing the AVA Rust codebase for panic-risk code. Scan ALL .rs files in crates/ (excluding test code).

1. Use Grep to find ALL `.unwrap()` and `.expect(` calls in `crates/` with glob `*.rs`
2. For each match, determine if it's in test code (`#[cfg(test)]` module or `tests/` directory) — SKIP test code
3. For each production unwrap/expect, read surrounding context (5 lines) to assess:
   - Is it justified? (e.g., compile-time guarantee, static data)
   - What's the panic risk? (CRITICAL: user-facing path, HIGH: agent loop, MEDIUM: init code, LOW: static data)
   - Suggested fix (use `?`, `.ok_or()`, `.unwrap_or_default()`, match, etc.)

Write the results file with this format:

# Unwrap/Expect Audit

## Summary
- Total production unwraps: N
- Critical: N, High: N, Medium: N, Low: N

## Critical Findings
### [file:line] — description
- **Code**: `the_line`
- **Context**: what this code does
- **Risk**: why it could panic
- **Fix**: suggested alternative

## High Findings
(same format)

## Medium Findings
(same format)

## Low Findings (Justified)
(same format, brief)
```

---

## Sub-Agent 2: Test Coverage Audit

**Output**: Write results to `docs/development/sprints/sprint-56/results/02-test-coverage.md`

**Instructions for the sub-agent prompt**:

```
You are auditing test coverage for the AVA Rust codebase. Scan ALL crates in crates/.

For each crate:
1. Use Grep to count `#[test]` and `#[tokio::test]` functions in src/ (inline tests)
2. Use Glob to find files in tests/ directory (integration tests)
3. Use Grep to find `#[ignore]` tests
4. Identify src/ modules that have ZERO test coverage — no `#[cfg(test)]` block AND not covered by integration tests
5. Check if key public functions/methods have corresponding tests

For each crate, calculate:
- Number of .rs source files
- Number of test functions (inline + integration)
- Test-to-source ratio
- List of UNTESTED modules (modules with no tests at all)

Write the results file with this format:

# Test Coverage Audit

## Summary
- Total crates: N
- Total test functions: N
- Crates with zero integration tests: [list]
- Modules with zero test coverage: [list with file paths]

## Per-Crate Breakdown

### crate-name
- Source files: N
- Inline tests: N
- Integration tests: N
- Coverage ratio: N tests per source file
- **Untested modules**: [list]
- **Missing test scenarios**: [list of what should be tested]

## Priority Recommendations
1. [Most critical coverage gap]
2. ...
```

---

## Sub-Agent 3: Documentation Coverage Audit

**Output**: Write results to `docs/development/sprints/sprint-56/results/03-doc-coverage.md`

**Instructions for the sub-agent prompt**:

```
You are auditing documentation coverage for the AVA Rust codebase. Scan ALL crates in crates/.

For each crate:
1. Use Grep to find all `pub struct`, `pub enum`, `pub trait`, `pub fn`, `pub async fn`, `pub type` declarations
2. For each public item, check if the PRECEDING line(s) contain `///` doc comments
3. Track: documented vs undocumented public items
4. Check for crate-level `//!` doc comments in lib.rs

Focus on:
- Public structs/enums/traits (MUST have docs)
- Public functions with more than 3 parameters (SHOULD have docs)
- Public functions that return Result (SHOULD document error conditions)

Write the results file with this format:

# Documentation Coverage Audit

## Summary
- Total public items: N
- Documented: N (X%)
- Undocumented: N (X%)

## Per-Crate Breakdown

### crate-name
- Public items: N
- Documented: N / N (X%)
- **Undocumented items**:
  - `pub struct Foo` at file:line
  - `pub fn bar()` at file:line

## Priority: Items That MUST Be Documented
(public traits, error types, key structs used across crate boundaries)
```

---

## Sub-Agent 4: Modularity Audit

**Output**: Write results to `docs/development/sprints/sprint-56/results/04-modularity.md`

**Instructions for the sub-agent prompt**:

```
You are auditing code modularity for the AVA Rust codebase. Scan ALL crates in crates/.

1. **Large files**: Find ALL .rs files. For each file, count lines. Flag anything over 300 lines.
   - For files over 300 lines, READ them and suggest specific split points (e.g., "extract FooBuilder into foo_builder.rs")

2. **Module organization**: For each crate, check if the module tree makes sense:
   - Are there flat `src/` directories with 10+ files? (should use subdirectories)
   - Are there modules doing multiple unrelated things?
   - Are there circular or unclear dependency patterns?

3. **Cross-crate coupling**: Use Grep to find `use ava_` imports in each crate. Map which crates depend on which. Flag any surprising or circular dependencies.

4. **God structs**: Find structs with more than 10 fields. These may need decomposition.

Write the results file with this format:

# Modularity Audit

## Summary
- Files over 300 lines: N
- Files over 500 lines: N
- Crates with flat module structure: [list]

## Large Files (Split Candidates)

### file_path (N lines)
- **Current responsibility**: what it does
- **Split suggestion**: specific modules to extract
- **Effort**: estimated complexity (trivial/moderate/significant)

## God Structs (>10 fields)

### StructName at file:line (N fields)
- **Fields**: [list]
- **Suggestion**: decompose into X + Y

## Dependency Map
(crate → [dependencies] for each crate)

## Circular/Suspicious Dependencies
(if any)
```

---

## Sub-Agent 5: Performance Audit

**Output**: Write results to `docs/development/sprints/sprint-56/results/05-performance.md`

**Instructions for the sub-agent prompt**:

```
You are auditing performance patterns in the AVA Rust codebase. Focus on hot paths: agent loop, LLM calls, tool execution, TUI rendering.

1. **Clone abuse**: Use Grep to find `.clone()` calls. For each, determine if it's in a hot path:
   - Agent loop (`crates/ava-agent/src/agent_loop/`)
   - LLM request building (`crates/ava-llm/src/providers/`)
   - Tool execution (`crates/ava-tools/src/`)
   - TUI render loop (`crates/ava-tui/src/ui/`, `src/widgets/`)
   Read context to determine if the clone is necessary or could use `&`, `Arc`, `Cow`, or be eliminated.

2. **Allocation patterns**: Search for:
   - `String::new()` + `push_str` (should use `format!` or capacity hints)
   - `Vec::new()` + `push` in loops (should use `with_capacity` or `collect`)
   - `to_string()` / `to_owned()` where `&str` would suffice

3. **Async overhead**: Search for `block_in_place` and `block_on` calls — these block the async runtime and should be minimized. Note each occurrence with context.

4. **Lock contention**: Search for `RwLock` and `Mutex` usage. Flag any locks held across `.await` points (potential deadlocks).

Write the results file with this format:

# Performance Audit

## Summary
- Total `.clone()` in hot paths: N
- Unnecessary clones identified: N
- `block_in_place` / `block_on` calls: N
- Lock contention risks: N

## Hot Path Clones

### file:line — description
- **Clone of**: what type
- **Hot path**: which loop/function
- **Suggestion**: alternative approach
- **Priority**: HIGH/MEDIUM/LOW

## Blocking Async Calls
(list of block_in_place/block_on with context)

## Lock Patterns
(RwLock/Mutex usage with potential contention analysis)

## Allocation Improvements
(Vec/String capacity hints, unnecessary to_string, etc.)
```

---

## Sub-Agent 6: Code Hygiene Audit

**Output**: Write results to `docs/development/sprints/sprint-56/results/06-hygiene.md`

**Instructions for the sub-agent prompt**:

```
You are auditing code hygiene for the AVA Rust codebase. Scan ALL crates in crates/.

1. **TODO/FIXME/HACK markers**: Use Grep to find `TODO`, `FIXME`, `HACK`, `XXX`, `TEMP`, `TEMPORARY`, `WORKAROUND` in all .rs files. For each, note the context and whether it's still relevant.

2. **Dead code**: Use Grep to find `#[allow(dead_code)]` and `#[allow(unused` attributes. Read context to determine if the code should be removed or the allow is justified.

3. **Unsafe code**: Use Grep to find `unsafe` blocks. For each, verify:
   - Is the unsafe justified? (FFI, performance-critical, etc.)
   - Is there a safe alternative?
   - Are safety invariants documented?

4. **Stale imports**: Use Grep to find `#[allow(unused_imports)]`. These may indicate incomplete refactoring.

5. **Deprecated patterns**: Search for:
   - `extern crate` (outdated Rust 2015 pattern)
   - `#[macro_use]` (should use explicit imports)
   - `try!()` macro (replaced by `?`)
   - `Box<dyn Error>` where `AvaError` should be used

6. **Consistency**: Check for:
   - Mixed error handling styles within the same crate
   - Inconsistent naming (snake_case for functions, CamelCase for types)
   - Inconsistent visibility (pub where pub(crate) would suffice)

Write the results file with this format:

# Code Hygiene Audit

## Summary
- TODO/FIXME markers: N
- Dead code allows: N
- Unsafe blocks: N (N justified, N questionable)
- Deprecated patterns: N
- Consistency issues: N

## TODO/FIXME Markers

### file:line — marker text
- **Status**: still relevant / stale / done
- **Action**: fix / remove / keep

## Dead Code

### file:line — `#[allow(dead_code)]` on what
- **Assessment**: remove code / remove allow / keep

## Unsafe Code

### file:line — unsafe block
- **Justification**: why
- **Safe alternative**: yes/no
- **Invariants documented**: yes/no

## Deprecated Patterns
(list with file:line)

## Consistency Issues
(list with examples)
```

---

## Final Synthesis

After ALL 6 sub-agents complete, do the following:

### Step 1: Read all 6 result files
Read each file in `docs/development/sprints/sprint-56/results/` (01 through 06).

### Step 2: Invoke Code Reviewer
Invoke the Code Reviewer sub-agent with this prompt:
```
Review all 6 audit reports in docs/development/sprints/sprint-56/results/. Cross-reference findings across reports. Check for:
1. Are findings consistent? (e.g., does the unwrap audit match the test coverage gaps?)
2. Are severity ratings appropriate?
3. Are any findings duplicated across reports?
4. Are there any gaps — things none of the 6 audits caught?
5. Are the fix suggestions practical and correct?

Return a list of corrections or additional findings.
```

### Step 3: Write the action plan

Write `docs/development/sprints/sprint-56/results/00-action-plan.md` with this format:

```markdown
# Sprint 56: Quality Audit Action Plan

> Generated from 6 parallel audit sub-agents + Code Reviewer synthesis

## Executive Summary
- Total findings: N
- Critical: N, High: N, Medium: N, Low: N
- Estimated fix effort: N sprint(s)

## P0: Must Fix (Critical)
| # | Finding | File:Line | Category | Fix |
|---|---------|-----------|----------|-----|
| 1 | ... | ... | unwrap/test/etc | ... |

## P1: Should Fix (High)
(same table format)

## P2: Nice to Have (Medium)
(same table format)

## P3: Backlog (Low)
(same table format)

## Suggested Fix Sprint Structure
- Sprint 57a: P0 fixes (estimated N files)
- Sprint 57b: P1 fixes (estimated N files)
- Sprint 57c: P2 improvements (estimated N files)

## Cross-Reference Notes
(Any patterns that span multiple audit dimensions)
```

**IMPORTANT**: The action plan is the KEY deliverable. It must be actionable — every finding needs a specific file:line reference and a concrete fix suggestion. This plan will be used to create the follow-up fix sprint.
