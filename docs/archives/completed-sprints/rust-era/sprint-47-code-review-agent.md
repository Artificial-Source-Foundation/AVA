# Sprint 47: Automated Code Review Agent & CI Integration

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Build an automated code review agent that can review git diffs, provide structured feedback, and integrate with CI pipelines. After this sprint, AVA can review PRs from the command line with `ava review` and output machine-readable results for CI.

## Key Files to Read

```
crates/ava-commander/src/lib.rs          # Commander, workflow infrastructure (from Sprint 46)
crates/ava-commander/src/workflow.rs     # Workflow, Phase, PhaseRole (from Sprint 46)
crates/ava-commander/src/prompts.rs      # Phase-specific prompts (from Sprint 46)

crates/ava-agent/src/loop.rs             # AgentLoop, AgentEvent
crates/ava-agent/src/stack.rs            # AgentStack
crates/ava-agent/src/system_prompt.rs    # build_system_prompt()

crates/ava-tools/src/core/bash.rs        # Bash tool (for git commands)
crates/ava-tools/src/core/read.rs        # Read tool
crates/ava-tools/src/core/grep.rs        # Grep tool
crates/ava-tools/src/core/diagnostics.rs # Diagnostics tool
crates/ava-tools/src/core/lint.rs        # Lint tool
crates/ava-tools/src/registry.rs         # ToolRegistry

crates/ava-tui/src/config/cli.rs         # CliArgs
crates/ava-tui/src/headless.rs           # Headless mode

crates/ava-permissions/src/classifier.rs # Command classifier (for git safety)
```

## What Already Exists (after Sprint 46)

- **Workflow engine**: Phase-based pipeline execution with feedback loops
- **PhaseRole::Reviewer**: Role-specific system prompt for code review
- **Reviewer tools**: read-only + diagnostics + lint
- **Git safety**: Command classifier knows safe git commands (status, log, diff, branch)
- **Headless mode**: `--json` for structured output
- **All core tools**: read, write, edit, bash, glob, grep, diagnostics, lint, test_runner

## Theme 1: Review Agent

### Story 1.1: Review Subcommand

Add `ava review` as a CLI subcommand for automated code review.

**Implementation:**
- File: `crates/ava-tui/src/review.rs` (NEW)
- Update `CliArgs` to support subcommands:

```rust
#[derive(Debug, Clone, Parser)]
#[command(name = "ava")]
pub struct CliArgs {
    #[command(subcommand)]
    pub command: Option<Command>,

    // ... existing fields remain for default (agent) mode
}

#[derive(Debug, Clone, Subcommand)]
pub enum Command {
    /// Review code changes
    Review(ReviewArgs),
}

#[derive(Debug, Clone, Args)]
pub struct ReviewArgs {
    /// Review staged changes (default)
    #[arg(long)]
    pub staged: bool,

    /// Review changes between two refs
    #[arg(long)]
    pub diff: Option<String>,  // e.g., "main..HEAD"

    /// Review a specific commit
    #[arg(long)]
    pub commit: Option<String>,

    /// Review all uncommitted changes
    #[arg(long)]
    pub working: bool,

    /// Output format: text, json, or markdown
    #[arg(long, default_value = "text")]
    pub format: String,

    /// Focus areas (comma-separated): security, performance, style, bugs, all
    #[arg(long, default_value = "all")]
    pub focus: String,

    /// Provider and model (inherited from parent)
    #[arg(long)]
    pub provider: Option<String>,
    #[arg(long, short)]
    pub model: Option<String>,
}
```

**Acceptance criteria:**
- `ava review` reviews staged changes by default
- `ava review --diff main..HEAD` reviews a range
- `ava review --commit abc123` reviews a specific commit
- `ava review --working` reviews all uncommitted changes
- Existing `ava "goal"` behavior unchanged
- Add help text

### Story 1.2: Diff Collection & Context Building

Collect the git diff and build rich context for the review agent.

**Implementation:**
- File: `crates/ava-commander/src/review.rs` (NEW)

```rust
pub struct ReviewContext {
    pub diff: String,
    pub changed_files: Vec<ChangedFile>,
    pub stats: DiffStats,
}

pub struct ChangedFile {
    pub path: String,
    pub change_type: ChangeType,  // Added, Modified, Deleted, Renamed
    pub additions: usize,
    pub deletions: usize,
}

pub struct DiffStats {
    pub files_changed: usize,
    pub total_additions: usize,
    pub total_deletions: usize,
}
```

- Parse `git diff --stat` for file-level stats
- Parse `git diff` for the actual diff content
- For each changed file, also read the full file content (for context)
- Limit: if diff > 50KB, summarize and review file-by-file

**Acceptance criteria:**
- Collects diff for staged, working, commit, or range
- Parses file stats
- Handles large diffs by chunking
- Handles binary files gracefully (skip)
- Add tests with sample diffs

### Story 1.3: Review System Prompt

Build a specialized review prompt that produces structured output.

**Implementation:**
- In `crates/ava-commander/src/prompts.rs`, add `review_system_prompt()`:

```
You are a code review agent. Review the following diff carefully.

## Focus Areas
{focus_areas}

## Output Format
Produce a structured review with:
1. **Summary**: 1-2 sentence overview of the changes
2. **Issues**: List of issues found, each with:
   - Severity: critical | warning | suggestion | nitpick
   - File and line number
   - Description
   - Suggested fix (if applicable)
3. **Positives**: Things done well (brief)
4. **Verdict**: approve | request-changes | comment

## Rules
- Be specific — reference file paths and line numbers
- Don't flag style issues unless they affect readability
- Focus on correctness, security, and maintainability
- If changes look good, say so — don't invent issues
```

**Focus area prompts:**
| Focus | Additional Instructions |
|-------|----------------------|
| security | "Pay special attention to: injection vulnerabilities, unsafe deserialization, hardcoded secrets, permission issues, path traversal" |
| performance | "Look for: N+1 queries, unnecessary allocations, missing caching opportunities, O(n²) algorithms where O(n) is possible" |
| bugs | "Focus on: logic errors, off-by-one, null/None handling, error propagation, race conditions" |
| style | "Check: naming conventions, code organization, documentation, consistent patterns" |
| all | Include all focus areas |

**Acceptance criteria:**
- Review prompt is focused and structured
- Focus area customization works
- Output format is parseable
- Add test for prompt generation

### Story 1.4: Structured Review Output

Parse the agent's review response into a structured `ReviewResult`.

**Implementation:**
- File: `crates/ava-commander/src/review.rs`

```rust
pub struct ReviewResult {
    pub summary: String,
    pub issues: Vec<ReviewIssue>,
    pub positives: Vec<String>,
    pub verdict: ReviewVerdict,
}

pub struct ReviewIssue {
    pub severity: Severity,
    pub file: String,
    pub line: Option<usize>,
    pub description: String,
    pub suggestion: Option<String>,
}

pub enum Severity {
    Critical,
    Warning,
    Suggestion,
    Nitpick,
}

pub enum ReviewVerdict {
    Approve,
    RequestChanges,
    Comment,
}
```

- Parse the agent's text output into `ReviewResult`
- Use heuristic parsing (look for severity markers, file:line patterns)
- Fallback: if parsing fails, return the raw text as the summary

**Output formatters:**
- `text`: Human-readable terminal output with colors
- `json`: Machine-readable JSON (for CI)
- `markdown`: GitHub-compatible markdown (for PR comments)

**Acceptance criteria:**
- Agent output parsed into structured result
- Three output formats work
- JSON format is stable (for CI integration)
- Graceful fallback on parse failure
- Add tests

## Theme 2: CI Integration

### Story 2.1: Exit Codes for CI

Make `ava review` return meaningful exit codes.

**Implementation:**
| Exit Code | Meaning |
|-----------|---------|
| 0 | Approved — no critical issues |
| 1 | Request changes — critical or warning issues found |
| 2 | Error — review could not be completed |

- Also support `--fail-on` flag:
  ```rust
  #[arg(long, default_value = "critical")]
  pub fail_on: String,  // "critical", "warning", "suggestion", "any"
  ```

**Acceptance criteria:**
- Exit code 0 when approved
- Exit code 1 when issues found matching `--fail-on` threshold
- Exit code 2 on error
- Works in CI without TTY

### Story 2.2: GitHub Actions Example

Provide a ready-to-use GitHub Actions workflow.

**Implementation:**
- File: `docs/ci/github-actions-review.yml` (NEW)

```yaml
name: AVA Code Review
on: [pull_request]

jobs:
  review:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
      - name: Install AVA
        run: cargo install --path .
      - name: Run Review
        env:
          AVA_PROVIDER: openrouter
          AVA_MODEL: anthropic/claude-sonnet-4
          OPENROUTER_API_KEY: ${{ secrets.OPENROUTER_API_KEY }}
        run: |
          ava review --diff origin/main..HEAD --format json --fail-on warning > review.json
      - name: Post Review Comment
        if: always()
        run: |
          ava review --diff origin/main..HEAD --format markdown > review.md
          # Post as PR comment via gh cli
```

**Acceptance criteria:**
- Example workflow is complete and correct
- Environment variable support for provider/model/key
- Instructions in a README section

### Story 2.3: Environment Variable Config

Support CI-friendly configuration via environment variables.

**Implementation:**
- In `CliArgs::resolve_provider_model()`, check env vars as fallback:
  - `AVA_PROVIDER` → provider
  - `AVA_MODEL` → model
  - Priority: CLI flags > env vars > config file

- Provider API keys should already work via env vars in the existing credential system. Verify and document:
  - `OPENROUTER_API_KEY`
  - `ANTHROPIC_API_KEY`
  - `OPENAI_API_KEY`

**Acceptance criteria:**
- `AVA_PROVIDER` and `AVA_MODEL` env vars work
- Documented in help text
- Doesn't break existing config resolution
- Add test

## Implementation Order

1. Story 1.1 (review subcommand) — CLI structure
2. Story 1.2 (diff collection) — data gathering
3. Story 1.3 (review prompt) — agent instructions
4. Story 1.4 (structured output) — parsing + formatting
5. Story 2.3 (env var config) — CI prerequisite
6. Story 2.1 (exit codes) — CI integration
7. Story 2.2 (GitHub Actions example) — documentation

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Don't break existing `ava "goal"` behavior — `review` is a subcommand
- Keep the review agent focused — it should NOT modify files unless explicitly asked
- The review agent uses read-only tools by default (no write, no edit, no bash except git read commands)
- JSON output format must be stable — treat it as a public API
- Git commands used by the review agent must be read-only (no push, no commit, no checkout)

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
cargo test -p ava-commander -- --nocapture

# Manual test
echo "fn main() { let x = 1; }" > /tmp/test.rs
cd /tmp && git init && git add . && git commit -m "init"
echo "fn main() { let x: i32 = 1; println!(\"{}\", x); }" > test.rs
git add test.rs
cargo run --bin ava -- review --staged --format text --provider openrouter --model anthropic/claude-sonnet-4
cargo run --bin ava -- review --staged --format json --provider openrouter --model anthropic/claude-sonnet-4
```
