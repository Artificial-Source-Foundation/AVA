# AVA Model Benchmarking System

AVA includes a comprehensive internal model benchmarking system for evaluating LLM code quality, speed, agent capabilities, and cost efficiency. It runs models through 35 standardized tasks across 12 categories in 4 programming languages, with multi-tier validation and optional SOTA judge council scoring.

**Source files:**
- `crates/ava-tui/src/benchmark.rs` -- benchmark runner, metrics collection, judge evaluation
- `crates/ava-tui/src/benchmark_tasks.rs` -- 35 task definitions, test harnesses, setup code
- `crates/ava-tui/src/benchmark_harness.rs` -- harnessed-pair benchmarking (SOTA director + fast worker)
- `crates/ava-tui/src/config/cli.rs` -- CLI flags (`--benchmark`, `--models`, `--judges`, `--suite`, `--harness`)

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

For non-tool tasks that include a `TestHarness`, the runner extracts code from the model's output, writes it to a temp file with the test harness appended, compiles, and runs tests. Supports multiple languages:

- **Rust**: `rustc --edition 2021 --test` -- extracts from ` ```rust ` fences, generic fences, bare `fn` definitions
- **Python**: `python3` -- extracts from ` ```python ` fences, parses unittest/pytest output
- **JavaScript**: `node` -- extracts from ` ```javascript `/` ```typescript ` fences
- **Go**: `go run` -- extracts from ` ```go ` fences

Results: `compile_success`, `tests_passed`, `tests_total`, `compile_error`.

The extraction logic auto-injects standard imports (e.g., `use std::collections::HashMap`) if the test harness references them but the model's code does not.

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
| `idiomatic` | Idiomatic patterns -- ownership, error handling, iterators, type system. |

Judges use `ThinkingLevel::High` for deeper analysis:

- Anthropic models: extended thinking (high budget)
- OpenAI models: `reasoning_effort: "high"`
- Gemini models: `reasoning_effort: "high"`
- Other providers: graceful fallback to standard generation

**Recommended judges** (SOTA council):
- `openrouter:anthropic/claude-opus-4.6` -- extended thinking, highest quality
- `openrouter:openai/gpt-5.4` -- strong all-rounder
- `openrouter:google/gemini-3.1-pro-preview` -- best value frontier

## Benchmark Suites

The `--suite` flag filters tasks by difficulty tier for fair comparisons within weight classes.

### speed

For speed/coding-specialist models. Includes single function generation, basic tool use, test generation, and compile+test validation.

Tasks: `is_palindrome`, `merge_sorted`, `lru_cache`, `bash_echo`, `read_cargo`, `generate_tests_stack`, `generate_tests_parser`, `generate_tests_result`, plus multi-language tasks (Python, JS, Go).

### standard

For mid-tier agent-capable models. All speed tasks plus bugfix tasks, constraint following, self-correction, security fixes, and advanced Rust.

Additional tasks: `bugfix_off_by_one`, `bugfix_lifetime`, `refactor_extract`, `multi_step_debug`, `constraint_edit`, `self_correct_compile`, `tool_efficiency`, `no_overengineer`, `error_recovery_loop`, `fix_sql_injection`, `fix_path_traversal`, `fix_integer_overflow`, `concurrent_counter`, `iterator_adapter`, `binary_tree`, `state_machine`.

### frontier

For SOTA models. Everything including hard multi-file agentic workflows.

Additional tasks: `cross_file_refactor`, `find_and_fix_across_files`.

## Harnessed-Pair Benchmarking

Tests whether a SOTA director model controlling a fast worker model outperforms either alone.

```bash
cargo run --bin ava -- --benchmark --harness \
  --director "openrouter:anthropic/claude-opus-4.6" \
  --worker "inception:mercury-2" \
  --suite speed
```

**Architecture**: Uses AVA's Praxis multi-agent system. The director receives the task, plans the approach, delegates coding to the worker, and reviews the result.

**Phases**: (1) Solo director runs, (2) Solo worker runs, (3) Harnessed pair runs. Results are compared across all three.

**Key finding**: The harnessed pair can solve problems neither model solves alone (1+1=3 effect), while being faster than the director solo and more reliable than the worker solo.

## Model Categories and Fair Comparisons

### Speed Tier

Comparing fast/cheap coding models against each other:

| Model | Provider | Price (in/out per 1M) | Notes |
|---|---|---|---|
| Mercury 2 | inception | $0.25/$0.75 | Diffusion LLM, ~1000 tok/s |
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

## Task Catalog (35 tasks)

### Coding Tasks -- Rust (Speed Suite)

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `is_palindrome` | Simple | 5 | String manipulation -- case folding, non-alphanumeric filtering |
| `merge_sorted` | Medium | 4 | Algorithm -- O(n+m) merge of two sorted slices |
| `lru_cache` | Hard | 3 | Data structure -- HashMap + ordering container, O(1) get/put, eviction |

### Basic Tool Use (Speed Suite)

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `bash_echo` | ToolUse | -- | Can the model invoke the bash tool and report output |
| `read_cargo` | RealWorld | -- | Can the model read a file and extract structured info |

### Test Generation (Speed Suite)

Tasks where the model must generate unit tests for given code. Validates that model-written tests compile and pass against the real implementation.

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `generate_tests_stack` | TestGeneration | 5+ | Write comprehensive tests for a `Stack<T>` (push, pop, peek, edge cases) |
| `generate_tests_parser` | TestGeneration | 4+ | Write tests for a CSV parser (empty input, columns, whitespace) |
| `generate_tests_result` | TestGeneration | 4+ | Write tests for a custom `Outcome<T,E>` type (map, unwrap, is_success) |

### Advanced Rust (Standard Suite)

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `concurrent_counter` | Hard | 3 | Thread-safe `Counter` with Arc/Mutex/Atomic, parallel increment |
| `iterator_adapter` | Medium | 4 | Custom `Batched<I>` iterator adapter with trait extension |
| `binary_tree` | Hard | 4 | Generic BST with insert, contains, min, in-order traversal |
| `state_machine` | Medium | 4 | Turnstile state machine with Locked/Unlocked states and Coin/Push events |

### Multi-Language Tasks (Speed Suite)

**Python:**

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `py_two_sum` | Simple | 3 | Classic two-sum with HashMap approach |
| `py_flatten_nested` | Medium | 3 | Recursive nested list flattening |
| `py_async_rate_limiter` | Hard | 2 | asyncio rate limiter with token bucket |

**JavaScript:**

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `js_debounce` | Medium | 2 | Debounce function with timer management |
| `js_deep_clone` | Medium | 2 | Deep clone handling nested objects, arrays, dates |
| `js_react_component` | Hard | 2 | React component with state, effects, event handlers |

**Go:**

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `go_reverse_linked_list` | Medium | 3 | Linked list reversal with pointer manipulation |
| `go_concurrent_map` | Hard | 2 | Thread-safe map with sync.RWMutex |

### Agentic Editing (Standard Suite)

Tier 3 tasks. The runner writes buggy files to the workspace, then the agent must use tools (read, edit, bash) to fix them.

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `bugfix_off_by_one` | Agentic | 6 | Fix binary search off-by-one (`arr.len()` → `arr.len() - 1`) |
| `bugfix_lifetime` | Agentic | 3 | Fix missing Rust lifetime annotations |
| `refactor_extract` | Agentic | 5 | Extract validation logic into separate function |

### Security (Standard Suite)

Tier 3 agentic tasks focused on finding and fixing security vulnerabilities.

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `fix_sql_injection` | Security | 3 | Fix string-concatenation SQL, repair broken input sanitization |
| `fix_path_traversal` | Security | 4 | Fix `../` path traversal, add null byte and `..` checks |
| `fix_integer_overflow` | Security | 3 | Replace unchecked arithmetic with `checked_mul`/`checked_add` |

### Agent Quality (Standard/Frontier Suite)

Higher-order agent behaviors -- multi-step reasoning, constraint following, and self-correction.

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `multi_step_debug` | MultiStep | 3 | Read tests → find bug → fix → verify. Multi-file navigation. |
| `constraint_edit` | ConstraintFollowing | 5 | Implement one function only, leave others as stubs. Selective editing. |
| `self_correct_compile` | SelfCorrection | 2 | Diagnose missing import, add it, re-verify. |
| `tool_efficiency` | MultiStep | 2 | Navigate multi-file project, find config module, add field. |
| `no_overengineer` | ConstraintFollowing | 2 | Add only a doc comment. Must not change function body. |
| `error_recovery_loop` | SelfCorrection | 2 | Replace broken external dep with stdlib, fix all usages. |

### Multi-File Navigation (Frontier Suite)

Tier 3 agentic tasks requiring cross-file understanding and coordinated edits.

| Task | Category | Tests | What It Tests |
|---|---|---|---|
| `cross_file_refactor` | Agentic | 2 | Extract function from lib.rs to utils.rs, update imports |
| `find_and_fix_across_files` | Agentic | 2 | Fix wrong field name references in client.rs from config.rs |

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
- `cost_per_task_usd` -- Cost for this task if it passed, None if failed (for cost-per-resolved analysis)

### Quality Metrics
- `quality_pass` -- Whether all regex patterns matched (Tier 1)
- `quality_details` -- Human-readable quality summary
- `compile_success` -- Whether compilation succeeded (Tier 2/3)
- `tests_passed` / `tests_total` -- Test pass rate (Tier 2/3)
- `compile_error` -- Compiler error message if compilation failed

### Agent Behavior Metrics
- `tool_calls_count` -- Total number of tool invocations
- `tool_calls_detail` -- List of tool names called (e.g., `["read", "edit", "bash"]`)
- `turns_used` -- Number of assistant response turns consumed
- `self_corrections` -- Number of times the model retried after a tool error
- `tool_efficiency_score` -- Ratio of minimum expected tools to actual tools used (1.0 = perfect, lower = wasteful). Only for tool-using tasks with `expected_min_tools` set.

### Consistency Metrics
- `consistency_hash` -- Hash of the code output for variance tracking. Run the same benchmark multiple times to measure how consistently a model produces equivalent code.

### Judge Scores
- `correctness` -- 0-10, averaged across all judges
- `code_quality` -- 0-10, averaged across all judges
- `efficiency` -- 0-10, averaged across all judges
- `idiomatic` -- 0-10, averaged across all judges
- `average` -- Mean of the four dimension scores
- `evaluations` -- Per-judge breakdown with individual scores and notes

### Aggregate Report Metrics
- `aggregate_cost_per_resolved` -- Total cost / number of resolved tasks (lower = better value)
- `aggregate_tool_efficiency` -- Mean tool efficiency score across all tool-using tasks

## External Benchmark Import

AVA can import tasks from established industry benchmarks and run them with AVA's unique metrics overlay (cost-per-resolved, tool efficiency, consistency tracking).

### Aider Polyglot Import

The [Aider Polyglot Benchmark](https://github.com/Aider-AI/polyglot-benchmark) contains 225 Exercism problems across 6 languages. AVA imports exercises for supported languages (Rust, Python, JavaScript, Go) and converts them to `BenchmarkTask` format.

**Source**: `crates/ava-tui/src/benchmark_import.rs`

**How it works:**
1. Clone the repo: `git clone https://github.com/Aider-AI/polyglot-benchmark ~/.ava/benchmarks/polyglot`
2. The importer reads each exercise's `.docs/instructions.md` for the prompt and test file for the harness
3. Rust `#[ignore]` attributes are stripped (Exercism uses them for progressive unlocking)
4. Tasks get the `MultiLang` category and run through AVA's standard Tier 2 validation
5. All of AVA's metrics apply: cost-per-resolved, tool efficiency, consistency hash, judge scores

**What you get**: Industry-standard Aider Polyglot results PLUS AVA's unique efficiency metrics. Instead of just "72% pass rate", you get "72% at $0.03/resolved with 4.2 avg tool calls."

### Future Imports (Planned)

| Benchmark | Status | What It Adds |
|---|---|---|
| Aider Polyglot | Implemented | 225 Exercism problems, 6 languages |
| BFCL v4 | Planned | 2,000+ tool calling scenarios |
| SWE-bench Verified | Planned | 500 real GitHub issue resolutions (needs Docker) |
| Multi-SWE-bench | Planned | 1,632 tasks across 7 languages |

## What Makes AVA's Benchmark Different

Most industry benchmarks (SWE-bench, HumanEval, LiveCodeBench) test code generation in isolation. AVA's benchmark is designed specifically for **coding agent evaluation**:

1. **Tool-harness aware.** Tasks measure how models use tools (read, edit, bash, glob, grep), not just whether they produce correct code. A model that solves a problem in 3 tool calls is scored higher than one that takes 15.

2. **Cost-normalized resolution.** `cost_per_task_usd` and `aggregate_cost_per_resolved` answer the real question: "how much does it cost to get a correct answer?" This enables fair comparison across price tiers.

3. **Harnessed-pair evaluation.** No other public benchmark tests SOTA-director + fast-worker model combinations. This matches how agents are actually deployed in production.

4. **Multi-tier validation.** Tier 1 (regex) catches gross errors fast. Tier 2 (compile+test) provides objective pass/fail. Tier 3 (agentic editing) validates real-world tool use. Judge council adds nuanced quality scoring.

5. **Security and constraint following.** Dedicated task categories for security vulnerability fixing and constraint following (restraint, selective editing) -- critical skills for coding agents that most benchmarks ignore.

6. **Variance tracking.** `consistency_hash` enables measuring how deterministic a model is across runs. High variance models are risky for production use even if they score well on average.

7. **Multi-language coverage.** Rust, Python, JavaScript, and Go tasks ensure models aren't just good at one language.

## Usage

### CLI Flags

| Flag | Description |
|---|---|
| `--benchmark` | Enable benchmark mode (replaces normal operation) |
| `--models "p:m,p:m"` | Models to benchmark in `provider:model` format, comma-separated |
| `--judges "p:m,p:m"` | Judge models for LLM-as-Judge evaluation (optional) |
| `--suite speed\|standard\|frontier\|all` | Task suite (default: standard) |
| `--harness` | Enable harnessed-pair benchmarking |
| `--director "p:m"` | Director model for harness mode |
| `--worker "p:m"` | Worker model for harness mode |
| `--import-polyglot <path>` | Import Aider Polyglot tasks from a local repo clone |
| `--provider` + `--model` | Alternative: single provider with comma-separated models |
| `--max-turns N` | Max agent turns per task (default: 10 for tool tasks, 3 for code gen) |

### Examples

```bash
# Quick speed comparison: Mercury vs Haiku
cargo run --bin ava -- --benchmark --suite speed \
  --models "inception:mercury-2,openrouter:anthropic/claude-haiku-4.5"

# Speed tier shootout with judges
cargo run --bin ava -- --benchmark --suite speed \
  --models "inception:mercury-2,openrouter:anthropic/claude-haiku-4.5,openrouter:google/gemini-3-flash-preview" \
  --judges "openrouter:anthropic/claude-opus-4.6,openrouter:openai/gpt-5.4"

# Standard tier evaluation
cargo run --bin ava -- --benchmark --suite standard \
  --models "openrouter:anthropic/claude-sonnet-4"

# Frontier evaluation with full judge council
cargo run --bin ava -- --benchmark --suite frontier \
  --models "openrouter:anthropic/claude-opus-4.6" \
  --judges "openrouter:openai/gpt-5.4,openrouter:google/gemini-3.1-pro-preview"

# Harnessed-pair: Opus directing Mercury
cargo run --bin ava -- --benchmark --harness --suite speed \
  --director "openrouter:anthropic/claude-opus-4.6" \
  --worker "inception:mercury-2"

# All tasks across all languages
cargo run --bin ava -- --benchmark --suite all \
  --models "openrouter:anthropic/claude-sonnet-4"

# Import Aider Polyglot benchmark (225 Exercism problems across 6 languages)
git clone https://github.com/Aider-AI/polyglot-benchmark ~/.ava/benchmarks/polyglot
cargo run --bin ava -- --benchmark --import-polyglot ~/.ava/benchmarks/polyglot \
  --models "inception:mercury-2,openrouter:anthropic/claude-haiku-4.5"
```

### Output

The runner prints a formatted comparison table to stderr during execution, showing per-task results with pass/fail status, timing, cost, and compile+test outcomes. Agent tasks also show tool call count, turns used, self-corrections, and tool efficiency score.

After all tasks complete, the full results are serialized to JSON and saved at `~/.ava/benchmarks/bench-{timestamp}.json`. Aggregate metrics (cost-per-resolved, tool efficiency) are included in the report.

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
- **Reproducibility.** Consistency hashing enables variance tracking across runs. A model that produces different code each time is less reliable for production use.
- **Real-world relevance.** Security fixes, multi-file navigation, and cross-file refactoring reflect what coding agents actually do in practice.
