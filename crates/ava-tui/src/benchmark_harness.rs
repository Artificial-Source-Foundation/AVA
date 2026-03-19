//! Harnessed-pair benchmark for AVA.
//!
//! Tests a SOTA director model orchestrating a cheap/fast worker model.
//! The director (e.g., Opus) plans and reviews while the worker (e.g., Mercury)
//! executes the actual code edits. Compares performance against solo runs of each model.
//!
//! Usage:
//! ```bash
//! cargo run --bin ava -- --harness \
//!   --director "openrouter:anthropic/claude-opus-4.6" \
//!   --worker "inception:mercury-2"
//! ```

use std::collections::HashMap;
use std::path::Path;
use std::sync::Arc;
use std::time::Instant;

use ava_agent::AgentEvent;
use ava_config::CredentialStore;
use ava_llm::pool::ConnectionPool;
use ava_llm::provider::{LLMProvider, SharedProvider};
use ava_llm::providers::create_provider;
use ava_praxis::{Budget, Director, DirectorConfig, Domain, PraxisEvent, Task, TaskType};
use color_eyre::eyre::{eyre, Result};
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

use crate::benchmark::{BenchmarkResult, ModelSpec};
use crate::benchmark_tasks::{
    advanced_rust_tasks, agent_quality_tasks, agentic_tasks, default_tasks, filter_tasks_by_suite,
    go_tasks, multi_file_tasks, python_tasks, security_tasks, test_generation_tasks,
    typescript_tasks, BenchmarkSuite, BenchmarkTask,
};

/// Configuration for a harnessed-pair benchmark run.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HarnessConfig {
    pub director: ModelSpecSerde,
    pub worker: ModelSpecSerde,
}

/// Serializable version of ModelSpec.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ModelSpecSerde {
    pub provider: String,
    pub model: String,
}

impl From<&ModelSpec> for ModelSpecSerde {
    fn from(spec: &ModelSpec) -> Self {
        Self {
            provider: spec.provider.clone(),
            model: spec.model.clone(),
        }
    }
}

/// Result of a single harnessed-pair task run.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HarnessResult {
    pub task_name: String,
    pub task_category: String,
    pub director_model: String,
    pub worker_model: String,
    pub total_time_ms: u64,
    pub director_tokens: usize,
    pub worker_tokens: usize,
    pub director_cost: f64,
    pub worker_cost: f64,
    pub total_cost: f64,
    pub compile_success: Option<bool>,
    pub tests_passed: Option<usize>,
    pub tests_total: Option<usize>,
    pub quality_pass: bool,
    pub worker_calls: usize,
    pub total_turns: usize,
    pub error: Option<String>,
}

/// Full harness benchmark report.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HarnessReport {
    pub timestamp: String,
    pub config: HarnessConfig,
    pub results: Vec<HarnessResult>,
    pub solo_director_results: Option<Vec<BenchmarkResult>>,
    pub solo_worker_results: Option<Vec<BenchmarkResult>>,
}

/// Parse a single "provider:model" spec string.
pub fn parse_single_model_spec(spec: &str) -> Result<ModelSpec> {
    let (prov, mdl) = spec.split_once(':').ok_or_else(|| {
        eyre!(
            "Invalid model spec '{}'. Expected format: provider:model",
            spec
        )
    })?;
    Ok(ModelSpec {
        provider: prov.to_string(),
        model: mdl.to_string(),
    })
}

/// Short display name for a model (last segment of model path).
fn short_model_name(model: &str) -> String {
    model.rsplit('/').next().unwrap_or(model).to_string()
}

/// All Praxis domains used for worker routing.
const ALL_DOMAINS: &[Domain] = &[
    Domain::Backend,
    Domain::Frontend,
    Domain::QA,
    Domain::Fullstack,
    Domain::DevOps,
    Domain::Debug,
    Domain::Research,
];

/// Run the full harnessed-pair benchmark.
pub async fn run_harness(
    director_spec: ModelSpec,
    worker_spec: ModelSpec,
    max_turns: usize,
    suite: BenchmarkSuite,
) -> Result<HarnessReport> {
    let max_turns = if max_turns == 0 { 15 } else { max_turns };

    eprintln!(
        "[harness] Director: {}:{} | Worker: {}:{}",
        director_spec.provider, director_spec.model, worker_spec.provider, worker_spec.model
    );
    eprintln!("[harness] Suite: {} | Max turns: {}", suite, max_turns);

    // Workspace setup (same pattern as regular benchmark)
    let workspace_dir = dirs::home_dir()
        .unwrap_or_default()
        .join(".ava")
        .join("benchmarks")
        .join("workspace");
    tokio::fs::create_dir_all(&workspace_dir)
        .await
        .map_err(|e| eyre!("Failed to create benchmark workspace: {}", e))?;

    // Copy Cargo.toml for tasks that need it
    let project_cargo = std::env::current_dir()
        .unwrap_or_default()
        .join("Cargo.toml");
    if project_cargo.exists() {
        let dest = workspace_dir.join("Cargo.toml");
        tokio::fs::copy(&project_cargo, &dest).await.ok();
    }

    eprintln!("[harness] Workspace: {}", workspace_dir.display());

    // Build task list: all task categories
    let mut all_tasks = default_tasks();
    all_tasks.extend(agentic_tasks(&workspace_dir));
    all_tasks.extend(agent_quality_tasks(&workspace_dir));
    all_tasks.extend(python_tasks());
    all_tasks.extend(typescript_tasks());
    all_tasks.extend(go_tasks());
    all_tasks.extend(security_tasks(&workspace_dir));
    all_tasks.extend(test_generation_tasks());
    all_tasks.extend(advanced_rust_tasks());
    all_tasks.extend(multi_file_tasks(&workspace_dir));
    all_tasks = filter_tasks_by_suite(all_tasks, suite);

    eprintln!("[harness] {} tasks to run", all_tasks.len());

    // ─── Phase 1: Solo director runs ───
    eprintln!("\n[harness] === Phase 1: Solo director runs ===");
    let solo_director = run_solo_phase(&director_spec, &all_tasks, max_turns, &workspace_dir).await;

    // ─── Phase 2: Solo worker runs ───
    eprintln!("\n[harness] === Phase 2: Solo worker runs ===");
    let solo_worker = run_solo_phase(&worker_spec, &all_tasks, max_turns, &workspace_dir).await;

    // ─── Phase 3: Harnessed-pair runs ───
    eprintln!("\n[harness] === Phase 3: Harnessed-pair runs ===");
    let harness_results = run_harness_phase(
        &director_spec,
        &worker_spec,
        &all_tasks,
        max_turns,
        &workspace_dir,
    )
    .await;

    let config = HarnessConfig {
        director: ModelSpecSerde::from(&director_spec),
        worker: ModelSpecSerde::from(&worker_spec),
    };

    let report = HarnessReport {
        timestamp: chrono::Utc::now().to_rfc3339(),
        config,
        results: harness_results,
        solo_director_results: Some(solo_director),
        solo_worker_results: Some(solo_worker),
    };

    // Print comparison table
    print_harness_table(&report);

    // Save JSON results
    save_harness_json(&report).await?;

    Ok(report)
}

/// Run solo benchmark phase using single-agent AgentStack (same as regular benchmark).
async fn run_solo_phase(
    spec: &ModelSpec,
    tasks: &[BenchmarkTask],
    max_turns: usize,
    workspace_dir: &Path,
) -> Vec<BenchmarkResult> {
    let mut results = Vec::new();

    for (idx, task) in tasks.iter().enumerate() {
        eprintln!(
            "\n[solo {}/{}] task={} model={}",
            idx + 1,
            tasks.len(),
            task.name,
            short_model_name(&spec.model),
        );

        // Set up files for agentic tasks
        if let Some(ref harness) = task.test_harness {
            if let Some(setup_code) = harness.setup_code {
                if let Err(e) = setup_agentic_file(workspace_dir, task.name, setup_code).await {
                    eprintln!("  => ERROR setting up files: {}", e);
                    results.push(make_error_result(task, spec, &e.to_string()));
                    continue;
                }
            }
        }

        match run_solo_task(task, spec, max_turns, workspace_dir).await {
            Ok(r) => {
                let status = if r.quality_pass { "PASS" } else { "FAIL" };
                eprintln!(
                    "  => {}: {:.1}s, ${:.4}",
                    status,
                    r.total_time_ms as f64 / 1000.0,
                    r.cost_usd,
                );
                results.push(r);
            }
            Err(e) => {
                eprintln!("  => ERROR: {}", e);
                results.push(make_error_result(task, spec, &e.to_string()));
            }
        }
    }

    results
}

/// Run a single task in solo mode using AgentStack.
async fn run_solo_task(
    task: &BenchmarkTask,
    spec: &ModelSpec,
    max_turns: usize,
    workspace_dir: &Path,
) -> Result<BenchmarkResult> {
    use ava_agent::stack::{AgentStack, AgentStackConfig};

    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");
    let effective_turns = if task.needs_tools { max_turns } else { 3 };

    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir,
        provider: Some(spec.provider.clone()),
        model: Some(spec.model.clone()),
        max_turns: effective_turns,
        yolo: true,
        working_dir: Some(workspace_dir.to_path_buf()),
        ..Default::default()
    })
    .await?;

    let (tx, mut rx) = mpsc::unbounded_channel();
    let cancel = CancellationToken::new();

    // Timeout
    let timeout_cancel = cancel.clone();
    tokio::spawn(async move {
        tokio::time::sleep(tokio::time::Duration::from_secs(180)).await;
        timeout_cancel.cancel();
    });

    let goal = task.prompt.clone();
    let start = Instant::now();
    let handle = tokio::spawn(async move {
        stack
            .run(
                &goal,
                effective_turns,
                Some(tx),
                cancel,
                Vec::new(),
                None,
                Vec::new(),
            )
            .await
    });

    let mut total_output = String::new();
    let mut input_tokens: usize = 0;
    let mut output_tokens: usize = 0;
    let mut cost_usd: f64 = 0.0;
    let mut tool_calls_count: usize = 0;
    let mut tool_calls_detail: Vec<String> = Vec::new();
    let mut turns_used: usize = 0;
    let mut in_assistant_turn = false;

    while let Some(event) = rx.recv().await {
        match event {
            AgentEvent::Token(t) => {
                if !in_assistant_turn {
                    turns_used += 1;
                    in_assistant_turn = true;
                }
                total_output.push_str(&t);
            }
            AgentEvent::TokenUsage {
                input_tokens: it,
                output_tokens: ot,
                cost_usd: c,
            } => {
                input_tokens += it;
                output_tokens += ot;
                cost_usd += c;
                in_assistant_turn = false;
            }
            AgentEvent::ToolCall(tc) => {
                tool_calls_count += 1;
                tool_calls_detail.push(tc.name.clone());
            }
            AgentEvent::ToolResult(tr) => {
                total_output.push_str(&tr.content);
            }
            AgentEvent::Complete(_) => break,
            AgentEvent::Error(e) => return Err(eyre!("Agent error: {}", e)),
            _ => {}
        }
    }

    let total_time_ms = start.elapsed().as_millis() as u64;
    let _result = handle.await??;

    // Quality check
    let quality_pass = check_patterns(&total_output, &task.expected_patterns);

    // Compile & test validation
    let (compile_success, tests_passed, tests_total, compile_error) =
        if let Some(ref harness) = task.test_harness {
            if task.needs_tools {
                run_tier3_validation(workspace_dir, task.name, harness).await
            } else {
                run_tier2_validation(&total_output, harness).await
            }
        } else {
            (None, None, None, None)
        };

    let tokens_per_second = if total_time_ms > 0 {
        (output_tokens as f64) / (total_time_ms as f64 / 1000.0)
    } else {
        0.0
    };

    // Determine if the task passed
    let task_passed = compile_success.unwrap_or(quality_pass);
    let cost_per_task_usd = if task_passed { Some(cost_usd) } else { None };

    // tool_efficiency_score
    let tool_efficiency_score = if let Some(min) = task.expected_min_tools {
        if tool_calls_count > 0 {
            Some(min as f64 / tool_calls_count as f64)
        } else {
            None
        }
    } else {
        None
    };

    Ok(BenchmarkResult {
        task_name: task.name.to_string(),
        task_category: task.category.to_string(),
        provider: spec.provider.clone(),
        model: spec.model.clone(),
        ttft_ms: None,
        total_time_ms,
        input_tokens,
        output_tokens,
        tokens_per_second,
        cost_usd,
        quality_pass,
        quality_details: String::new(),
        error: None,
        compile_success,
        tests_passed,
        tests_total,
        compile_error,
        judge_scores: None,
        tool_calls_count,
        tool_calls_detail,
        turns_used,
        self_corrections: 0,
        raw_output: None,
        cost_per_task_usd,
        tool_efficiency_score,
        consistency_hash: None,
    })
}

/// Run the harnessed-pair phase: director delegates to workers via Praxis.
async fn run_harness_phase(
    director_spec: &ModelSpec,
    worker_spec: &ModelSpec,
    tasks: &[BenchmarkTask],
    max_turns: usize,
    workspace_dir: &Path,
) -> Vec<HarnessResult> {
    let mut results = Vec::new();

    for (idx, task) in tasks.iter().enumerate() {
        eprintln!(
            "\n[harness {}/{}] task={} ({}+{})",
            idx + 1,
            tasks.len(),
            task.name,
            short_model_name(&director_spec.model),
            short_model_name(&worker_spec.model),
        );

        // Set up files for agentic tasks
        if let Some(ref harness) = task.test_harness {
            if let Some(setup_code) = harness.setup_code {
                if let Err(e) = setup_agentic_file(workspace_dir, task.name, setup_code).await {
                    eprintln!("  => ERROR setting up files: {}", e);
                    results.push(HarnessResult {
                        task_name: task.name.to_string(),
                        task_category: task.category.to_string(),
                        director_model: director_spec.model.clone(),
                        worker_model: worker_spec.model.clone(),
                        total_time_ms: 0,
                        director_tokens: 0,
                        worker_tokens: 0,
                        director_cost: 0.0,
                        worker_cost: 0.0,
                        total_cost: 0.0,
                        compile_success: None,
                        tests_passed: None,
                        tests_total: None,
                        quality_pass: false,
                        worker_calls: 0,
                        total_turns: 0,
                        error: Some(e.to_string()),
                    });
                    continue;
                }
            }
        }

        match run_harness_task(task, director_spec, worker_spec, max_turns, workspace_dir).await {
            Ok(r) => {
                let status = if r.quality_pass { "PASS" } else { "FAIL" };
                eprintln!(
                    "  => {}: {:.1}s, dir=${:.4}, wrk=${:.4}, total=${:.4}, workers={}",
                    status,
                    r.total_time_ms as f64 / 1000.0,
                    r.director_cost,
                    r.worker_cost,
                    r.total_cost,
                    r.worker_calls,
                );
                results.push(r);
            }
            Err(e) => {
                eprintln!("  => ERROR: {}", e);
                results.push(HarnessResult {
                    task_name: task.name.to_string(),
                    task_category: task.category.to_string(),
                    director_model: director_spec.model.clone(),
                    worker_model: worker_spec.model.clone(),
                    total_time_ms: 0,
                    director_tokens: 0,
                    worker_tokens: 0,
                    director_cost: 0.0,
                    worker_cost: 0.0,
                    total_cost: 0.0,
                    compile_success: None,
                    tests_passed: None,
                    tests_total: None,
                    quality_pass: false,
                    worker_calls: 0,
                    total_turns: 0,
                    error: Some(e.to_string()),
                });
            }
        }
    }

    results
}

/// Run a single task with the harnessed-pair model via Praxis Director.
async fn run_harness_task(
    task: &BenchmarkTask,
    director_spec: &ModelSpec,
    worker_spec: &ModelSpec,
    max_turns: usize,
    workspace_dir: &Path,
) -> Result<HarnessResult> {
    let credentials = CredentialStore::load_default().await.unwrap_or_default();
    let pool = Arc::new(ConnectionPool::new());

    // Create providers
    let director_provider = create_provider(
        &director_spec.provider,
        &director_spec.model,
        &credentials,
        pool.clone(),
    )
    .map_err(|e| eyre!("Failed to create director provider: {}", e))?;

    let worker_provider = create_provider(
        &worker_spec.provider,
        &worker_spec.model,
        &credentials,
        pool.clone(),
    )
    .map_err(|e| eyre!("Failed to create worker provider: {}", e))?;

    let director_arc: Arc<dyn LLMProvider> = Arc::from(director_provider);
    let worker_arc: Arc<dyn LLMProvider> = Arc::from(worker_provider);

    // Set worker provider for ALL domains
    let mut domain_providers = HashMap::new();
    for domain in ALL_DOMAINS {
        domain_providers.insert(
            domain.clone(),
            Arc::new(SharedProvider::new(worker_arc.clone())) as Arc<dyn LLMProvider>,
        );
    }

    let platform = Arc::new(ava_platform::StandardPlatform);

    let effective_turns = if task.needs_tools { max_turns } else { 5 };

    let mut director = Director::new(DirectorConfig {
        budget: Budget::new(128_000, effective_turns, 5.0),
        default_provider: Arc::new(SharedProvider::new(director_arc.clone()))
            as Arc<dyn LLMProvider>,
        domain_providers,
        platform: Some(platform),
        scout_provider: None,
        board_providers: vec![],
        worker_names: vec![],
        enabled_leads: vec![],
        lead_prompts: std::collections::HashMap::new(),
    });

    // Determine task type based on the benchmark task
    let task_type = if task.needs_tools {
        TaskType::CodeGeneration
    } else {
        TaskType::Simple
    };

    let praxis_task = Task {
        description: task.prompt.clone(),
        task_type,
        files: vec![],
    };

    let worker = director
        .delegate(praxis_task)
        .map_err(|e| eyre!("Failed to delegate task: {}", e))?;

    let cancel = CancellationToken::new();

    // Timeout
    let timeout_cancel = cancel.clone();
    tokio::spawn(async move {
        tokio::time::sleep(tokio::time::Duration::from_secs(300)).await;
        timeout_cancel.cancel();
    });

    let (tx, mut rx) = mpsc::unbounded_channel();

    let start = Instant::now();

    let handle = tokio::spawn(async move { director.coordinate(vec![worker], cancel, tx).await });

    // Collect metrics from events
    let mut total_output = String::new();
    let mut worker_calls: usize = 0;
    let mut total_turns: usize = 0;

    while let Some(event) = rx.recv().await {
        match &event {
            PraxisEvent::WorkerStarted { .. } => {
                worker_calls += 1;
            }
            PraxisEvent::WorkerToken { token, .. } => {
                total_output.push_str(token);
            }
            PraxisEvent::WorkerCompleted { turns, .. } => {
                total_turns += turns;
            }
            PraxisEvent::AllComplete { .. } => break,
            PraxisEvent::WorkerFailed { error, .. } => {
                return Err(eyre!("Worker failed: {}", error));
            }
            _ => {}
        }
    }

    let total_time_ms = start.elapsed().as_millis() as u64;

    // Wait for handle
    let session = handle
        .await?
        .map_err(|e| eyre!("Director coordination failed: {}", e))?;

    // Estimate token usage and costs from the providers
    // The director cost is estimated from planning overhead, worker cost from execution
    let dir_provider_ref = director_arc.as_ref();
    let wrk_provider_ref = worker_arc.as_ref();

    // Rough estimation: director uses ~20% of total output for planning/review,
    // worker uses ~80% for actual code generation. Input tokens are estimated from prompt.
    let total_chars = total_output.len();
    let dir_slice_end = if total_chars == 0 {
        0
    } else {
        (total_chars / 5).max(1)
    };
    let est_director_output = dir_provider_ref.estimate_tokens(&total_output[..dir_slice_end]);
    let est_worker_output = wrk_provider_ref.estimate_tokens(&total_output);
    let est_input = dir_provider_ref.estimate_tokens(&task.prompt);

    let director_cost = dir_provider_ref.estimate_cost(est_input, est_director_output);
    let worker_cost = wrk_provider_ref.estimate_cost(est_input, est_worker_output);
    let total_cost = director_cost + worker_cost;

    // Quality check
    let quality_pass = check_patterns(&total_output, &task.expected_patterns);

    // Also check the session messages for quality patterns
    let session_text: String = session
        .messages
        .iter()
        .map(|m| m.content.as_str())
        .collect::<Vec<_>>()
        .join("\n");
    let quality_pass = quality_pass || check_patterns(&session_text, &task.expected_patterns);

    // Compile & test validation
    let (compile_success, tests_passed, tests_total, _compile_error) =
        if let Some(ref harness) = task.test_harness {
            if task.needs_tools {
                run_tier3_validation(workspace_dir, task.name, harness).await
            } else {
                run_tier2_validation(&total_output, harness).await
            }
        } else {
            (None, None, None, None)
        };

    Ok(HarnessResult {
        task_name: task.name.to_string(),
        task_category: task.category.to_string(),
        director_model: director_spec.model.clone(),
        worker_model: worker_spec.model.clone(),
        total_time_ms,
        director_tokens: est_director_output,
        worker_tokens: est_worker_output,
        director_cost,
        worker_cost,
        total_cost,
        compile_success,
        tests_passed,
        tests_total,
        quality_pass,
        worker_calls,
        total_turns,
        error: None,
    })
}

// ─── Shared validation helpers ───

/// Check if output matches expected regex patterns.
fn check_patterns(output: &str, patterns: &[&str]) -> bool {
    if output.trim().is_empty() {
        return false;
    }
    patterns.iter().all(|pat| {
        regex::Regex::new(pat)
            .map(|re| re.is_match(output))
            .unwrap_or(true)
    })
}

/// Extract Rust code from model output.
fn extract_rust_code(output: &str) -> Option<String> {
    let re_rust = regex::Regex::new(r"(?s)```rust\s*\n(.*?)```").ok()?;
    if let Some(cap) = re_rust.captures(output) {
        return Some(cap[1].to_string());
    }
    let re_generic = regex::Regex::new(r"(?s)```\s*\n(.*?)```").ok()?;
    if let Some(cap) = re_generic.captures(output) {
        return Some(cap[1].to_string());
    }
    let re_fn = regex::Regex::new(r"(?s)((?:use\s+.*?;\s*)*(?:pub\s+)?fn\s+\w+.*?\n\})").ok()?;
    if let Some(cap) = re_fn.captures(output) {
        return Some(cap[1].to_string());
    }
    if output.contains("fn ") {
        return Some(output.to_string());
    }
    None
}

/// Tier 2: Extract code, compile + test.
async fn run_tier2_validation(
    model_output: &str,
    harness: &crate::benchmark_tasks::TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let Some(code) = extract_rust_code(model_output) else {
        return (
            Some(false),
            None,
            None,
            Some("Could not extract Rust code from output".to_string()),
        );
    };

    let mut full_source = String::new();
    if harness.test_code.contains("HashMap") && !code.contains("use std::collections::HashMap") {
        full_source.push_str("use std::collections::HashMap;\n");
    }
    if harness.test_code.contains("Hash") && !code.contains("use std::hash::Hash") {
        full_source.push_str("use std::hash::Hash;\n");
    }
    full_source.push_str(&code);
    full_source.push_str("\n\nfn main() {}\n");
    full_source.push_str(harness.test_code);

    compile_and_test(&full_source, harness.test_count).await
}

/// Tier 3: Read agent-edited file, append tests, compile + test.
async fn run_tier3_validation(
    temp_dir: &Path,
    task_name: &str,
    harness: &crate::benchmark_tasks::TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let filename = match task_name {
        "bugfix_off_by_one" => "binary_search.rs",
        "bugfix_lifetime" => "lifetime_fix.rs",
        "refactor_extract" => "refactor.rs",
        "multi_step_debug" => "multi_step_debug/lib.rs",
        "constraint_edit" => "validators.rs",
        "self_correct_compile" => "cache.rs",
        "tool_efficiency" => "tool_efficiency/src/config.rs",
        "no_overengineer" => "math.rs",
        "error_recovery_loop" => "broken.rs",
        _ => return (None, None, None, None),
    };

    let file_path = temp_dir.join(filename);
    let file_content = match tokio::fs::read_to_string(&file_path).await {
        Ok(content) => content,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to read edited file: {}", e)),
            );
        }
    };

    let full_source = format!("{}\n{}", file_content, harness.test_code);
    compile_and_test(&full_source, harness.test_count).await
}

/// Compile and test a Rust source file.
async fn compile_and_test(
    source: &str,
    expected_test_count: usize,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let temp_dir = match tempfile::tempdir() {
        Ok(d) => d,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to create temp dir: {}", e)),
            );
        }
    };

    let source_path = temp_dir.path().join("bench_test.rs");
    let test_binary = temp_dir.path().join("bench_test");

    if let Err(e) = tokio::fs::write(&source_path, source).await {
        return (
            Some(false),
            None,
            None,
            Some(format!("Failed to write source: {}", e)),
        );
    }

    let compile_output = tokio::process::Command::new("rustc")
        .args([
            "--edition",
            "2021",
            "--test",
            source_path.to_str().unwrap_or("bench_test.rs"),
            "-o",
            test_binary.to_str().unwrap_or("bench_test"),
        ])
        .output()
        .await;

    let compile_result = match compile_output {
        Ok(output) => output,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to run rustc: {}", e)),
            );
        }
    };

    if !compile_result.status.success() {
        let stderr = String::from_utf8_lossy(&compile_result.stderr);
        let error_msg = if stderr.len() > 500 {
            format!("{}...", &stderr[..500])
        } else {
            stderr.to_string()
        };
        return (
            Some(false),
            Some(0),
            Some(expected_test_count),
            Some(error_msg),
        );
    }

    let test_output = tokio::process::Command::new(test_binary.to_str().unwrap_or("./bench_test"))
        .output()
        .await;

    let test_result = match test_output {
        Ok(output) => output,
        Err(e) => {
            return (
                Some(true),
                Some(0),
                Some(expected_test_count),
                Some(format!("Failed to run tests: {}", e)),
            );
        }
    };

    let stdout = String::from_utf8_lossy(&test_result.stdout);
    let (passed, failed) = parse_test_output(&stdout);
    let tests_passed = passed;
    let tests_total = passed + failed;
    let total = if tests_total == 0 {
        expected_test_count
    } else {
        tests_total
    };

    if test_result.status.success() {
        (
            Some(true),
            Some(if tests_passed > 0 {
                tests_passed
            } else {
                total
            }),
            Some(total),
            None,
        )
    } else {
        let stderr = String::from_utf8_lossy(&test_result.stderr);
        let error_msg = if stderr.len() > 500 {
            format!("{}...", &stderr[..500])
        } else if stderr.is_empty() {
            stdout.to_string()
        } else {
            stderr.to_string()
        };
        (Some(true), Some(tests_passed), Some(total), Some(error_msg))
    }
}

/// Parse "test result: ok. N passed; M failed;" from rustc test output.
fn parse_test_output(output: &str) -> (usize, usize) {
    let re = regex::Regex::new(r"test result:.*?(\d+) passed.*?(\d+) failed").ok();
    if let Some(re) = re {
        if let Some(cap) = re.captures(output) {
            let passed = cap[1].parse().unwrap_or(0);
            let failed = cap[2].parse().unwrap_or(0);
            return (passed, failed);
        }
    }
    (0, 0)
}

/// Set up agentic task files in workspace (mirrors benchmark.rs setup_agentic_file).
async fn setup_agentic_file(temp_dir: &Path, task_name: &str, setup_code: &str) -> Result<()> {
    match task_name {
        "bugfix_off_by_one" => {
            let path = temp_dir.join("binary_search.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", path.display(), e))?;
        }
        "bugfix_lifetime" => {
            let path = temp_dir.join("lifetime_fix.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", path.display(), e))?;
        }
        "refactor_extract" => {
            let path = temp_dir.join("refactor.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", path.display(), e))?;
        }
        "multi_step_debug" => {
            let dir = temp_dir.join("multi_step_debug");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            let lib_path = dir.join("lib.rs");
            tokio::fs::write(&lib_path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", lib_path.display(), e))?;
            let tests_path = dir.join("tests.rs");
            let test_content = "mod lib;\n\n#[test]\nfn test_area() {\n    assert!((lib::area(3.0, 4.0) - 12.0).abs() < 1e-9);\n}\n\n#[test]\nfn test_perimeter() {\n    assert!((lib::perimeter(3.0, 4.0) - 14.0).abs() < 1e-9);\n}\n\n#[test]\nfn test_diagonal() {\n    assert!((lib::diagonal(3.0, 4.0) - 5.0).abs() < 1e-9);\n}\n";
            tokio::fs::write(&tests_path, test_content)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", tests_path.display(), e))?;
        }
        "constraint_edit" => {
            let path = temp_dir.join("validators.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", path.display(), e))?;
        }
        "self_correct_compile" => {
            let path = temp_dir.join("cache.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", path.display(), e))?;
        }
        "tool_efficiency" => {
            let src_dir = temp_dir.join("tool_efficiency").join("src");
            tokio::fs::create_dir_all(&src_dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", src_dir.display(), e))?;
            let main_code = "mod lib;\n\nfn main() {\n    let cfg = lib::config::Config::default();\n    let msg = lib::utils::greet(&cfg.name);\n    println!(\"{}\", msg);\n}\n";
            let lib_code = "pub mod utils;\npub mod config;\n";
            let utils_code = "/// Greets a user by name.\npub fn greet(name: &str) -> String {\n    format!(\"Hello, {}!\", name)\n}\n\n/// Formats a duration in seconds into a human-readable string.\npub fn format_duration(seconds: u64) -> String {\n    if seconds < 60 {\n        format!(\"{}s\", seconds)\n    } else if seconds < 3600 {\n        format!(\"{}m {}s\", seconds / 60, seconds % 60)\n    } else {\n        format!(\"{}h {}m\", seconds / 3600, (seconds % 3600) / 60)\n    }\n}\n";
            tokio::fs::write(src_dir.join("main.rs"), main_code)
                .await
                .map_err(|e| eyre!("Failed to write main.rs: {}", e))?;
            tokio::fs::write(src_dir.join("lib.rs"), lib_code)
                .await
                .map_err(|e| eyre!("Failed to write lib.rs: {}", e))?;
            tokio::fs::write(src_dir.join("utils.rs"), utils_code)
                .await
                .map_err(|e| eyre!("Failed to write utils.rs: {}", e))?;
            tokio::fs::write(src_dir.join("config.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write config.rs: {}", e))?;
        }
        "no_overengineer" => {
            let path = temp_dir.join("math.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", path.display(), e))?;
        }
        "error_recovery_loop" => {
            let path = temp_dir.join("broken.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", path.display(), e))?;
        }
        _ => {}
    }
    Ok(())
}

/// Create an error result for a solo run.
fn make_error_result(task: &BenchmarkTask, spec: &ModelSpec, error: &str) -> BenchmarkResult {
    BenchmarkResult {
        task_name: task.name.to_string(),
        task_category: task.category.to_string(),
        provider: spec.provider.clone(),
        model: spec.model.clone(),
        ttft_ms: None,
        total_time_ms: 0,
        input_tokens: 0,
        output_tokens: 0,
        tokens_per_second: 0.0,
        cost_usd: 0.0,
        quality_pass: false,
        quality_details: "error".to_string(),
        error: Some(error.to_string()),
        compile_success: None,
        tests_passed: None,
        tests_total: None,
        compile_error: None,
        judge_scores: None,
        tool_calls_count: 0,
        tool_calls_detail: Vec::new(),
        turns_used: 0,
        self_corrections: 0,
        raw_output: None,
        cost_per_task_usd: None,
        tool_efficiency_score: None,
        consistency_hash: None,
    }
}

// ─── Output formatting ───

/// Print the harnessed-pair comparison table.
fn print_harness_table(report: &HarnessReport) {
    let dir_name = short_model_name(&report.config.director.model);
    let wrk_name = short_model_name(&report.config.worker.model);

    println!();
    println!("=======================================================================");
    println!("           AVA Harnessed-Pair Benchmark Results");
    println!("           Director: {} | Worker: {}", dir_name, wrk_name);
    println!("           {}", &report.timestamp[..19]);
    println!("=======================================================================");

    // Group by task
    let mut tasks_seen: Vec<String> = Vec::new();
    for r in &report.results {
        if !tasks_seen.contains(&r.task_name) {
            tasks_seen.push(r.task_name.clone());
        }
    }

    for task_name in &tasks_seen {
        let harness_r = report.results.iter().find(|r| &r.task_name == task_name);
        let solo_dir = report
            .solo_director_results
            .as_ref()
            .and_then(|v| v.iter().find(|r| r.task_name == *task_name));
        let solo_wrk = report
            .solo_worker_results
            .as_ref()
            .and_then(|v| v.iter().find(|r| r.task_name == *task_name));

        let category = harness_r.map(|r| r.task_category.as_str()).unwrap_or("?");

        println!();
        println!("  Task: {} [{}]", task_name, category);
        println!(
            "  {:<20} {:>9} {:>10} {:>10} {:>8} {:>8} {:>7} {:>7}",
            "Mode", "Total(s)", "Dir Cost", "Wrk Cost", "Total$", "Compile", "Tests", "Quality"
        );
        println!(
            "  {:-<20} {:-<9} {:-<10} {:-<10} {:-<8} {:-<8} {:-<7} {:-<7}",
            "", "", "", "", "", "", "", ""
        );

        // Director solo row
        if let Some(r) = solo_dir {
            let compile_str = match r.compile_success {
                Some(true) => "PASS",
                Some(false) => "FAIL",
                None => "-",
            };
            let tests_str = match (r.tests_passed, r.tests_total) {
                (Some(p), Some(t)) => format!("{}/{}", p, t),
                _ => "-".to_string(),
            };
            let quality_str = if r.error.is_some() {
                "ERROR"
            } else if r.quality_pass {
                "PASS"
            } else {
                "FAIL"
            };
            let label = format!("{} solo", dir_name);
            let label = if label.len() > 20 {
                format!("{}...", &label[..17])
            } else {
                label
            };
            println!(
                "  {:<20} {:>8.1}s {:>10} {:>10} {:>7} {:>8} {:>7} {:>7}",
                label,
                r.total_time_ms as f64 / 1000.0,
                format!("${:.4}", r.cost_usd),
                "-",
                format!("${:.4}", r.cost_usd),
                compile_str,
                tests_str,
                quality_str,
            );
        }

        // Worker solo row
        if let Some(r) = solo_wrk {
            let compile_str = match r.compile_success {
                Some(true) => "PASS",
                Some(false) => "FAIL",
                None => "-",
            };
            let tests_str = match (r.tests_passed, r.tests_total) {
                (Some(p), Some(t)) => format!("{}/{}", p, t),
                _ => "-".to_string(),
            };
            let quality_str = if r.error.is_some() {
                "ERROR"
            } else if r.quality_pass {
                "PASS"
            } else {
                "FAIL"
            };
            let label = format!("{} solo", wrk_name);
            let label = if label.len() > 20 {
                format!("{}...", &label[..17])
            } else {
                label
            };
            println!(
                "  {:<20} {:>8.1}s {:>10} {:>10} {:>7} {:>8} {:>7} {:>7}",
                label,
                r.total_time_ms as f64 / 1000.0,
                "-",
                format!("${:.4}", r.cost_usd),
                format!("${:.4}", r.cost_usd),
                compile_str,
                tests_str,
                quality_str,
            );
        }

        // Harnessed pair row
        if let Some(r) = harness_r {
            let compile_str = match r.compile_success {
                Some(true) => "PASS",
                Some(false) => "FAIL",
                None => "-",
            };
            let tests_str = match (r.tests_passed, r.tests_total) {
                (Some(p), Some(t)) => format!("{}/{}", p, t),
                _ => "-".to_string(),
            };
            let quality_str = if r.error.is_some() {
                "ERROR"
            } else if r.quality_pass {
                "PASS"
            } else {
                "FAIL"
            };
            let label = format!("{}+{}", dir_name, wrk_name);
            let label = if label.len() > 20 {
                format!("{}...", &label[..17])
            } else {
                label
            };
            println!(
                "  {:<20} {:>8.1}s {:>10} {:>10} {:>7} {:>8} {:>7} {:>7}",
                label,
                r.total_time_ms as f64 / 1000.0,
                format!("${:.4}", r.director_cost),
                format!("${:.4}", r.worker_cost),
                format!("${:.4}", r.total_cost),
                compile_str,
                tests_str,
                quality_str,
            );

            // Savings comparison vs director solo
            if let Some(dir_r) = solo_dir {
                if dir_r.cost_usd > 0.0 && r.total_cost > 0.0 {
                    let cost_savings =
                        ((dir_r.cost_usd - r.total_cost) / dir_r.cost_usd * 100.0).max(0.0);
                    let time_savings = if dir_r.total_time_ms > 0 {
                        ((dir_r.total_time_ms as f64 - r.total_time_ms as f64)
                            / dir_r.total_time_ms as f64
                            * 100.0)
                            .max(0.0)
                    } else {
                        0.0
                    };
                    if cost_savings > 0.0 || time_savings > 0.0 {
                        println!(
                            "  Savings vs {} solo: {:.0}% cost, {:.0}% faster",
                            dir_name, cost_savings, time_savings,
                        );
                    }
                }
            }
        }
    }

    // Summary
    println!();
    println!("-----------------------------------------------------------------------");

    let harness_total_cost: f64 = report.results.iter().map(|r| r.total_cost).sum();
    let harness_total_time: f64 = report
        .results
        .iter()
        .map(|r| r.total_time_ms as f64 / 1000.0)
        .sum();
    let harness_pass = report.results.iter().filter(|r| r.quality_pass).count();

    let dir_total_cost: f64 = report
        .solo_director_results
        .as_ref()
        .map(|v| v.iter().map(|r| r.cost_usd).sum())
        .unwrap_or(0.0);
    let wrk_total_cost: f64 = report
        .solo_worker_results
        .as_ref()
        .map(|v| v.iter().map(|r| r.cost_usd).sum())
        .unwrap_or(0.0);

    println!(
        "  Harness: {} tasks, {}/{} passed, {:.1}s, ${:.4}",
        report.results.len(),
        harness_pass,
        report.results.len(),
        harness_total_time,
        harness_total_cost,
    );
    println!(
        "  {} solo total: ${:.4} | {} solo total: ${:.4}",
        dir_name, dir_total_cost, wrk_name, wrk_total_cost,
    );

    if dir_total_cost > 0.0 {
        let overall_savings =
            ((dir_total_cost - harness_total_cost) / dir_total_cost * 100.0).max(0.0);
        println!(
            "  Overall cost savings vs {} solo: {:.0}%",
            dir_name, overall_savings,
        );
    }

    println!("=======================================================================");
    println!();
}

/// Save harness results as JSON.
async fn save_harness_json(report: &HarnessReport) -> Result<()> {
    let benchmarks_dir = dirs::home_dir()
        .unwrap_or_default()
        .join(".ava")
        .join("benchmarks");

    tokio::fs::create_dir_all(&benchmarks_dir)
        .await
        .map_err(|e| eyre!("Failed to create benchmarks dir: {}", e))?;

    let filename = format!(
        "harness-{}.json",
        &report.timestamp.replace(':', "-").replace('T', "_")[..19]
    );
    let path = benchmarks_dir.join(&filename);

    let json = serde_json::to_string_pretty(report)
        .map_err(|e| eyre!("Failed to serialize harness results: {}", e))?;

    tokio::fs::write(&path, &json)
        .await
        .map_err(|e| eyre!("Failed to write harness results: {}", e))?;

    eprintln!("[harness] Results saved to {}", path.display());

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_single_model_spec() {
        let spec = parse_single_model_spec("openrouter:anthropic/claude-opus-4.6").unwrap();
        assert_eq!(spec.provider, "openrouter");
        assert_eq!(spec.model, "anthropic/claude-opus-4.6");
    }

    #[test]
    fn test_parse_single_model_spec_invalid() {
        let result = parse_single_model_spec("no-colon");
        assert!(result.is_err());
    }

    #[test]
    fn test_short_model_name() {
        assert_eq!(
            short_model_name("anthropic/claude-opus-4.6"),
            "claude-opus-4.6"
        );
        assert_eq!(short_model_name("mercury-2"), "mercury-2");
    }

    #[test]
    fn test_check_patterns() {
        assert!(check_patterns(
            "fn is_palindrome() -> bool",
            &[r"fn\s+is_palindrome", r"-> bool"]
        ));
        assert!(!check_patterns("fn other()", &[r"fn\s+is_palindrome"]));
        assert!(!check_patterns("", &[r"anything"]));
    }
}
