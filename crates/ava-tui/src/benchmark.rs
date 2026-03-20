//! Model benchmarking system for AVA.
//!
//! Runs a set of coding tasks against multiple models, captures timing and quality
//! metrics, and outputs a formatted comparison table plus JSON results.
//!
//! ## Tiers
//! - **Tier 1**: Regex pattern matching (fast pre-check)
//! - **Tier 2**: Compile & test validation for code generation tasks
//! - **Tier 3**: Agentic editing tasks with setup code and post-edit verification
//! - **LLM-as-Judge**: Optional SOTA model evaluation of outputs
//!
//! ## Recommended Judges
//!
//! Use the top reasoning models for best evaluation quality:
//! - `openrouter:anthropic/claude-opus-4.6` (extended thinking, highest quality)
//! - `openrouter:openai/gpt-5.4` (reasoning_effort: high)
//! - `openrouter:google/gemini-3.1-pro-preview` (reasoning_effort: high)
//!
//! Judges automatically use `ThinkingLevel::High` for deeper analysis.

use std::collections::hash_map::DefaultHasher;
use std::collections::HashMap;
use std::hash::{Hash, Hasher};
use std::path::Path;
use std::sync::{Arc, LazyLock};
use std::time::Instant;

use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use ava_config::CredentialStore;
use ava_llm::pool::ConnectionPool;
use ava_llm::providers::create_provider;
use ava_types::{Message, Role, ThinkingLevel};
use color_eyre::eyre::{eyre, Result};
use regex::Regex;
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

use crate::benchmark_tasks::{
    advanced_rust_tasks, agent_quality_tasks, agentic_tasks, default_tasks, filter_tasks_by_suite,
    go_tasks, multi_file_tasks, python_tasks, security_tasks, test_generation_tasks,
    typescript_tasks, BenchmarkSuite, BenchmarkTask, Language, TestHarness,
};

/// A provider:model pair to benchmark.
#[derive(Debug, Clone)]
pub struct ModelSpec {
    pub provider: String,
    pub model: String,
}

impl std::fmt::Display for ModelSpec {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}:{}", self.provider, self.model)
    }
}

/// Short display name for a model (last segment of model path).
fn short_model_name(model: &str) -> String {
    model.rsplit('/').next().unwrap_or(model).to_string()
}

/// Scores from a single LLM judge evaluation.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JudgeEvaluation {
    pub judge_model: String,
    pub correctness: f64,
    pub code_quality: f64,
    pub efficiency: f64,
    pub idiomatic: f64,
    pub notes: String,
}

/// Aggregated judge scores across all judges.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JudgeScores {
    pub correctness: f64,
    pub code_quality: f64,
    pub efficiency: f64,
    pub idiomatic: f64,
    pub average: f64,
    pub evaluations: Vec<JudgeEvaluation>,
}

/// Result of a single task x model benchmark run.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BenchmarkResult {
    pub task_name: String,
    pub task_category: String,
    pub provider: String,
    pub model: String,
    pub ttft_ms: Option<u64>,
    pub total_time_ms: u64,
    pub input_tokens: usize,
    pub output_tokens: usize,
    pub tokens_per_second: f64,
    pub cost_usd: f64,
    pub quality_pass: bool,
    pub quality_details: String,
    pub error: Option<String>,
    // Tier 2: compile & test fields
    pub compile_success: Option<bool>,
    pub tests_passed: Option<usize>,
    pub tests_total: Option<usize>,
    pub compile_error: Option<String>,
    // LLM-as-Judge fields
    pub judge_scores: Option<JudgeScores>,
    // Agent-quality metrics
    /// Total number of tool calls made during the task.
    pub tool_calls_count: usize,
    /// List of tool names called (e.g. ["read", "edit", "bash"]).
    pub tool_calls_detail: Vec<String>,
    /// Number of agent turns consumed (each assistant response = 1 turn).
    pub turns_used: usize,
    /// Number of times the model retried after a tool error (self-corrections).
    pub self_corrections: usize,
    // Raw model output for judge evaluation
    #[serde(skip_serializing_if = "Option::is_none")]
    pub raw_output: Option<String>,
    /// Cost per resolved task (None if task failed). Equals `cost_usd` for passing tasks.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub cost_per_task_usd: Option<f64>,
    /// Ratio of minimum expected tools to actual tools used (1.0 = perfect, lower = wasteful).
    /// Only populated for tool-using tasks with `expected_min_tools` set.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tool_efficiency_score: Option<f64>,
    /// Hash of the code output for variance tracking across runs.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub consistency_hash: Option<String>,
}

/// Full benchmark suite results.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BenchmarkReport {
    pub timestamp: String,
    pub results: Vec<BenchmarkResult>,
    /// Total cost / number of resolved (passed) tasks.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub aggregate_cost_per_resolved: Option<f64>,
    /// Mean tool efficiency across all tool-using tasks with efficiency scores.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub aggregate_tool_efficiency: Option<f64>,
}

/// Parse a `--models` string like "openrouter:model1,openrouter:model2"
/// or use --provider + comma-separated --model.
pub fn parse_model_specs(
    provider: Option<&str>,
    model: Option<&str>,
    models_arg: Option<&str>,
) -> Result<Vec<ModelSpec>> {
    let mut specs = Vec::new();

    // Parse --models "provider:model,provider:model" format
    if let Some(models_str) = models_arg {
        for entry in models_str.split(',') {
            let entry = entry.trim();
            if entry.is_empty() {
                continue;
            }
            let (prov, mdl) = entry.split_once(':').ok_or_else(|| {
                eyre!(
                    "Invalid model spec '{}'. Expected format: provider:model",
                    entry
                )
            })?;
            specs.push(ModelSpec {
                provider: prov.to_string(),
                model: mdl.to_string(),
            });
        }
    }

    // Parse --provider + --model (comma-separated models)
    if let (Some(prov), Some(mdl)) = (provider, model) {
        for m in mdl.split(',') {
            let m = m.trim();
            if !m.is_empty() {
                specs.push(ModelSpec {
                    provider: prov.to_string(),
                    model: m.to_string(),
                });
            }
        }
    }

    if specs.is_empty() {
        return Err(eyre!(
            "No models specified for benchmark. Use --provider + --model or --models.\n\
             Examples:\n  \
             ava --benchmark --provider openrouter --model anthropic/claude-haiku-4.5,inception/mercury-coder-small\n  \
             ava --benchmark --models \"openrouter:anthropic/claude-haiku-4.5,openrouter:inception/mercury-coder-small\""
        ));
    }

    Ok(specs)
}

/// Parse `--judges` string like "openrouter:anthropic/claude-opus-4.6,openrouter:openai/gpt-5.4,openrouter:google/gemini-3.1-pro-preview"
pub fn parse_judge_specs(judges_arg: Option<&str>) -> Result<Vec<ModelSpec>> {
    let Some(judges_str) = judges_arg else {
        return Ok(Vec::new());
    };

    let mut specs = Vec::new();
    for entry in judges_str.split(',') {
        let entry = entry.trim();
        if entry.is_empty() {
            continue;
        }
        let (prov, mdl) = entry.split_once(':').ok_or_else(|| {
            eyre!(
                "Invalid judge spec '{}'. Expected format: provider:model",
                entry
            )
        })?;
        specs.push(ModelSpec {
            provider: prov.to_string(),
            model: mdl.to_string(),
        });
    }

    Ok(specs)
}

/// Run the full benchmark suite.
///
/// `imported_tasks` contains externally imported tasks (e.g., from Aider Polyglot).
/// They are appended to the built-in task list before suite filtering.
pub async fn run_benchmark(
    specs: Vec<ModelSpec>,
    tasks: Option<Vec<BenchmarkTask>>,
    max_turns: usize,
    judge_specs: Vec<ModelSpec>,
    suite: BenchmarkSuite,
    imported_tasks: Vec<BenchmarkTask>,
    language_filter: Option<Vec<Language>>,
) -> Result<BenchmarkReport> {
    let max_turns = if max_turns == 0 { 10 } else { max_turns };

    eprintln!("[benchmark] Suite: {}", suite);

    // Use a stable workspace directory for benchmark runs, isolated from the project.
    // This prevents benchmark tool calls (read, edit, bash) from touching the real codebase.
    let workspace_dir = dirs::home_dir()
        .unwrap_or_default()
        .join(".ava")
        .join("benchmarks")
        .join("workspace");
    tokio::fs::create_dir_all(&workspace_dir)
        .await
        .map_err(|e| eyre!("Failed to create benchmark workspace: {}", e))?;

    // Copy Cargo.toml into the workspace so the `read_cargo` task can read it.
    let project_cargo = std::env::current_dir()
        .unwrap_or_default()
        .join("Cargo.toml");
    if project_cargo.exists() {
        let dest = workspace_dir.join("Cargo.toml");
        tokio::fs::copy(&project_cargo, &dest)
            .await
            .map_err(|e| eyre!("Failed to copy Cargo.toml to workspace: {}", e))?;
    }

    eprintln!("[benchmark] Workspace: {}", workspace_dir.display());

    // Build task list: all task categories
    let mut all_tasks = tasks.unwrap_or_else(default_tasks);
    all_tasks.extend(agentic_tasks(&workspace_dir));
    all_tasks.extend(agent_quality_tasks(&workspace_dir));
    all_tasks.extend(python_tasks());
    all_tasks.extend(typescript_tasks());
    all_tasks.extend(go_tasks());
    all_tasks.extend(security_tasks(&workspace_dir));
    all_tasks.extend(test_generation_tasks());
    all_tasks.extend(advanced_rust_tasks());
    all_tasks.extend(multi_file_tasks(&workspace_dir));

    // Append externally imported tasks (e.g., Aider Polyglot)
    if !imported_tasks.is_empty() {
        eprintln!("[benchmark] Adding {} imported tasks", imported_tasks.len());
        all_tasks.extend(imported_tasks);
    }

    // Filter by suite
    all_tasks = filter_tasks_by_suite(all_tasks, suite);

    // Filter by language
    if let Some(ref langs) = language_filter {
        all_tasks.retain(|t| langs.contains(&t.language()));
        let lang_names: Vec<_> = langs.iter().map(|l| l.to_string()).collect();
        eprintln!("[benchmark] Language filter: {}", lang_names.join(", "));
    }

    let mut results = Vec::new();
    let total_runs = all_tasks.len() * specs.len();
    let mut run_idx = 0;

    for task in &all_tasks {
        for spec in &specs {
            run_idx += 1;
            eprintln!(
                "\n[benchmark {}/{}] task={} model={}",
                run_idx,
                total_runs,
                task.name,
                short_model_name(&spec.model),
            );

            // Set up Tier 3 files if needed
            if let Some(ref harness) = task.test_harness {
                if let Some(setup_code) = harness.setup_code {
                    setup_agentic_file(&workspace_dir, task.name, setup_code).await?;
                }
            }

            let result = run_single_task(task, spec, max_turns, &workspace_dir).await;
            match result {
                Ok(r) => {
                    let status = if r.quality_pass { "PASS" } else { "FAIL" };
                    let compile_info = match (r.compile_success, r.tests_passed, r.tests_total) {
                        (Some(true), Some(passed), Some(total)) => {
                            format!(", compile=OK, tests={}/{}", passed, total)
                        }
                        (Some(false), _, _) => ", compile=FAIL".to_string(),
                        _ => String::new(),
                    };
                    let agent_info = if r.tool_calls_count > 0 {
                        format!(
                            ", tools={}, turns={}, corrections={}",
                            r.tool_calls_count, r.turns_used, r.self_corrections,
                        )
                    } else {
                        String::new()
                    };
                    eprintln!(
                        "  => {}: {:.1}s, {} tok/s, ${:.4}, {}{}{}",
                        status,
                        r.total_time_ms as f64 / 1000.0,
                        r.tokens_per_second as u64,
                        r.cost_usd,
                        r.quality_details,
                        compile_info,
                        agent_info,
                    );
                    results.push(r);
                }
                Err(e) => {
                    eprintln!("  => ERROR: {}", e);
                    results.push(BenchmarkResult {
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
                        error: Some(e.to_string()),
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
                    });
                }
            }
        }
    }

    // LLM-as-Judge evaluation
    if !judge_specs.is_empty() {
        eprintln!(
            "\n[benchmark] Running LLM-as-Judge evaluation with {} judge(s)...",
            judge_specs.len()
        );
        judge_outputs(&mut results, &judge_specs, &all_tasks).await;
    }

    // Compute aggregate metrics
    let total_cost: f64 = results.iter().map(|r| r.cost_usd).sum();
    let resolved_count = results
        .iter()
        .filter(|r| r.compile_success.unwrap_or(r.quality_pass))
        .count();
    let aggregate_cost_per_resolved = if resolved_count > 0 {
        Some(total_cost / resolved_count as f64)
    } else {
        None
    };

    let efficiency_scores: Vec<f64> = results
        .iter()
        .filter_map(|r| r.tool_efficiency_score)
        .collect();
    let aggregate_tool_efficiency = if !efficiency_scores.is_empty() {
        Some(efficiency_scores.iter().sum::<f64>() / efficiency_scores.len() as f64)
    } else {
        None
    };

    let report = BenchmarkReport {
        timestamp: chrono::Utc::now().to_rfc3339(),
        results,
        aggregate_cost_per_resolved,
        aggregate_tool_efficiency,
    };

    // Print formatted table
    print_results_table(&report, suite);

    // Save JSON results
    save_results_json(&report).await?;

    Ok(report)
}

/// Write a setup file for a Tier 3 agentic task or agent quality task.
async fn setup_agentic_file(temp_dir: &Path, task_name: &str, setup_code: &str) -> Result<()> {
    match task_name {
        "bugfix_off_by_one" => {
            let path = temp_dir.join("binary_search.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write setup file {}: {}", path.display(), e))?;
        }
        "bugfix_lifetime" => {
            let path = temp_dir.join("lifetime_fix.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write setup file {}: {}", path.display(), e))?;
        }
        "refactor_extract" => {
            let path = temp_dir.join("refactor.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write setup file {}: {}", path.display(), e))?;
        }
        "multi_step_debug" => {
            // Create directory structure: multi_step_debug/lib.rs + multi_step_debug/tests.rs
            let dir = temp_dir.join("multi_step_debug");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            let lib_path = dir.join("lib.rs");
            tokio::fs::write(&lib_path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", lib_path.display(), e))?;
            // Write the test file that references lib as a module
            let tests_path = dir.join("tests.rs");
            let test_content = r#"
mod lib;

#[test]
fn test_area() {
    assert!((lib::area(3.0, 4.0) - 12.0).abs() < 1e-9);
}

#[test]
fn test_perimeter() {
    assert!((lib::perimeter(3.0, 4.0) - 14.0).abs() < 1e-9);
}

#[test]
fn test_diagonal() {
    assert!((lib::diagonal(3.0, 4.0) - 5.0).abs() < 1e-9);
}
"#;
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
            // Create project structure: tool_efficiency/src/{main,lib,utils,config}.rs
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
            // setup_code is TOOL_EFFICIENCY_CONFIG for the config.rs file
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
        _ => return Ok(()),
    }
    Ok(())
}

/// Run a single task against a single model, collecting metrics.
async fn run_single_task(
    task: &BenchmarkTask,
    spec: &ModelSpec,
    max_turns: usize,
    workspace_dir: &Path,
) -> Result<BenchmarkResult> {
    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");

    let effective_turns = if task.needs_tools { max_turns } else { 3 };

    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir,
        provider: Some(spec.provider.clone()),
        model: Some(spec.model.clone()),
        max_turns: effective_turns,
        yolo: true, // auto-approve for benchmarks
        working_dir: Some(workspace_dir.to_path_buf()),
        ..Default::default()
    })
    .await?;

    let (tx, mut rx) = mpsc::unbounded_channel();
    let cancel = CancellationToken::new();

    // Set a timeout for the entire run
    let timeout_cancel = cancel.clone();
    tokio::spawn(async move {
        tokio::time::sleep(tokio::time::Duration::from_secs(120)).await;
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

    let mut ttft: Option<u64> = None;
    let mut total_output = String::new();
    let mut input_tokens: usize = 0;
    let mut output_tokens: usize = 0;
    let mut cost_usd: f64 = 0.0;

    // Agent-quality metrics
    let mut tool_calls_count: usize = 0;
    let mut tool_calls_detail: Vec<String> = Vec::new();
    let mut turns_used: usize = 0;
    let mut self_corrections: usize = 0;
    let mut last_tool_was_error = false;
    let mut in_assistant_turn = false;

    while let Some(event) = rx.recv().await {
        match event {
            AgentEvent::Token(t) => {
                if ttft.is_none() {
                    ttft = Some(start.elapsed().as_millis() as u64);
                }
                // First token in a new assistant turn
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
                // TokenUsage marks the end of an assistant response; reset turn tracking
                in_assistant_turn = false;
            }
            AgentEvent::ToolCall(tc) => {
                tool_calls_count += 1;
                tool_calls_detail.push(tc.name.clone());
                // If the previous tool returned an error and the model is retrying,
                // count it as a self-correction
                if last_tool_was_error {
                    self_corrections += 1;
                    last_tool_was_error = false;
                }
            }
            AgentEvent::ToolResult(tr) => {
                // Include tool results in output for quality checking
                total_output.push_str(&tr.content);
                last_tool_was_error = tr.is_error;
            }
            AgentEvent::Complete(_) => break,
            AgentEvent::Error(e) => {
                return Err(eyre!("Agent error: {}", e));
            }
            _ => {}
        }
    }

    let total_time_ms = start.elapsed().as_millis() as u64;

    // Wait for the spawned task
    let _result = handle.await??;

    // Quality check (Tier 1: regex patterns)
    let (quality_pass, quality_details) = check_quality(&total_output, &task.expected_patterns);

    let tokens_per_second = if total_time_ms > 0 {
        (output_tokens as f64) / (total_time_ms as f64 / 1000.0)
    } else {
        0.0
    };

    // Tier 2/3: Compile & test validation
    let (compile_success, tests_passed, tests_total, compile_error) =
        if let Some(ref harness) = task.test_harness {
            if task.needs_tools {
                // Tier 3: read the file back after agent edits, then compile + test
                run_tier3_validation(workspace_dir, task.name, harness).await
            } else {
                // Tier 2: extract code from output, compile + test
                run_tier2_validation(&total_output, harness).await
            }
        } else {
            (None, None, None, None)
        };

    // Determine if the task passed (compile_success for code tasks, quality_pass for others)
    let task_passed = compile_success.unwrap_or(quality_pass);

    // cost_per_task_usd: only for resolved tasks
    let cost_per_task_usd = if task_passed { Some(cost_usd) } else { None };

    // tool_efficiency_score: ratio of minimum expected tools to actual tools used
    let tool_efficiency_score = if let Some(min) = task.expected_min_tools {
        if tool_calls_count > 0 {
            Some(min as f64 / tool_calls_count as f64)
        } else {
            None
        }
    } else {
        None
    };

    // consistency_hash: hash of the output for variance tracking across runs
    let consistency_hash = if !total_output.trim().is_empty() {
        let mut hasher = DefaultHasher::new();
        total_output.hash(&mut hasher);
        Some(format!("{:016x}", hasher.finish()))
    } else {
        None
    };

    Ok(BenchmarkResult {
        task_name: task.name.to_string(),
        task_category: task.category.to_string(),
        provider: spec.provider.clone(),
        model: spec.model.clone(),
        ttft_ms: ttft,
        total_time_ms,
        input_tokens,
        output_tokens,
        tokens_per_second,
        cost_usd,
        quality_pass,
        quality_details,
        error: None,
        compile_success,
        tests_passed,
        tests_total,
        compile_error,
        judge_scores: None,
        tool_calls_count,
        tool_calls_detail,
        turns_used,
        self_corrections,
        raw_output: Some(total_output),
        cost_per_task_usd,
        tool_efficiency_score,
        consistency_hash,
    })
}

/// Extract code from model output, aware of the target language.
///
/// Looks for fenced code blocks tagged with the appropriate language, then falls
/// back to generic fenced blocks, then to language-specific heuristics.
fn extract_code(output: &str, language: Language) -> Option<String> {
    // Language-specific fenced block tags to try first
    let lang_tags: &[&str] = match language {
        Language::Rust => &["rust"],
        Language::Python => &["python", "py"],
        Language::JavaScript => &["javascript", "js", "jsx", "typescript", "ts", "tsx"],
        Language::Go => &["go", "golang"],
    };

    // Try language-specific fenced blocks first
    for tag in lang_tags {
        let pattern = format!(r"(?s)```{}\s*\n(.*?)```", tag);
        if let Ok(re) = Regex::new(&pattern) {
            if let Some(cap) = re.captures(output) {
                return Some(cap[1].to_string());
            }
        }
    }

    // Try generic ``` blocks
    let re_generic = Regex::new(r"(?s)```\s*\n(.*?)```").ok()?;
    if let Some(cap) = re_generic.captures(output) {
        return Some(cap[1].to_string());
    }

    // Language-specific heuristics for unfenced code
    match language {
        Language::Rust => {
            // Try to find code that looks like a function definition without fences
            let re_fn = Regex::new(r"(?s)((?:use\s+.*?;\s*)*(?:pub\s+)?fn\s+\w+.*?\n\})").ok()?;
            if let Some(cap) = re_fn.captures(output) {
                return Some(cap[1].to_string());
            }
            if output.contains("fn ") {
                return Some(output.to_string());
            }
        }
        Language::Python => {
            // Look for def or class definitions
            let re_def =
                Regex::new(r"(?s)((?:import\s+.*\n|from\s+.*\n)*(?:def|class)\s+\w+.*)").ok()?;
            if let Some(cap) = re_def.captures(output) {
                return Some(cap[1].to_string());
            }
            if output.contains("def ") || output.contains("class ") {
                return Some(output.to_string());
            }
        }
        Language::JavaScript => {
            // Look for function definitions or const/let assignments
            let re_fn = Regex::new(r"(?s)((?:const|let|var|function)\s+\w+.*)").ok()?;
            if let Some(cap) = re_fn.captures(output) {
                return Some(cap[1].to_string());
            }
            if output.contains("function ") || output.contains("const ") {
                return Some(output.to_string());
            }
        }
        Language::Go => {
            // Look for func or type definitions
            let re_fn =
                Regex::new(r"(?s)((?:package\s+\w+\s*\n)?(?:import\s+.*\n)*(?:type|func)\s+\w+.*)")
                    .ok()?;
            if let Some(cap) = re_fn.captures(output) {
                return Some(cap[1].to_string());
            }
            if output.contains("func ") || output.contains("type ") {
                return Some(output.to_string());
            }
        }
    }

    None
}

/// Backward-compatible wrapper for Rust code extraction (used in tests).
#[cfg(test)]
fn extract_rust_code(output: &str) -> Option<String> {
    extract_code(output, Language::Rust)
}

/// Tier 2: Extract code from model output, write to temp file with tests, compile and run.
///
/// Dispatches to language-specific validation based on `harness.language`.
async fn run_tier2_validation(
    model_output: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    match harness.language {
        Language::Rust => run_tier2_rust(model_output, harness).await,
        Language::Python => run_tier2_python(model_output, harness).await,
        Language::JavaScript => run_tier2_javascript(model_output, harness).await,
        Language::Go => run_tier2_go(model_output, harness).await,
    }
}

/// Tier 2 validation for Rust: compile with rustc --test, run the binary.
async fn run_tier2_rust(
    model_output: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let Some(code) = extract_code(model_output, Language::Rust) else {
        return (
            Some(false),
            None,
            None,
            Some("Could not extract Rust code from output".to_string()),
        );
    };

    // Build the full source: extracted code + use statements for HashMap if needed + test harness
    let mut full_source = String::new();

    // Add common imports if not present
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

/// Tier 2 validation for Python: concatenate extracted code + test harness, run with python3.
async fn run_tier2_python(
    model_output: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let Some(code) = extract_code(model_output, Language::Python) else {
        return (
            Some(false),
            None,
            None,
            Some("Could not extract Python code from output".to_string()),
        );
    };

    let full_source = format!("{}\n{}", code, harness.test_code);
    run_script("python3", &full_source, "py", harness.test_count).await
}

/// Tier 2 validation for JavaScript: concatenate extracted code + test harness, run with node.
async fn run_tier2_javascript(
    model_output: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let Some(code) = extract_code(model_output, Language::JavaScript) else {
        return (
            Some(false),
            None,
            None,
            Some("Could not extract JavaScript code from output".to_string()),
        );
    };

    let full_source = format!("{}\n{}", code, harness.test_code);
    run_script("node", &full_source, "js", harness.test_count).await
}

/// Compiled regex for stripping `func main()` bodies from model-generated Go code.
///
/// Using `LazyLock` ensures the pattern is validated at first use (in tests)
/// rather than panicking at runtime with `.unwrap()`.
static RE_GO_MAIN: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?s)func\s+main\s*\(\s*\)\s*\{[^}]*\}")
        .expect("RE_GO_MAIN: static regex pattern is invalid — this is a compile-time bug")
});

/// Tier 2 validation for Go: wrap extracted code in package main with imports, run with `go run`.
async fn run_tier2_go(
    model_output: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let Some(code) = extract_code(model_output, Language::Go) else {
        return (
            Some(false),
            None,
            None,
            Some("Could not extract Go code from output".to_string()),
        );
    };

    // Build a single-file Go program. If the model already included `package main`,
    // we skip adding it again; otherwise we wrap everything.
    let mut full_source = String::new();

    let has_package = code.contains("package ");

    if !has_package {
        full_source.push_str("package main\n\n");
    }

    // Gather required imports from both code and test harness
    let combined = format!("{}\n{}", code, harness.test_code);
    let mut imports: Vec<&str> = Vec::new();
    if combined.contains("fmt.") && !combined.contains("\"fmt\"") {
        imports.push("\"fmt\"");
    }
    if combined.contains("os.") && !combined.contains("\"os\"") {
        imports.push("\"os\"");
    }
    if combined.contains("sync.") && !combined.contains("\"sync\"") {
        imports.push("\"sync\"");
    }

    if !imports.is_empty() && !code.contains("import ") {
        full_source.push_str("import (\n");
        for imp in &imports {
            full_source.push_str(&format!("    {}\n", imp));
        }
        full_source.push_str(")\n\n");
    }

    // Strip package/import lines from model code if we already added them
    let code_lines: Vec<&str> = code.lines().collect();
    let mut skip_import_block = false;
    for line in &code_lines {
        let trimmed = line.trim();
        if !has_package && trimmed.starts_with("package ") {
            continue;
        }
        if !has_package && trimmed == "import (" {
            skip_import_block = true;
            continue;
        }
        if skip_import_block {
            if trimmed == ")" {
                skip_import_block = false;
            }
            continue;
        }
        if !has_package && trimmed.starts_with("import ") && !trimmed.contains("(") {
            continue;
        }
        full_source.push_str(line);
        full_source.push('\n');
    }

    // Remove any `func main()` from the model code since the test harness provides one
    let source_without_main = RE_GO_MAIN.replace_all(&full_source, "").to_string();

    let final_source = format!("{}\n{}", source_without_main, harness.test_code);
    run_script("go", &final_source, "go", harness.test_count).await
}

/// Run a script file with the given interpreter and check results.
///
/// For Go, uses `go run <file>`. For others, uses `<interpreter> <file>`.
/// Returns the standard (compile_success, tests_passed, tests_total, error) tuple.
async fn run_script(
    interpreter: &str,
    source: &str,
    extension: &str,
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

    let filename = format!("bench_test.{}", extension);
    let source_path = temp_dir.path().join(&filename);

    if let Err(e) = tokio::fs::write(&source_path, source).await {
        return (
            Some(false),
            None,
            None,
            Some(format!("Failed to write source: {}", e)),
        );
    }

    // Build the command based on interpreter
    let output = if interpreter == "go" {
        tokio::process::Command::new("go")
            .arg("run")
            .arg(source_path.to_str().unwrap_or(&filename))
            .current_dir(temp_dir.path())
            .output()
            .await
    } else {
        tokio::process::Command::new(interpreter)
            .arg(source_path.to_str().unwrap_or(&filename))
            .current_dir(temp_dir.path())
            .output()
            .await
    };

    let result = match output {
        Ok(r) => r,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to run {} {}: {}", interpreter, filename, e)),
            );
        }
    };

    let stdout = String::from_utf8_lossy(&result.stdout);
    let stderr = String::from_utf8_lossy(&result.stderr);

    if result.status.success() {
        // For Python unittest, parse "Ran N tests" from stderr
        let tests_passed = if interpreter == "python3" {
            parse_python_test_count(&stderr).unwrap_or(expected_test_count)
        } else {
            expected_test_count
        };
        (Some(true), Some(tests_passed), Some(tests_passed), None)
    } else {
        let error_output = if !stderr.is_empty() {
            stderr.to_string()
        } else {
            stdout.to_string()
        };
        let error_msg = if error_output.len() > 500 {
            format!("{}...", &error_output[..500])
        } else {
            error_output
        };

        // For Python, try to parse how many tests passed vs failed
        if interpreter == "python3" {
            let (passed, failed) = parse_python_test_results(&stderr);
            if passed + failed > 0 {
                return (
                    Some(true),
                    Some(passed),
                    Some(passed + failed),
                    Some(error_msg),
                );
            }
        }

        (
            Some(false),
            Some(0),
            Some(expected_test_count),
            Some(error_msg),
        )
    }
}

/// Parse "Ran N tests" from Python unittest output (printed to stderr).
fn parse_python_test_count(output: &str) -> Option<usize> {
    let re = Regex::new(r"Ran (\d+) test").ok()?;
    re.captures(output)
        .and_then(|cap| cap[1].parse::<usize>().ok())
}

/// Parse Python unittest failure details: "FAILED (failures=N)" or "OK".
fn parse_python_test_results(output: &str) -> (usize, usize) {
    let total = parse_python_test_count(output).unwrap_or(0);
    if output.contains("OK") && !output.contains("FAILED") {
        return (total, 0);
    }
    let re_fail = Regex::new(r"failures=(\d+)").ok();
    let failures = re_fail
        .and_then(|re| re.captures(output))
        .and_then(|cap| cap[1].parse::<usize>().ok())
        .unwrap_or(0);
    let re_errors = Regex::new(r"errors=(\d+)").ok();
    let errors = re_errors
        .and_then(|re| re.captures(output))
        .and_then(|cap| cap[1].parse::<usize>().ok())
        .unwrap_or(0);
    let failed = failures + errors;
    (total.saturating_sub(failed), failed)
}

/// Tier 3: Read the agent-edited file, append tests, compile and run.
async fn run_tier3_validation(
    temp_dir: &Path,
    task_name: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let filename = match task_name {
        "bugfix_off_by_one" => "binary_search.rs",
        "bugfix_lifetime" => "lifetime_fix.rs",
        "refactor_extract" => "refactor.rs",
        // Agent quality tasks
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

    // Combine the edited file with the test harness
    let full_source = format!("{}\n{}", file_content, harness.test_code);

    compile_and_test(&full_source, harness.test_count).await
}

/// Compile a Rust source file and run its tests. Returns (compile_success, tests_passed, tests_total, compile_error).
async fn compile_and_test(
    source: &str,
    expected_test_count: usize,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    // Write to a temp file
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

    // Compile with --test flag
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
        // Truncate long compiler errors
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

    // Run tests
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

    // Parse test results from output: "test result: ok. X passed; Y failed; ..."
    let (passed, failed) = parse_test_output(&stdout);

    let tests_passed = passed;
    let tests_total = passed + failed;
    // If we couldn't parse, fall back to expected count
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
    let re = Regex::new(r"test result:.*?(\d+) passed.*?(\d+) failed").ok();
    if let Some(re) = re {
        if let Some(cap) = re.captures(output) {
            let passed = cap[1].parse().unwrap_or(0);
            let failed = cap[2].parse().unwrap_or(0);
            return (passed, failed);
        }
    }
    (0, 0)
}

// ---------------------------------------------------------------------------
// LLM-as-Judge
// ---------------------------------------------------------------------------

const JUDGE_PROMPT_TEMPLATE: &str = r#"You are an expert code evaluator judging AI-generated code for a benchmark.

Think carefully and step by step about each dimension before providing your scores. Consider edge cases, algorithmic complexity, error handling, and Rust best practices. Reason through what the code does, whether it handles all inputs correctly, and how it compares to an ideal solution.

## Task
{task_prompt}

## Model Output
{model_output}

## Compilation Result
{compile_result}

## Test Results
{test_results}

Rate the output on these dimensions (0-10 each). For each dimension, think through your reasoning before assigning a score:
- **correctness**: Does the code solve the task correctly? Consider edge cases, off-by-one errors, and boundary conditions.
- **code_quality**: Is the code clean, readable, well-structured? Consider naming, modularity, and documentation.
- **efficiency**: Is the algorithm efficient for the problem? Consider time and space complexity.
- **idiomatic**: Does it use idiomatic Rust patterns? Consider ownership, error handling, iterator usage, and type system leverage.

Respond ONLY with JSON (no markdown wrapping):
{"correctness": N, "code_quality": N, "efficiency": N, "idiomatic": N, "notes": "brief explanation of key strengths and weaknesses"}"#;

/// Run LLM-as-Judge evaluation on all benchmark results.
async fn judge_outputs(
    results: &mut [BenchmarkResult],
    judge_specs: &[ModelSpec],
    tasks: &[BenchmarkTask],
) {
    // Build a task prompt lookup
    let task_prompts: HashMap<&str, &str> =
        tasks.iter().map(|t| (t.name, t.prompt.as_str())).collect();

    let credentials = CredentialStore::load_default().await.unwrap_or_default();
    let pool = Arc::new(ConnectionPool::new());

    for result in results.iter_mut() {
        let raw_output = match &result.raw_output {
            Some(o) if !o.trim().is_empty() => o.clone(),
            _ => continue,
        };

        let task_prompt = task_prompts
            .get(result.task_name.as_str())
            .copied()
            .unwrap_or("(unknown task)");

        let compile_result = match result.compile_success {
            Some(true) => "Compilation succeeded".to_string(),
            Some(false) => {
                let err = result.compile_error.as_deref().unwrap_or("unknown error");
                format!("Compilation failed: {}", err)
            }
            None => "Not applicable (no compilation step)".to_string(),
        };

        let test_results = match (result.tests_passed, result.tests_total) {
            (Some(p), Some(t)) => format!("{}/{} tests passed", p, t),
            _ => "Not applicable (no tests)".to_string(),
        };

        // Truncate output for judge to avoid huge prompts
        let truncated_output = if raw_output.len() > 4000 {
            format!("{}...(truncated)", &raw_output[..4000])
        } else {
            raw_output.clone()
        };

        let judge_prompt = JUDGE_PROMPT_TEMPLATE
            .replace("{task_prompt}", task_prompt)
            .replace("{model_output}", &truncated_output)
            .replace("{compile_result}", &compile_result)
            .replace("{test_results}", &test_results);

        let mut evaluations = Vec::new();

        for judge_spec in judge_specs {
            eprintln!(
                "  [judge] {} evaluating {}:{}...",
                short_model_name(&judge_spec.model),
                result.task_name,
                short_model_name(&result.model),
            );

            match evaluate_with_judge(&judge_prompt, judge_spec, &credentials, pool.clone()).await {
                Ok(eval) => evaluations.push(eval),
                Err(e) => {
                    eprintln!(
                        "  [judge] ERROR from {}: {}",
                        short_model_name(&judge_spec.model),
                        e
                    );
                }
            }
        }

        if !evaluations.is_empty() {
            let n = evaluations.len() as f64;
            let correctness = evaluations.iter().map(|e| e.correctness).sum::<f64>() / n;
            let code_quality = evaluations.iter().map(|e| e.code_quality).sum::<f64>() / n;
            let efficiency = evaluations.iter().map(|e| e.efficiency).sum::<f64>() / n;
            let idiomatic = evaluations.iter().map(|e| e.idiomatic).sum::<f64>() / n;
            let average = (correctness + code_quality + efficiency + idiomatic) / 4.0;

            result.judge_scores = Some(JudgeScores {
                correctness,
                code_quality,
                efficiency,
                idiomatic,
                average,
                evaluations,
            });
        }
    }
}

/// Call a single judge model to evaluate a benchmark output.
///
/// Uses `generate_with_thinking` at `ThinkingLevel::High` so that judge models
/// engage their reasoning capabilities for deeper evaluation. This maps to:
/// - Anthropic: extended thinking (high budget)
/// - OpenAI: reasoning_effort "high"
/// - Gemini: reasoning_effort "high"
/// - Other providers: graceful fallback to standard generation
async fn evaluate_with_judge(
    judge_prompt: &str,
    judge_spec: &ModelSpec,
    credentials: &CredentialStore,
    pool: Arc<ConnectionPool>,
) -> Result<JudgeEvaluation> {
    let provider = create_provider(&judge_spec.provider, &judge_spec.model, credentials, pool)
        .map_err(|e| eyre!("Failed to create judge provider: {}", e))?;

    let messages = vec![Message::new(Role::User, judge_prompt)];

    // Use thinking/reasoning mode for higher-quality evaluations.
    // ThinkingLevel::High enables extended thinking on Anthropic, reasoning_effort
    // "high" on OpenAI/Gemini. Providers that don't support thinking fall back to
    // standard generation via the default trait implementation.
    let response = if provider.supports_thinking() {
        let llm_response = provider
            .generate_with_thinking(&messages, &[], ThinkingLevel::High)
            .await
            .map_err(|e| eyre!("Judge generate_with_thinking failed: {}", e))?;
        llm_response.content
    } else {
        provider
            .generate(&messages)
            .await
            .map_err(|e| eyre!("Judge generate failed: {}", e))?
    };

    // Parse JSON from response (the model should return only JSON)
    parse_judge_response(&response, &judge_spec.model)
}

/// Parse the judge's JSON response into a JudgeEvaluation.
fn parse_judge_response(response: &str, judge_model: &str) -> Result<JudgeEvaluation> {
    // Try to find JSON in the response (may have markdown wrapping)
    let json_str = extract_json(response).ok_or_else(|| {
        eyre!(
            "Could not extract JSON from judge response: {}",
            if response.len() > 200 {
                format!("{}...", &response[..200])
            } else {
                response.to_string()
            }
        )
    })?;

    let parsed: serde_json::Value =
        serde_json::from_str(&json_str).map_err(|e| eyre!("Failed to parse judge JSON: {}", e))?;

    let correctness = parsed["correctness"]
        .as_f64()
        .unwrap_or(0.0)
        .clamp(0.0, 10.0);
    let code_quality = parsed["code_quality"]
        .as_f64()
        .unwrap_or(0.0)
        .clamp(0.0, 10.0);
    let efficiency = parsed["efficiency"]
        .as_f64()
        .unwrap_or(0.0)
        .clamp(0.0, 10.0);
    let idiomatic = parsed["idiomatic"].as_f64().unwrap_or(0.0).clamp(0.0, 10.0);
    let notes = parsed["notes"].as_str().unwrap_or("").to_string();

    Ok(JudgeEvaluation {
        judge_model: judge_model.to_string(),
        correctness,
        code_quality,
        efficiency,
        idiomatic,
        notes,
    })
}

/// Extract a JSON object from a string that may contain markdown fences or other text.
fn extract_json(text: &str) -> Option<String> {
    // Try direct parse first
    if serde_json::from_str::<serde_json::Value>(text.trim()).is_ok() {
        return Some(text.trim().to_string());
    }

    // Try to find JSON in ```json ... ``` blocks
    let re_json = Regex::new(r"(?s)```(?:json)?\s*\n(\{.*?\})\s*```").ok()?;
    if let Some(cap) = re_json.captures(text) {
        let candidate = cap[1].to_string();
        if serde_json::from_str::<serde_json::Value>(&candidate).is_ok() {
            return Some(candidate);
        }
    }

    // Try to find any { ... } that parses as JSON
    let start = text.find('{')?;
    let end = text.rfind('}')? + 1;
    if start < end {
        let candidate = &text[start..end];
        if serde_json::from_str::<serde_json::Value>(candidate).is_ok() {
            return Some(candidate.to_string());
        }
    }

    None
}

// ---------------------------------------------------------------------------
// Output formatting
// ---------------------------------------------------------------------------

/// Check output quality against expected regex patterns.
fn check_quality(output: &str, expected_patterns: &[&str]) -> (bool, String) {
    if output.trim().is_empty() {
        return (false, "empty output".to_string());
    }

    let mut passed = 0;
    let total = expected_patterns.len();
    let mut missing = Vec::new();

    for pattern in expected_patterns {
        match Regex::new(pattern) {
            Ok(re) => {
                if re.is_match(output) {
                    passed += 1;
                } else {
                    missing.push(*pattern);
                }
            }
            Err(_) => {
                // Treat bad patterns as passed to avoid false negatives
                passed += 1;
            }
        }
    }

    let all_pass = passed == total;
    let details = if all_pass {
        format!("{}/{} patterns matched", passed, total)
    } else {
        format!(
            "{}/{} patterns matched (missing: {})",
            passed,
            total,
            missing
                .iter()
                .map(|p| truncate_pattern(p))
                .collect::<Vec<_>>()
                .join(", ")
        )
    };

    (all_pass, details)
}

fn truncate_pattern(p: &str) -> String {
    if p.len() > 30 {
        format!("{}...", &p[..27])
    } else {
        p.to_string()
    }
}

/// Print a formatted results table to stdout.
fn print_results_table(report: &BenchmarkReport, suite: BenchmarkSuite) {
    if report.results.is_empty() {
        println!("No benchmark results.");
        return;
    }

    // Group results by task
    let mut tasks_seen: Vec<String> = Vec::new();
    for r in &report.results {
        if !tasks_seen.contains(&r.task_name) {
            tasks_seen.push(r.task_name.clone());
        }
    }

    println!();
    println!("=======================================================================",);
    println!(
        "                AVA Model Benchmark Results ({} suite)",
        suite
    );
    println!("                     {}", &report.timestamp[..19]);
    println!("=======================================================================",);

    let has_judges = report.results.iter().any(|r| r.judge_scores.is_some());

    for task_name in &tasks_seen {
        let task_results: Vec<&BenchmarkResult> = report
            .results
            .iter()
            .filter(|r| &r.task_name == task_name)
            .collect();

        if task_results.is_empty() {
            continue;
        }

        let has_compile = task_results.iter().any(|r| r.compile_success.is_some());
        let has_tools = task_results.iter().any(|r| r.tool_calls_count > 0);

        println!();
        println!("  Task: {} [{}]", task_name, task_results[0].task_category);

        if has_compile && has_judges {
            println!(
                "  {:<22} {:>9} {:>9} {:>7} {:>8} {:>7} {:>6} {:>6} {:>7}",
                "Model",
                "TTFT(ms)",
                "Total(s)",
                "Tok/s",
                "Compile",
                "Tests",
                "Tools",
                "Turns",
                "Score",
            );
            println!(
                "  {:-<22} {:-<9} {:-<9} {:-<7} {:-<8} {:-<7} {:-<6} {:-<6} {:-<7}",
                "", "", "", "", "", "", "", "", ""
            );
        } else if has_compile {
            println!(
                "  {:<22} {:>9} {:>9} {:>7} {:>8} {:>7} {:>6} {:>6}  Quality",
                "Model", "TTFT(ms)", "Total(s)", "Tok/s", "Compile", "Tests", "Tools", "Turns",
            );
            println!(
                "  {:-<22} {:-<9} {:-<9} {:-<7} {:-<8} {:-<7} {:-<6} {:-<6}  {:-<20}",
                "", "", "", "", "", "", "", "", ""
            );
        } else if has_tools {
            println!(
                "  {:<22} {:>9} {:>9} {:>7} {:>8} {:>8} {:>6} {:>6}  Quality",
                "Model", "TTFT(ms)", "Total(s)", "Tok/s", "In Tok", "Cost", "Tools", "Turns",
            );
            println!(
                "  {:-<22} {:-<9} {:-<9} {:-<7} {:-<8} {:-<8} {:-<6} {:-<6}  {:-<20}",
                "", "", "", "", "", "", "", "", ""
            );
        } else if has_judges {
            println!(
                "  {:<22} {:>9} {:>9} {:>7} {:>8} {:>8} {:>7}",
                "Model", "TTFT(ms)", "Total(s)", "Tok/s", "In Tok", "Cost", "Score",
            );
            println!(
                "  {:-<22} {:-<9} {:-<9} {:-<7} {:-<8} {:-<8} {:-<7}",
                "", "", "", "", "", "", ""
            );
        } else {
            println!(
                "  {:<22} {:>9} {:>9} {:>8} {:>8} {:>8}  Quality",
                "Model", "TTFT(ms)", "Total(s)", "Tok/s", "In Tok", "Cost",
            );
            println!(
                "  {:-<22} {:-<9} {:-<9} {:-<8} {:-<8} {:-<8}  {:-<20}",
                "", "", "", "", "", "", ""
            );
        }

        for r in &task_results {
            let model_short = short_model_name(&r.model);
            let model_display = if model_short.len() > 22 {
                format!("{}...", &model_short[..19])
            } else {
                model_short
            };

            let ttft_str = r
                .ttft_ms
                .map(|t| t.to_string())
                .unwrap_or_else(|| "-".to_string());

            let total_str = format!("{:.1}", r.total_time_ms as f64 / 1000.0);

            let tps_str = if r.tokens_per_second > 0.0 {
                format!("{:.0}", r.tokens_per_second)
            } else {
                "-".to_string()
            };

            let compile_str = match r.compile_success {
                Some(true) => "PASS".to_string(),
                Some(false) => "FAIL".to_string(),
                None => "-".to_string(),
            };

            let tests_str = match (r.tests_passed, r.tests_total) {
                (Some(p), Some(t)) => format!("{}/{}", p, t),
                _ => "-".to_string(),
            };

            let score_str = match &r.judge_scores {
                Some(scores) => format!("{:.1}", scores.average),
                None => "-".to_string(),
            };

            let cost_str = if r.cost_usd > 0.0 {
                format!("${:.4}", r.cost_usd)
            } else {
                "$0.00".to_string()
            };

            let quality_str = if r.error.is_some() {
                "ERROR".to_string()
            } else if r.quality_pass {
                "PASS".to_string()
            } else {
                "FAIL".to_string()
            };

            let tools_str = if r.tool_calls_count > 0 {
                r.tool_calls_count.to_string()
            } else {
                "-".to_string()
            };

            let turns_str = if r.turns_used > 0 {
                r.turns_used.to_string()
            } else {
                "-".to_string()
            };

            if has_compile && has_judges {
                println!(
                    "  {:<22} {:>9} {:>9} {:>7} {:>8} {:>7} {:>6} {:>6} {:>7}",
                    model_display,
                    ttft_str,
                    total_str,
                    tps_str,
                    compile_str,
                    tests_str,
                    tools_str,
                    turns_str,
                    score_str,
                );
            } else if has_compile {
                println!(
                    "  {:<22} {:>9} {:>9} {:>7} {:>8} {:>7} {:>6} {:>6}  {}",
                    model_display,
                    ttft_str,
                    total_str,
                    tps_str,
                    compile_str,
                    tests_str,
                    tools_str,
                    turns_str,
                    quality_str,
                );
            } else if has_tools {
                println!(
                    "  {:<22} {:>9} {:>9} {:>7} {:>8} {:>8} {:>6} {:>6}  {}",
                    model_display,
                    ttft_str,
                    total_str,
                    tps_str,
                    r.input_tokens,
                    cost_str,
                    tools_str,
                    turns_str,
                    quality_str,
                );
            } else if has_judges {
                println!(
                    "  {:<22} {:>9} {:>9} {:>7} {:>8} {:>8} {:>7}",
                    model_display,
                    ttft_str,
                    total_str,
                    tps_str,
                    r.input_tokens,
                    cost_str,
                    score_str,
                );
            } else {
                println!(
                    "  {:<22} {:>9} {:>9} {:>8} {:>8} {:>8}  {}",
                    model_display,
                    ttft_str,
                    total_str,
                    tps_str,
                    r.input_tokens,
                    cost_str,
                    quality_str,
                );
            }
        }
    }

    // Summary
    println!();
    println!("-----------------------------------------------------------------------");

    let total_cost: f64 = report.results.iter().map(|r| r.cost_usd).sum();
    let total_time: f64 = report
        .results
        .iter()
        .map(|r| r.total_time_ms as f64 / 1000.0)
        .sum();
    let pass_count = report.results.iter().filter(|r| r.quality_pass).count();
    let compile_count = report
        .results
        .iter()
        .filter(|r| r.compile_success == Some(true))
        .count();
    let compile_total = report
        .results
        .iter()
        .filter(|r| r.compile_success.is_some())
        .count();
    let error_count = report.results.iter().filter(|r| r.error.is_some()).count();

    let mut summary = format!(
        "  Total: {} runs, {}/{} quality passed",
        report.results.len(),
        pass_count,
        report.results.len(),
    );

    if compile_total > 0 {
        summary.push_str(&format!(", {}/{} compiled", compile_count, compile_total));
    }

    if error_count > 0 {
        summary.push_str(&format!(", {} errors", error_count));
    }

    summary.push_str(&format!(
        ", {:.1}s elapsed, ${:.4} total cost",
        total_time, total_cost,
    ));

    // Average judge score
    let judge_results: Vec<&BenchmarkResult> = report
        .results
        .iter()
        .filter(|r| r.judge_scores.is_some())
        .collect();
    if !judge_results.is_empty() {
        let avg_score: f64 = judge_results
            .iter()
            .map(|r| r.judge_scores.as_ref().unwrap().average)
            .sum::<f64>()
            / judge_results.len() as f64;
        summary.push_str(&format!(", avg judge score: {:.1}/10", avg_score));
    }

    // Aggregate cost per resolved task
    if let Some(cpr) = report.aggregate_cost_per_resolved {
        summary.push_str(&format!(", ${:.4}/resolved", cpr));
    }

    // Aggregate tool efficiency
    if let Some(eff) = report.aggregate_tool_efficiency {
        summary.push_str(&format!(", tool efficiency: {:.2}", eff));
    }

    println!("{}", summary);
    println!("=======================================================================");
    println!();
}

/// Save the full results as JSON to ~/.ava/benchmarks/.
async fn save_results_json(report: &BenchmarkReport) -> Result<()> {
    let benchmarks_dir = dirs::home_dir()
        .unwrap_or_default()
        .join(".ava")
        .join("benchmarks");

    tokio::fs::create_dir_all(&benchmarks_dir)
        .await
        .map_err(|e| eyre!("Failed to create benchmarks dir: {}", e))?;

    // Use timestamp for filename
    let filename = format!(
        "bench-{}.json",
        &report.timestamp.replace(':', "-").replace('T', "_")[..19]
    );
    let path = benchmarks_dir.join(&filename);

    // Strip raw_output from saved JSON to keep file size reasonable
    let mut save_report = report.clone();
    for result in &mut save_report.results {
        result.raw_output = None;
    }

    let json = serde_json::to_string_pretty(&save_report)
        .map_err(|e| eyre!("Failed to serialize results: {}", e))?;

    tokio::fs::write(&path, &json)
        .await
        .map_err(|e| eyre!("Failed to write results: {}", e))?;

    tracing::info!("[benchmark] Results saved to {}", path.display());

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_extract_rust_code_fenced() {
        let output = "Here is the code:\n```rust\nfn foo() -> bool { true }\n```\nDone.";
        let code = extract_rust_code(output).unwrap();
        assert!(code.contains("fn foo()"));
    }

    #[test]
    fn test_extract_rust_code_generic_fence() {
        let output = "```\nfn bar() {}\n```";
        let code = extract_rust_code(output).unwrap();
        assert!(code.contains("fn bar()"));
    }

    #[test]
    fn test_extract_rust_code_no_fence() {
        let output = "pub fn baz(x: i32) -> i32 {\n    x + 1\n}";
        let code = extract_rust_code(output).unwrap();
        assert!(code.contains("fn baz"));
    }

    #[test]
    fn test_extract_json_direct() {
        let json = r#"{"correctness": 8, "code_quality": 7, "efficiency": 9, "idiomatic": 8, "notes": "good"}"#;
        let result = extract_json(json).unwrap();
        assert!(result.contains("correctness"));
    }

    #[test]
    fn test_extract_json_fenced() {
        let text = "Here is the evaluation:\n```json\n{\"correctness\": 8}\n```";
        let result = extract_json(text).unwrap();
        assert!(result.contains("correctness"));
    }

    #[test]
    fn test_extract_json_embedded() {
        let text = "My evaluation: {\"correctness\": 5, \"code_quality\": 6, \"efficiency\": 7, \"idiomatic\": 8, \"notes\": \"ok\"} end.";
        let result = extract_json(text).unwrap();
        assert!(result.contains("correctness"));
    }

    #[test]
    fn test_parse_test_output() {
        let output = "test result: ok. 3 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out";
        let (passed, failed) = parse_test_output(output);
        assert_eq!(passed, 3);
        assert_eq!(failed, 0);
    }

    #[test]
    fn test_parse_test_output_with_failures() {
        let output = "test result: FAILED. 2 passed; 1 failed; 0 ignored";
        let (passed, failed) = parse_test_output(output);
        assert_eq!(passed, 2);
        assert_eq!(failed, 1);
    }

    #[test]
    fn test_parse_judge_response() {
        let response = r#"{"correctness": 9, "code_quality": 8, "efficiency": 7, "idiomatic": 8.5, "notes": "Well done"}"#;
        let eval = parse_judge_response(response, "test-model").unwrap();
        assert_eq!(eval.correctness, 9.0);
        assert_eq!(eval.code_quality, 8.0);
        assert_eq!(eval.efficiency, 7.0);
        assert_eq!(eval.idiomatic, 8.5);
        assert_eq!(eval.notes, "Well done");
    }

    #[test]
    fn test_parse_model_specs() {
        let specs =
            parse_model_specs(None, None, Some("openrouter:model-a,openrouter:model-b")).unwrap();
        assert_eq!(specs.len(), 2);
        assert_eq!(specs[0].provider, "openrouter");
        assert_eq!(specs[0].model, "model-a");
    }

    #[test]
    fn test_parse_judge_specs() {
        let specs =
            parse_judge_specs(Some("openrouter:claude-sonnet,openrouter:gemini-pro")).unwrap();
        assert_eq!(specs.len(), 2);
    }

    #[test]
    fn test_parse_judge_specs_empty() {
        let specs = parse_judge_specs(None).unwrap();
        assert!(specs.is_empty());
    }

    /// Verify all static regexes compile successfully.
    /// Catches any future regex mutation that would otherwise panic at runtime.
    #[test]
    fn regexes_compile() {
        // Force initialisation of the LazyLock; panics here rather than at runtime if invalid.
        let _ = &*RE_GO_MAIN;
        assert!(RE_GO_MAIN.is_match("func main() { return }"));
        assert!(!RE_GO_MAIN.is_match("func helper() { return }"));
    }
}
