# Benchmark Hardening And Transparency Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make AVA benchmarks more credible, fair, and transparent by isolating runs, separating model output from tool output, classifying invalid results, and generating publishable reports with reproducible metadata.

**Architecture:** Keep the current benchmark entrypoints in `crates/ava-tui/src/benchmark.rs` and `crates/ava-tui/src/benchmark_harness.rs`, but refactor the result schema and execution flow around explicit run artifacts. Each task-model execution should get an isolated workspace, structured transcripts, validity metadata, and a reproducible run context. Reporting should consume those structured artifacts instead of inferring meaning from one mixed `raw_output` string.

**Tech Stack:** Rust, Tokio, Serde, existing AVA benchmark runner in `crates/ava-tui/`, Markdown docs.

---

### Task 1: Add benchmark metadata and failure taxonomy

**Files:**
- Modify: `crates/ava-tui/src/benchmark.rs`
- Modify: `crates/ava-tui/src/benchmark_harness.rs`
- Modify: `docs/development/benchmarks/benchmark-system.md`

**Step 1: Add explicit metadata structs**

Add new serializable structs in `crates/ava-tui/src/benchmark.rs`:

```rust
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BenchmarkRunMetadata {
    pub benchmark_version: String,
    pub git_commit: Option<String>,
    pub suite: String,
    pub seed: Option<u64>,
    pub task_order: Vec<String>,
    pub model_order: Vec<String>,
    pub host_os: String,
    pub rust_version: Option<String>,
    pub started_at: String,
    pub cold_start: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum FailureClass {
    ModelFailure,
    HarnessFailure,
    InfraFailure,
    Timeout,
    InvalidResult,
}
```

**Step 2: Extend result structs**

Add fields to `BenchmarkResult` and `HarnessResult`:

```rust
pub failure_class: Option<FailureClass>,
pub failure_reason_code: Option<String>,
pub validity_note: Option<String>,
```

**Step 3: Extend report structs**

Add top-level metadata:

```rust
pub metadata: BenchmarkRunMetadata,
```

to `BenchmarkReport` and a matching metadata field to `HarnessReport`.

**Step 4: Run compile check**

Run: `cargo test -p ava-tui benchmark --no-run`

Expected: compiles successfully with new schema types.

---

### Task 2: Stop mixing assistant output with tool output

**Files:**
- Modify: `crates/ava-tui/src/benchmark.rs`

**Step 1: Split transcript capture**

Inside `run_single_task`, replace the single `total_output` accumulation with separate buffers:

```rust
let mut assistant_output = String::new();
let mut tool_transcript: Vec<String> = Vec::new();
let mut execution_log: Vec<String> = Vec::new();
```

**Step 2: Route events into the right buffers**

- `AgentEvent::Token` -> append to `assistant_output`
- `AgentEvent::ToolCall` / `AgentEvent::ToolResult` -> append summarized entries to `tool_transcript`
- keep timing/cost/tool counters unchanged

**Step 3: Change all validators to use assistant output only**

Update these call sites so they consume `assistant_output` instead of mixed output:
- regex quality checks
- `extract_code(...)`
- `consistency_hash`
- LLM judge prompt input

**Step 4: Preserve transcripts for debugging**

Store these fields on `BenchmarkResult`:

```rust
pub assistant_output: Option<String>,
pub tool_transcript: Option<Vec<String>>,
```

and deprecate or remove the current `raw_output` field.

**Step 5: Run targeted tests**

Run: `cargo test -p ava-tui benchmark -- --nocapture`

Expected: benchmark module tests still pass; extraction-related regressions are visible immediately if they break.

---

### Task 3: Isolate each benchmark run workspace

**Files:**
- Modify: `crates/ava-tui/src/benchmark.rs`
- Modify: `crates/ava-tui/src/benchmark_harness.rs`

**Step 1: Introduce per-run workspace creation helper**

Create a helper in `crates/ava-tui/src/benchmark.rs` like:

```rust
async fn create_benchmark_workspace(task_name: &str, model_name: &str) -> Result<PathBuf>
```

Use a timestamp or UUID under `~/.ava/benchmarks/runs/...`.

**Step 2: Copy baseline files into the isolated workspace**

Move current `Cargo.toml` copy logic into the helper. Keep the source project read-only.

**Step 3: Update regular benchmark runner**

Pass the per-run workspace into task setup and agent config instead of the single shared `workspace` directory.

**Step 4: Update harness runner**

Use the same isolated-workspace helper for:
- solo director runs
- solo worker runs
- harnessed pair runs

**Step 5: Keep artifacts**

Save each workspace path in the result struct so failed runs can be inspected later.

**Step 6: Verify contamination is gone**

Run the same task twice back-to-back with different models and confirm the second run starts from a clean directory.

---

### Task 4: Add deterministic seed support and fairer ordering

**Files:**
- Modify: `crates/ava-tui/src/benchmark.rs`
- Modify: `crates/ava-tui/src/benchmark_harness.rs`
- Modify: `crates/ava-tui/src/commands.rs`

**Step 1: Add CLI seed option**

Add a `--seed <u64>` flag to benchmark commands.

**Step 2: Shuffle tasks and model order with the seed**

Use a reproducible RNG. Save the resulting task order and model order into report metadata.

**Step 3: Default behavior**

If no seed is passed, generate one and print it at the start of the run.

**Step 4: Verify reproducibility**

Run the same benchmark twice with the same seed and confirm the logged task/model order matches.

---

### Task 5: Mark invalid and harness-caused results explicitly

**Files:**
- Modify: `crates/ava-tui/src/benchmark.rs`
- Modify: `crates/ava-tui/src/benchmark_import.rs`
- Modify: `crates/ava-tui/src/benchmark_tasks.rs`

**Step 1: Add execution assumptions to tasks**

Extend `BenchmarkTask` with metadata like:

```rust
pub execution_mode: Option<String>,
pub known_flaky: bool,
```

Use values like `single_file`, `package_test`, `multi_file`, `agentic_edit`.

**Step 2: Map common failures to failure classes**

Add a helper:

```rust
fn classify_failure(err: &str, task: &BenchmarkTask) -> (FailureClass, String)
```

Examples:
- provider policy block -> `InfraFailure`
- missing imported test file -> `HarnessFailure`
- extraction contamination -> `InvalidResult`
- compiler/test assertion errors -> `ModelFailure`

**Step 3: Use the classifier in regular and harness modes**

Make summary stats count valid vs invalid separately.

**Step 4: Verify with fixture cases**

Add unit tests for known errors and assert the expected `FailureClass` and `failure_reason_code`.

---

### Task 6: Improve summary reporting and publishable outputs

**Files:**
- Modify: `crates/ava-tui/src/benchmark.rs`
- Create: `crates/ava-tui/src/benchmark_report.rs`
- Modify: `docs/development/benchmarks/benchmark-system.md`

**Step 1: Move report formatting into a dedicated module**

Extract summary rendering from `benchmark.rs` into `benchmark_report.rs`.

**Step 2: Report valid vs invalid runs clearly**

Add summary sections for:
- valid model failures
- harness/infra failures
- invalid results excluded from model ranking

**Step 3: Generate Markdown and CSV outputs**

Write sidecar files next to JSON output:
- `bench-<timestamp>.md`
- `bench-<timestamp>.csv`

**Step 4: Include reproducibility block in Markdown**

Add run metadata, seed, model list, suite, commit, and caveats automatically.

**Step 5: Verify outputs**

Run one small benchmark and confirm JSON, Markdown, and CSV files are all emitted.

---

### Task 7: Add rerun support for invalid or noisy cases

**Files:**
- Modify: `crates/ava-tui/src/commands.rs`
- Modify: `crates/ava-tui/src/benchmark.rs`

**Step 1: Add rerun filters**

Support flags like:

```text
--rerun-invalid
--rerun-failure-class infra_failure
--rerun-task <task-name>
```

**Step 2: Reuse prior JSON report as input**

Add a loader that reads an existing report and schedules only the selected subset.

**Step 3: Verify UX**

Run a benchmark with a known invalid task, then rerun only that task from the saved report.

---

### Task 8: Tighten benchmark documentation and public methodology

**Files:**
- Modify: `docs/development/benchmarks/benchmark-system.md`
- Create: `docs/development/benchmarks/methodology.md`

**Step 1: Update system docs to match code**

Document actual execution behavior, workspace isolation, task classes, and failure taxonomy.

**Step 2: Add a methodology document**

Include:
- what counts as a valid result
- what gets excluded from rankings
- what "quality", "compile", and "full pass" mean
- cold/warm start policy
- seed and rerun policy

**Step 3: Add publication guidance**

Document which metrics are safe to post publicly and which caveats must be included.

---

### Task 9: Verification pass before merge

**Files:**
- Modify as needed based on failures from verification

**Step 1: Run Rust tests**

Run: `cargo test -p ava-tui`

Expected: all `ava-tui` tests pass.

**Step 2: Run lint/checks**

Run: `cargo clippy -p ava-tui -- -D warnings`

Expected: no clippy warnings in modified benchmark code.

**Step 3: Run a smoke benchmark**

Run a tiny seeded benchmark on 1-2 models and confirm:
- isolated workspace per run
- JSON includes metadata and failure class fields
- Markdown/CSV sidecars exist
- summary separates invalid/harness/infra failures from true model failures

**Step 4: Commit in small slices**

Recommended commit order:
- `feat(benchmark): add metadata and failure taxonomy`
- `fix(benchmark): isolate workspaces and split transcripts`
- `feat(benchmark): add seeded ordering and rerun support`
- `docs(benchmark): document methodology and reporting`

---

## Notes For The Implementer

- Keep the benchmark core in Rust; do not move new benchmark logic into TypeScript packages.
- Prefer adding small focused helpers instead of growing `crates/ava-tui/src/benchmark.rs` further.
- Be conservative about compatibility when changing the JSON schema; if needed, add versioning rather than silently breaking old reports.
- Treat imported-task execution mismatches as a transparency problem first, not as model failures.
