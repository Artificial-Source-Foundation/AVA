# AVA Model Benchmarking System

AVA includes an internal model benchmarking system for evaluating LLM code quality, speed, and agent capabilities. It runs models through standardized Rust coding tasks and optionally uses a SOTA judge council for nuanced quality scoring.

**Source files:**
- `crates/ava-tui/src/benchmark.rs` -- benchmark runner, metrics collection, judge evaluation
- `crates/ava-tui/src/benchmark_tasks.rs` -- task definitions, test harnesses, setup code
- `crates/ava-tui/src/config/cli.rs` -- CLI flags (`--benchmark`, `--models`, `--judges`)

## Directory Structure

| Path | Purpose |
|---|---|
| `~/.ava/benchmarks/bench-{timestamp}.json` | Benchmark result files (JSON) |
| `~/.ava/benchmarks/workspace/` | Isolated workspace for benchmark runs -- all agent tool calls (read, edit, bash) operate here, not in the project directory |
| System temp dir (`/tmp/...`) | Temporary compilation files for Tier 2/3 test validation (auto-cleaned) |

Benchmarks do **not** write to the project directory. The workspace is created at the start of each benchmark run. A copy of the project's `Cargo.toml` is placed in the workspace so that the `read_cargo` task can function. Tier 3 agentic setup files (buggy code) are also written to the workspace. The agent's `working_dir` is set to the workspace via `AgentStackConfig`, so project-root detection, codebase indexing, and tool discovery all target the workspace instead of the real project.

## Architecture

The benchmark system uses a multi-tier evaluation pipeline. Each tier adds depth, and tiers are cumulative (a task with a test harness gets Tier 1 + Tier 2; an agentic task gets Tier 1 + Tier 3).

### Tier 1: Speed Metrics + Regex Pattern Matching

Every task gets Tier 1 evaluation automatically. The runner captures timing and token metrics from the `AgentEvent` stream, then checks the model's output against regex patterns defined per task.

Metrics captured: TTFT (ms), total time (ms), tokens/second, input/output token counts, cost in USD.

Quality check: each task defines `expected_patterns` (regex). All patterns must match for a `quality_pass = true`.

### Tier 2: Compile and Test (Code Generation Tasks)

For non-tool tasks that include a `TestHarness`, the runner extracts Rust code from the model's output (searching for ` ```rust ` fences, generic fences, bare function definitions, or anything containing `fn`), writes it to a temp file with the test harness appended, compiles with `rustc --edition 2021 --test`, and runs the binary.

Results: `compile_success`, `tests_passed`, `tests_total`, `compile_error`.

The extraction logic auto-injects `use std::collections::HashMap` and `use std::hash::Hash` if the test harness references them but the model's code does not.

### Tier 3: Agentic Editing Tasks

For tasks with `needs_tools: true` and a `TestHarness` containing `setup_code`, the runner:

1. Writes the buggy/incomplete setup code to `~/.ava/benchmarks/workspace/` before the run.
2. Spins up a full `AgentStack` with auto-approve enabled (`yolo: true`) and `working_dir` set to the workspace.
3. Lets the agent read, edit, and bash its way to a fix -- all file operations are confined to the workspace.
4. After the agent finishes, reads the file back, appends the test harness, and compiles + runs tests.

This validates that the agent actually fixed the file on disk, not just described a fix.

### LLM-as-Judge Evaluation

When `--judges` is provided, each judge model scores every benchmark result on four dimensions (0-10 each):

| Dimension | What it measures |
|---|---|
| `correctness` | Does the code solve the task? Edge cases, boundary conditions. |
| `code_quality` | Cleanliness, readability, structure, naming, modularity. |
| `efficiency` | Algorithmic time and space complexity. |
| `idiomatic` | Idiomatic Rust patterns -- ownership, error handling, iterators, type system. |

Judges receive the original task prompt, the model's raw output (truncated to 4000 chars), compilation results, and test results. They use `ThinkingLevel::High` for deeper analysis:

- Anthropic models: extended thinking (high budget)
- OpenAI models: `reasoning_effort: "high"`
- Gemini models: `reasoning_effort: "high"`
- Other providers: graceful fallback to standard generation

Scores from all judges are averaged per dimension, and a composite `average` is computed as the mean of all four dimensions.

**Recommended judges** (SOTA council):
- `openrouter:anthropic/claude-opus-4.6` -- extended thinking, highest quality
- `openrouter:openai/gpt-5.4` -- strong all-rounder
- `openrouter:google/gemini-3.1-pro-preview` -- best value frontier

## Benchmark Suites (Planned)

The `--suite` flag (planned) will filter tasks by difficulty tier for fair comparisons within weight classes.

### speed

For speed/coding-specialist models. Includes single function generation tasks, basic tool use, and compile+test validation. Key metrics: TTFT, tokens/second, cost efficiency, judge scores on code quality.

Tasks included: `is_palindrome`, `merge_sorted`, `lru_cache`, `bash_echo`, `read_cargo`.

### standard

For mid-tier agent-capable models. All speed tasks plus bugfix tasks, constraint following, and self-correction.

Tasks included: everything in speed, plus `bugfix_off_by_one`, `bugfix_lifetime`, `refactor_extract`, `multi_step_debug`, `constraint_edit`, `self_correct_compile`, `tool_efficiency`, `no_overengineer`, `error_recovery_loop`.

### frontier

For SOTA models. Everything including hard multi-step agentic workflows. All tasks from standard, evaluated with full judge council.

## Model Categories and Fair Comparisons

### Speed Tier

Comparing fast/cheap coding models against each other:

| Model | Provider | Price (in/out per 1M) | Notes |
|---|---|---|---|
| Mercury Coder | inception | $0.25/$0.75 | Diffusion LLM, ~1000 tok/s |
| Claude Haiku 4.5 | openrouter | $1/$5 | Anthropic's fast model |
| GPT-5.3 Codex | openrouter | $1.75/$14 | OpenAI coding specialist |
| Gemini 3 Flash | openrouter | $0.50/$3 | Google's fast model |
| Grok 4.1 Fast | openrouter | $0.20/$0.50 | xAI's cheapest, 2M context |
| DeepSeek Coder | openrouter | ~$0.14/$0.28 | Chinese speed coder |
| Qwen Coder | openrouter | ~$0.10/$0.30 | Alibaba's coder |

### Standard Tier

Mid-tier agent-capable models:

| Model | Provider | Notes |
|---|---|---|
| Claude Sonnet 4.6 | openrouter | Anthropic's balanced model |
| GPT-5.4 (no reasoning) | openrouter | OpenAI mid-tier usage |
| Gemini 3.1 Pro | openrouter | Google's capable model |

### Frontier Tier

SOTA models, also used as judges:

| Model | Provider | Notes |
|---|---|---|
| Claude Opus 4.6 | openrouter | Best for code, $5/$25 |
| GPT-5.4 (xhigh reasoning) | openrouter | Strong all-rounder, $2.50/$15 |
| Gemini 3.1 Pro (high reasoning) | openrouter | Best value frontier, $2/$12 |
| Kimi K2.5 | openrouter | Chinese frontier |

## Task Catalog

### Coding Tasks (Speed Suite)

| Task | Category | Difficulty | Tests | What It Tests |
|---|---|---|---|---|
| `is_palindrome` | Simple | Easy | 5 | Basic string manipulation -- case folding, non-alphanumeric filtering |
| `merge_sorted` | Medium | Medium | 4 | Algorithm implementation -- O(n+m) merge of two sorted slices |
| `lru_cache` | Hard | Hard | 3 | Complex data structure -- `LruCache<K, V>` with HashMap + ordering container, eviction policy, recency update |

### Basic Tool Use (Speed Suite)

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `bash_echo` | ToolUse | -- | Can the model invoke the bash tool and report output |
| `read_cargo` | RealWorld | -- | Can the model read a file and extract structured info (workspace members) |

### Agentic Editing (Standard Suite)

These are Tier 3 tasks. The runner writes buggy files to `~/.ava/benchmarks/workspace/`, then the agent must use tools (read, edit, bash) to fix them. Post-run validation compiles the edited file with a test harness.

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `bugfix_off_by_one` | Agentic | 6 | Fix binary search off-by-one (`arr.len()` should be `arr.len() - 1`) |
| `bugfix_lifetime` | Agentic | 3 | Fix missing Rust lifetime annotations on `longest()` and `Wrapper` struct |
| `refactor_extract` | Agentic | 5 | Extract validation logic into a separate `pub fn validate()` function |

### Agent Quality (Standard/Frontier Suite)

These tasks evaluate higher-order agent behaviors -- multi-step reasoning, constraint following, and self-correction.

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `multi_step_debug` | MultiStep | 3 | Read tests, find bug in `perimeter()`, fix it, verify with rustc. Tests multi-file navigation. |
| `constraint_edit` | ConstraintFollowing | 5 | Implement `validate_email` only -- must leave `validate_phone` and `validate_url` as stubs. Tests selective editing discipline. |
| `self_correct_compile` | SelfCorrection | 2 | Cache struct uses `HashMap` without importing it. Agent must compile, diagnose, add `use std::collections::HashMap`, re-verify. |
| `tool_efficiency` | MultiStep | 2 | Navigate a multi-file project structure (`src/main.rs`, `lib.rs`, `utils.rs`, `config.rs`), find the config module, add a `timeout_seconds: u32` field with default 30. Tests exploration efficiency. |
| `no_overengineer` | ConstraintFollowing | 2 | Add only a doc comment to `add()` function. Must not change the function body, add tests, or restructure. Tests restraint. |
| `error_recovery_loop` | SelfCorrection | 2 | File imports `nonexistent_crate::Thing`. Agent must replace with `std::collections::HashMap` and fix all usages. Tests diagnosis and recovery from compile errors. |

## Metrics Captured

Every benchmark result (`BenchmarkResult`) captures:

### Speed Metrics
- `ttft_ms` -- Time to first token (milliseconds)
- `total_time_ms` -- Total wall-clock time for the run
- `tokens_per_second` -- Output tokens divided by total time

### Cost Metrics
- `input_tokens` -- Total input tokens consumed
- `output_tokens` -- Total output tokens generated
- `cost_usd` -- Total cost in USD (from provider pricing)

### Quality Metrics
- `quality_pass` -- Whether all regex patterns matched (Tier 1)
- `quality_details` -- Human-readable quality summary
- `compile_success` -- Whether `rustc --test` succeeded (Tier 2/3)
- `tests_passed` / `tests_total` -- Test pass rate (Tier 2/3)
- `compile_error` -- Compiler error message if compilation failed

### Agent Behavior Metrics
- `tool_calls_count` -- Total number of tool invocations
- `tool_calls_detail` -- List of tool names called (e.g., `["read", "edit", "bash"]`)
- `turns_used` -- Number of assistant response turns consumed
- `self_corrections` -- Number of times the model retried after a tool error

### Judge Scores
- `correctness` -- 0-10, averaged across all judges
- `code_quality` -- 0-10, averaged across all judges
- `efficiency` -- 0-10, averaged across all judges
- `idiomatic` -- 0-10, averaged across all judges
- `average` -- Mean of the four dimension scores
- `evaluations` -- Per-judge breakdown with individual scores and notes

## Usage

### CLI Flags

| Flag | Description |
|---|---|
| `--benchmark` | Enable benchmark mode (replaces normal operation) |
| `--models "p:m,p:m"` | Models to benchmark in `provider:model` format, comma-separated |
| `--judges "p:m,p:m"` | Judge models for LLM-as-Judge evaluation (optional) |
| `--provider` + `--model` | Alternative: single provider with comma-separated models |
| `--max-turns N` | Max agent turns per task (default: 10 for tool tasks, 3 for code gen) |

### Examples

```bash
# Quick speed comparison: Mercury vs Haiku
cargo run --bin ava -- --benchmark \
  --models "inception:mercury-coder-small,openrouter:anthropic/claude-haiku-4.5"

# Speed tier shootout with judges
cargo run --bin ava -- --benchmark \
  --models "inception:mercury-coder-small,openrouter:anthropic/claude-haiku-4.5,openrouter:google/gemini-3-flash-preview" \
  --judges "openrouter:anthropic/claude-opus-4.6,openrouter:openai/gpt-5.4,openrouter:google/gemini-3.1-pro-preview"

# Standard tier evaluation
cargo run --bin ava -- --benchmark \
  --models "openrouter:anthropic/claude-sonnet-4"

# Frontier evaluation
cargo run --bin ava -- --benchmark \
  --models "openrouter:anthropic/claude-opus-4.6"

# Single provider shorthand (benchmarks two models from the same provider)
cargo run --bin ava -- --benchmark \
  --provider openrouter \
  --model "anthropic/claude-haiku-4.5,anthropic/claude-sonnet-4"

# Results are saved to ~/.ava/benchmarks/bench-{timestamp}.json
```

### Output

The runner prints a formatted comparison table to stderr during execution, showing per-task results with pass/fail status, timing, cost, and compile+test outcomes. Agent tasks also show tool call count, turns used, and self-corrections.

After all tasks complete, the full results are serialized to JSON and saved at `~/.ava/benchmarks/bench-{timestamp}.json`.

## Judge System Details

- Judges are optional -- compile+test validation runs without them for free.
- Each judge scores independently; scores are averaged across all judges for the final result.
- Judge costs: approximately $0.01-0.05 per evaluation depending on output length and model pricing.
- The judge prompt includes the task description, model output (truncated to 4000 characters), compilation result, and test results. Judges are instructed to think step by step through each dimension before scoring.
- JSON parsing is robust: the runner tries direct parse, ` ```json ` fences, and brace-delimited extraction.
- All scores are clamped to [0.0, 10.0].

## Configuration

### API Keys

Store provider credentials in `~/.ava/credentials.json`:

```json
{
  "providers": {
    "openrouter": { "api_key": "sk-or-..." },
    "inception": { "api_key": "..." }
  }
}
```

### Results Storage

JSON results are saved to `~/.ava/benchmarks/` with filenames like `bench-2026-03-10T14:30:00Z.json`.

### Timeouts

Each individual task run has a 120-second timeout. If the timeout fires, the cancellation token is triggered and the run reports an error.

### Agent Configuration

Benchmark runs use `AgentStack` with `yolo: true` (auto-approve all tool calls) and `working_dir` set to `~/.ava/benchmarks/workspace/` (isolated from the project codebase). Non-tool tasks get a max of 3 turns; tool tasks get the value from `--max-turns` (default: 10).

## Design Philosophy

- **Compare within weight class.** Do not benchmark speed models against frontier models. Compare Mercury vs Haiku vs Gemini Flash for speed; compare Opus vs GPT-5.4 vs Gemini Pro for frontier quality.
- **Objective + subjective.** Compile+test gives objective pass/fail. Judges add nuanced quality assessment that catches things like code style, unnecessary complexity, and non-idiomatic patterns.
- **Agent behavior matters.** Tool efficiency, constraint following, and self-correction are as important as code correctness for an AI coding agent. A model that passes all tests but uses 15 tool calls where 3 would suffice is not as good as one that navigates efficiently.
- **Cost awareness.** Track cost per task to evaluate quality-per-dollar, not just raw quality. A model at $0.25/M that scores 7/10 may be better value than one at $25/M that scores 9/10.
