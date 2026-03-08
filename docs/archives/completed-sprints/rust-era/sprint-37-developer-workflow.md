# Sprint 37: Developer Workflow Mega-Sprint

> Combines Sprints 37 + 38 from the roadmap.

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in the "Key Files to Read" section
2. Read `CLAUDE.md` for project conventions
3. Read `docs/development/roadmap.md` for context
4. Enter plan mode and produce a detailed implementation plan
5. Get the plan confirmed before proceeding

## Goal

Give AVA the developer tools that make it useful for real coding workflows: applying patches, running tests, auto-fixing lint errors, and getting LSP diagnostics. After this sprint, AVA can do the full edit→test→fix cycle.

## Key Files to Read

```
# Tools
crates/ava-tools/src/lib.rs                    # Module structure
crates/ava-tools/src/core/mod.rs                # Core tool registration
crates/ava-tools/src/core/edit.rs               # Existing edit tool
crates/ava-tools/src/core/bash.rs               # Existing bash tool
crates/ava-tools/src/registry.rs                # Tool trait + ToolRegistry

# LSP (skeleton)
crates/ava-lsp/src/lib.rs                       # Module structure
crates/ava-lsp/src/client.rs                    # LspClient (foundation only)
crates/ava-lsp/src/transport.rs                 # JSON-RPC transport
crates/ava-lsp/src/error.rs                     # Error types
crates/ava-lsp/Cargo.toml                       # Dependencies

# TUI (for diff display)
crates/ava-tui/src/widgets/diff_preview.rs      # Existing diff widget (line-by-line)
crates/ava-tui/src/rendering/diff.rs            # Diff rendering

# Types
crates/ava-types/src/lib.rs                     # Tool, ToolCall, ToolResult types
crates/ava-platform/src/lib.rs                  # Platform abstraction
```

## What Already Exists

- 6 core tools (read, write, edit, bash, glob, grep) — all functional
- `edit` tool does single-region text replacement
- `bash` tool runs commands with sandbox support
- `diff_preview.rs` renders line-by-line diffs using `similar` crate
- `ava-lsp` has client primitives (request ID, diagnostics broadcast) but NO LSP methods
- `ava-tools/src/edit.rs` exists as a separate module from core edit

## Theme 1: Advanced Editing Tools

### Story 1.1: Multi-Edit Tool

A tool that applies multiple edits to one or more files in a single call. The current `edit` tool only does one replacement at a time.

**Tool definition:**
```rust
pub struct MultiEditTool { platform: Arc<dyn Platform> }

// Parameters:
{
    "edits": [
        {
            "path": "src/main.rs",
            "old_text": "fn old_name()",
            "new_text": "fn new_name()"
        },
        {
            "path": "src/lib.rs",
            "old_text": "use crate::old_name",
            "new_text": "use crate::new_name"
        }
    ]
}
```

**Implementation:**
- File: `crates/ava-tools/src/core/multiedit.rs`
- Validate all edits before applying any (dry-run check)
- If any `old_text` not found, return error listing which edits failed
- Apply edits in order within each file
- Return summary: files modified, edits applied

**Acceptance criteria:**
- Multi-file edits work in single call
- Atomic-ish: validates all before applying
- Clear error if any edit fails validation
- Register in `register_core_tools()`
- Add tests

### Story 1.2: Apply-Patch Tool

Apply a unified diff/patch to one or more files. Useful when the LLM generates a diff instead of edit instructions.

**Tool definition:**
```rust
pub struct ApplyPatchTool { platform: Arc<dyn Platform> }

// Parameters:
{
    "patch": "--- a/src/main.rs\n+++ b/src/main.rs\n@@ -1,3 +1,3 @@\n-fn old()\n+fn new()\n",
    "strip": 1  // optional, strip leading path components (like patch -p1)
}
```

**Implementation:**
- File: `crates/ava-tools/src/core/apply_patch.rs`
- Parse unified diff format
- Apply hunks to target files
- Handle context matching (fuzzy: allow offset of up to 3 lines)
- Return: files modified, hunks applied, any rejected hunks

**Acceptance criteria:**
- Applies standard unified diffs
- Handles multi-file patches
- Reports rejected hunks instead of crashing
- Add tests with sample patches

## Theme 2: Test & Lint Integration

### Story 2.1: Test Runner Tool

A tool that runs tests and returns structured results.

**Tool definition:**
```rust
pub struct TestRunnerTool { platform: Arc<dyn Platform> }

// Parameters:
{
    "command": "cargo test",           // optional, auto-detected if missing
    "filter": "test_name_pattern",     // optional
    "timeout": 60                      // seconds, default 60
}
```

**Auto-detection logic:**
- If `Cargo.toml` exists → `cargo test`
- If `package.json` exists → `npm test`
- If `pytest.ini` or `pyproject.toml` with pytest → `pytest`
- If `go.mod` exists → `go test ./...`
- Otherwise → return error asking user to specify command

**Implementation:**
- File: `crates/ava-tools/src/core/test_runner.rs`
- Run the test command via `tokio::process::Command`
- Capture stdout + stderr
- Parse exit code: 0 = all pass, non-zero = failures
- Truncate output if > 50KB (keep first + last sections)
- Return structured result: `{ passed: bool, output: String, exit_code: i32 }`

**Acceptance criteria:**
- Auto-detects project type
- Runs tests with timeout
- Returns structured pass/fail result
- Truncates long output
- Add tests (mock command execution)

### Story 2.2: Lint Tool

A tool that runs the project linter and optionally auto-fixes.

**Tool definition:**
```rust
pub struct LintTool { platform: Arc<dyn Platform> }

// Parameters:
{
    "command": "cargo clippy",    // optional, auto-detected
    "fix": false,                 // if true, run with --fix flag
    "path": "src/"               // optional, scope to directory
}
```

**Auto-detection:**
- `Cargo.toml` → `cargo clippy` (fix: `cargo clippy --fix --allow-dirty`)
- `package.json` with eslint → `npx eslint` (fix: `npx eslint --fix`)
- `pyproject.toml` with ruff → `ruff check` (fix: `ruff check --fix`)
- Otherwise → return error

**Implementation:**
- File: `crates/ava-tools/src/core/lint.rs`
- Run linter, capture output
- Parse warning/error counts if possible
- Return: `{ warnings: usize, errors: usize, output: String, fixed: bool }`

**Acceptance criteria:**
- Auto-detects linter
- Fix mode works
- Structured result with counts
- Add tests

## Theme 3: LSP Integration

### Story 3.1: LSP Client — Core Methods

Implement the essential LSP methods in `ava-lsp`. The client skeleton exists — add actual request handlers.

**Methods to implement:**
1. `diagnostics(path) -> Vec<Diagnostic>` — get errors/warnings for a file
2. `goto_definition(path, line, col) -> Option<Location>` — jump to definition
3. `hover(path, line, col) -> Option<String>` — get hover info
4. `references(path, line, col) -> Vec<Location>` — find all references

**Implementation:**
- File: `crates/ava-lsp/src/client.rs` (extend existing `LspClient`)
- Each method sends a JSON-RPC request and parses the response
- Use the existing transport layer
- Handle timeouts (5 second default)
- Handle "server not running" gracefully (return empty results, not errors)

**Acceptance criteria:**
- 4 LSP methods implemented
- Graceful handling when no LSP server is running
- JSON-RPC request/response correctly formatted
- Add tests with mock transport

### Story 3.2: LSP Diagnostics Tool

Expose LSP diagnostics as a tool the agent can use.

**Tool definition:**
```rust
pub struct DiagnosticsTool {
    lsp_client: Option<Arc<LspClient>>,
}

// Parameters:
{
    "path": "src/main.rs"
}

// Returns:
{
    "diagnostics": [
        { "line": 10, "severity": "error", "message": "unused variable: x" },
        { "line": 25, "severity": "warning", "message": "dead code" }
    ]
}
```

**Implementation:**
- File: `crates/ava-tools/src/core/diagnostics.rs`
- If LSP client is available, query it
- If not, fall back to running `cargo check 2>&1` and parsing rustc output
- Return structured diagnostics

**Acceptance criteria:**
- Works with LSP when available
- Falls back to compiler output when no LSP
- Structured diagnostic results
- Add tests

## Theme 4: Enhanced Diff Display

### Story 4.1: Word-Level Diffs

Upgrade `diff_preview.rs` from line-level to word-level diffs for better readability.

**Implementation:**
- In `crates/ava-tui/src/rendering/diff.rs`
- For changed lines, use `similar::TextDiff` at word granularity
- Highlight changed words within a line (not just mark entire line red/green)
- Keep line-level for added/removed lines, word-level for modified lines

**Acceptance criteria:**
- Modified lines show word-level highlights
- Added/removed lines still show line-level
- Doesn't break existing diff rendering
- Add test

## Implementation Order

1. Story 1.1 (multi-edit) — high value, builds on existing edit
2. Story 2.1 (test runner) — core workflow need
3. Story 2.2 (lint tool) — pairs with test runner
4. Story 1.2 (apply-patch) — more complex, do after multi-edit
5. Story 3.1 (LSP client methods) — foundation for diagnostics
6. Story 3.2 (diagnostics tool) — depends on 3.1
7. Story 4.1 (word-level diffs) — polish, do last

## Constraints

- **Rust only**
- New tools go in `crates/ava-tools/src/core/` and register in `register_core_tools()`
- LSP changes go in `crates/ava-lsp/src/`
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Don't break existing tools
- Tools must implement the `Tool` trait from `registry.rs`

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
cargo test -p ava-tools -- --nocapture
cargo test -p ava-lsp -- --nocapture
```
