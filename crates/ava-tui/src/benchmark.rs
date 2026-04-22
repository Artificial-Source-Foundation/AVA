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

use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};
use std::time::Instant;

use ava_agent::system_prompt::{resolved_prompt_family, BenchmarkPromptOverride};
use ava_agent::AgentEvent;
use ava_agent_orchestration::stack::{AgentStack, AgentStackConfig};
use color_eyre::eyre::{eyre, Result};
use regex::Regex;
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

use crate::benchmark_format::{
    format_subagent_mix, print_repeat_summary, print_results_table, short_model_name,
};
use crate::benchmark_judge::judge_outputs;
use crate::benchmark_reporting::{compute_aggregate_summary, AggregateScoreSummary, ScoreInput};
use crate::benchmark_support::{
    expected_min_subagents, prepare_benchmark_workspace, run_tier3_validation, setup_agentic_file,
    spawn_default_question_responses, subagent_type_from_description, BenchmarkWorkspaceGuard,
};
use crate::benchmark_tasks::{
    advanced_rust_tasks, agent_quality_tasks, agentic_tasks, default_tasks, filter_tasks_by_name,
    filter_tasks_by_suite, go_tasks, large_project_tasks, lsp_smoke_tasks, maintenance_tasks,
    mcp_integration_tasks, multi_file_tasks, normal_coding_tasks, product_smoke_tasks,
    prompt_regression_tasks, python_tasks, security_tasks, small_coding_tasks, stress_coding_tasks,
    test_generation_tasks, test_heavy_tasks, tool_recovery_tasks, tool_reliability_tasks,
    typescript_tasks, BenchmarkSuite, BenchmarkTask, Language,
};
use crate::benchmark_validation::run_tier2_validation;
use crate::headless::spawn_auto_approve_requests;

/// A provider:model pair to benchmark.
#[derive(Debug, Clone)]
pub struct ModelSpec {
    pub provider: String,
    pub model: String,
}

const BENCHMARK_REPORT_SCHEMA_VERSION: u32 = 2;

fn default_report_schema_version() -> u32 {
    0
}

fn default_run_count() -> usize {
    1
}

#[derive(Debug, Clone)]
pub struct BenchmarkOptions {
    pub prompt: BenchmarkPromptConfig,
    pub repeat: usize,
    pub seed: Option<u64>,
    pub output_path: Option<PathBuf>,
}

impl Default for BenchmarkOptions {
    fn default() -> Self {
        Self {
            prompt: BenchmarkPromptConfig::default(),
            repeat: 1,
            seed: None,
            output_path: None,
        }
    }
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct BenchmarkPromptConfig {
    #[serde(default)]
    pub family: Option<String>,
    #[serde(default)]
    pub variant: Option<String>,
    #[serde(default)]
    pub file_path: Option<String>,
    #[serde(default)]
    pub version: Option<String>,
    #[serde(default)]
    pub hash: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub file_contents: Option<String>,
}

impl BenchmarkPromptConfig {
    pub async fn from_cli(
        family: Option<&str>,
        variant: Option<&str>,
        file_path: Option<&str>,
        version: Option<&str>,
        hash: Option<&str>,
    ) -> Result<Self> {
        let file_contents = if let Some(path) = file_path {
            Some(
                tokio::fs::read_to_string(path)
                    .await
                    .map_err(|e| eyre!("Failed to read prompt override file {}: {}", path, e))?,
            )
        } else {
            None
        };

        let mut config = Self {
            family: family.map(str::to_string),
            variant: variant.map(str::to_string),
            file_path: file_path.map(str::to_string),
            version: version.map(str::to_string),
            hash: hash.map(str::to_string),
            file_contents,
        };

        if config.hash.is_none() {
            config.hash = config.derived_hash();
        }

        Ok(config)
    }

    pub fn agent_override(&self) -> Option<BenchmarkPromptOverride> {
        if self.family.is_none() && self.file_contents.is_none() {
            return None;
        }

        Some(BenchmarkPromptOverride {
            family: self.family.clone(),
            prompt_file_contents: self.file_contents.clone(),
        })
    }

    pub fn derived_hash(&self) -> Option<String> {
        if self.family.is_none()
            && self.variant.is_none()
            && self.file_path.is_none()
            && self.version.is_none()
            && self.file_contents.is_none()
        {
            return None;
        }

        let mut input = String::new();
        if let Some(family) = &self.family {
            input.push_str("family=");
            input.push_str(family);
            input.push('\n');
        }
        if let Some(variant) = &self.variant {
            input.push_str("variant=");
            input.push_str(variant);
            input.push('\n');
        }
        if let Some(version) = &self.version {
            input.push_str("version=");
            input.push_str(version);
            input.push('\n');
        }
        if let Some(path) = &self.file_path {
            input.push_str("file=");
            input.push_str(path);
            input.push('\n');
        }
        if let Some(contents) = &self.file_contents {
            input.push_str("contents=\n");
            input.push_str(contents);
        }

        Some(stable_hash_hex(&input))
    }
}

fn wall_clock_tokens_per_second(output_tokens: usize, total_time_ms: u64) -> f64 {
    if total_time_ms > 0 {
        output_tokens as f64 / (total_time_ms as f64 / 1000.0)
    } else {
        0.0
    }
}

fn generation_tokens_per_second(
    output_tokens: usize,
    total_time_ms: u64,
    ttft_ms: Option<u64>,
) -> Option<f64> {
    let ttft_ms = ttft_ms?;
    let generation_time_ms = total_time_ms.checked_sub(ttft_ms)?;
    if generation_time_ms > 0 {
        Some(output_tokens as f64 / (generation_time_ms as f64 / 1000.0))
    } else {
        None
    }
}

fn accumulate_token_usage(
    input_tokens: &mut usize,
    output_tokens: &mut usize,
    cost_usd: &mut f64,
    usage_input_tokens: usize,
    usage_output_tokens: usize,
    usage_cost_usd: f64,
) {
    *input_tokens += usage_input_tokens;
    *output_tokens += usage_output_tokens;
    *cost_usd += usage_cost_usd;
}

fn accumulate_subagent_usage(
    input_tokens: &mut usize,
    output_tokens: &mut usize,
    cost_usd: &mut f64,
    subagent_cost_usd: &mut f64,
    sub_input_tokens: usize,
    sub_output_tokens: usize,
    sub_cost_usd: f64,
) {
    *input_tokens += sub_input_tokens;
    *output_tokens += sub_output_tokens;
    *subagent_cost_usd += sub_cost_usd;
    *cost_usd += sub_cost_usd;
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
pub struct BenchmarkRepeatTaskSummary {
    pub task_name: String,
    pub task_category: String,
    pub provider: String,
    pub model: String,
    #[serde(default)]
    pub prompt_family: Option<String>,
    #[serde(default)]
    pub prompt_variant: Option<String>,
    #[serde(default)]
    pub prompt_hash: Option<String>,
    pub attempts: usize,
    pub passes: usize,
    pub failures: usize,
    pub pass_rate: f64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub compile_pass_rate: Option<f64>,
    pub median_total_time_ms: u64,
    pub p95_total_time_ms: u64,
    pub median_tool_calls_count: usize,
    pub median_subagent_calls_count: usize,
    pub average_cost_usd: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
pub struct BenchmarkRepeatSummary {
    pub repeat_count: usize,
    pub overall_pass_rate: f64,
    pub median_total_time_ms: u64,
    pub worst_task_variance_ms: u64,
    pub task_summaries: Vec<BenchmarkRepeatTaskSummary>,
}

impl std::fmt::Display for ModelSpec {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}:{}", self.provider, self.model)
    }
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
    #[serde(default)]
    pub prompt_family: Option<String>,
    #[serde(default)]
    pub prompt_variant: Option<String>,
    #[serde(default)]
    pub prompt_hash: Option<String>,
    #[serde(default)]
    pub run_index: Option<usize>,
    pub ttft_ms: Option<u64>,
    pub total_time_ms: u64,
    pub input_tokens: usize,
    pub output_tokens: usize,
    pub tokens_per_second: f64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub generation_tps: Option<f64>,
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
    /// Total number of tool results flagged as errors.
    #[serde(default)]
    pub tool_error_count: usize,
    /// Tool error counts keyed by tool name.
    #[serde(default)]
    pub tool_error_breakdown: HashMap<String, usize>,
    /// Number of agent turns consumed (each assistant response = 1 turn).
    pub turns_used: usize,
    /// Number of times the model retried after a tool error (self-corrections).
    pub self_corrections: usize,
    /// Number of hidden sub-agent runs spawned during the task.
    pub subagent_calls_count: usize,
    /// Ordered list of sub-agent types used during the task.
    pub subagent_types: Vec<String>,
    /// Ordered list of external providers used by delegated runs when known.
    #[serde(default)]
    pub subagent_providers: Vec<String>,
    /// Cost attributable to hidden sub-agents.
    pub subagent_cost_usd: f64,
    /// Number of delegated runs that resumed an existing external session.
    #[serde(default)]
    pub resumed_subagent_calls_count: usize,
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
    /// Composite score for tool reliability tasks based on execution success and tool efficiency.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tool_reliability_score: Option<f64>,
    /// Ratio of expected helper usage to actual helper usage (1.0 = ideal).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub delegation_efficiency_score: Option<f64>,
    /// Closed-loop score for whether delegation helped the task outcome.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub delegation_quality_score: Option<f64>,
    /// Hash of the code output for variance tracking across runs.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub consistency_hash: Option<String>,
}

impl BenchmarkResult {
    pub fn delegation_summary(&self) -> Option<String> {
        if self.subagent_calls_count == 0 {
            return self.delegation_efficiency_score.map(|score| {
                format!(
                    "0 helper runs | delegated cost ${:.4} | efficiency {:.2} | expected hidden delegation but none was used",
                    self.subagent_cost_usd, score
                )
            });
        }

        let mut parts = vec![format!(
            "{} helper run{}",
            self.subagent_calls_count,
            if self.subagent_calls_count == 1 {
                ""
            } else {
                "s"
            }
        )];

        if let Some(mix) = format_subagent_mix(&self.subagent_types) {
            parts.push(format!("mix: {mix}"));
        }

        if let Some(provider_mix) = format_subagent_mix(&self.subagent_providers) {
            parts.push(format!("providers: {provider_mix}"));
        }

        if self.resumed_subagent_calls_count > 0 {
            parts.push(format!("resumed {}", self.resumed_subagent_calls_count));
        }

        parts.push(format!("delegated cost ${:.4}", self.subagent_cost_usd));

        if let Some(score) = self.delegation_efficiency_score {
            parts.push(format!("efficiency {:.2}", score));
        }

        if let Some(score) = self.delegation_quality_score {
            parts.push(format!("quality {:.2}", score));
        }

        Some(parts.join(" | "))
    }
}

/// Full benchmark suite results.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BenchmarkReport {
    #[serde(default = "default_report_schema_version")]
    pub schema_version: u32,
    #[serde(default)]
    pub binary_version: Option<String>,
    #[serde(default)]
    pub binary_commit: Option<String>,
    #[serde(default)]
    pub suite_name: Option<String>,
    #[serde(default)]
    pub task_filter: Option<String>,
    #[serde(default)]
    pub provider: Option<String>,
    #[serde(default)]
    pub model: Option<String>,
    #[serde(default)]
    pub prompt: BenchmarkPromptConfig,
    #[serde(default = "default_run_count")]
    pub run_count: usize,
    #[serde(default)]
    pub run_index: Option<usize>,
    #[serde(default)]
    pub run_seed: Option<u64>,
    #[serde(default)]
    pub runner_mode: Option<String>,
    pub timestamp: String,
    pub results: Vec<BenchmarkResult>,
    #[serde(default)]
    pub score_summary: Option<AggregateScoreSummary>,
    #[serde(default)]
    pub repeat_summary: Option<BenchmarkRepeatSummary>,
    #[serde(default)]
    pub raw_report_paths: Vec<String>,
    #[serde(default)]
    pub saved_path: Option<String>,
    /// Total cost / number of resolved (passed) tasks.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub aggregate_cost_per_resolved: Option<f64>,
    /// Mean tool efficiency across all tool-using tasks with efficiency scores.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub aggregate_tool_efficiency: Option<f64>,
    /// Mean tool reliability across tool reliability tasks.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub aggregate_tool_reliability: Option<f64>,
    /// Mean delegation efficiency across tasks that expect helper usage.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub aggregate_delegation_efficiency: Option<f64>,
    /// Mean closed-loop delegation quality across runs that used helpers.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub aggregate_delegation_quality: Option<f64>,
}

pub(crate) fn compute_delegation_quality_score(
    quality_pass: bool,
    compile_success: Option<bool>,
    judge_scores: Option<&JudgeScores>,
    subagent_calls_count: usize,
    resumed_subagent_calls_count: usize,
    subagent_cost_usd: f64,
    total_cost_usd: f64,
) -> Option<f64> {
    if subagent_calls_count == 0 {
        return None;
    }

    let outcome_score = if let Some(compiled) = compile_success {
        if compiled {
            1.0
        } else {
            0.2
        }
    } else if quality_pass {
        1.0
    } else {
        0.3
    };

    let judge_bonus = judge_scores
        .map(|scores| (scores.average / 10.0).clamp(0.0, 1.0))
        .unwrap_or(outcome_score);
    let resume_bonus = if resumed_subagent_calls_count > 0 {
        0.1
    } else {
        0.0
    };
    let delegated_cost_ratio = if total_cost_usd > 0.0 {
        (subagent_cost_usd / total_cost_usd).clamp(0.0, 1.0)
    } else {
        0.0
    };
    let cost_penalty = delegated_cost_ratio * 0.25;

    Some((outcome_score * 0.6 + judge_bonus * 0.4 + resume_bonus - cost_penalty).clamp(0.0, 1.2))
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
    task_filter: Option<&str>,
    options: BenchmarkOptions,
) -> Result<BenchmarkReport> {
    let max_turns = if max_turns == 0 { 10 } else { max_turns };
    let repeat = options.repeat.max(1);
    let workspace_dir = benchmark_workspace_dir();

    eprintln!("[benchmark] Suite: {}", suite);
    eprintln!("[benchmark] Workspace: {}", workspace_dir.display());
    if let Some(filter) = task_filter.filter(|value| !value.trim().is_empty()) {
        eprintln!("[benchmark] Task filter: {}", filter.trim());
    }
    if let Some(ref langs) = language_filter {
        let lang_names: Vec<_> = langs.iter().map(|l| l.to_string()).collect();
        eprintln!("[benchmark] Language filter: {}", lang_names.join(", "));
    }
    if let Some(family) = options.prompt.family.as_deref() {
        eprintln!("[benchmark] Prompt family: {}", family);
    }
    if let Some(variant) = options.prompt.variant.as_deref() {
        eprintln!("[benchmark] Prompt variant: {}", variant);
    }
    if repeat > 1 {
        eprintln!("[benchmark] Repeat count: {}", repeat);
    }

    let all_tasks = build_task_list(
        &workspace_dir,
        tasks,
        imported_tasks,
        suite,
        language_filter.as_ref(),
        task_filter,
    )?;

    if repeat == 1 {
        let mut report = run_benchmark_once(
            &specs,
            &all_tasks,
            max_turns,
            &judge_specs,
            suite,
            task_filter,
            &options.prompt,
            1,
            repeat,
            options.seed,
            &workspace_dir,
        )
        .await?;
        print_results_table(&report, suite);
        save_results_json(
            &mut report,
            options.output_path.as_deref(),
            BenchmarkArtifactKind::SingleRun,
        )
        .await?;
        return Ok(report);
    }

    let mut raw_reports = Vec::with_capacity(repeat);
    let mut raw_report_paths = Vec::with_capacity(repeat);
    for run_index in 1..=repeat {
        eprintln!("\n[benchmark] Repeat {}/{}", run_index, repeat);
        let mut report = run_benchmark_once(
            &specs,
            &all_tasks,
            max_turns,
            &judge_specs,
            suite,
            task_filter,
            &options.prompt,
            run_index,
            repeat,
            options.seed,
            &workspace_dir,
        )
        .await?;
        let path = save_results_json(&mut report, None, BenchmarkArtifactKind::RawRun).await?;
        raw_report_paths.push(path.display().to_string());
        raw_reports.push(report);
    }

    let mut aggregate_report = build_repeat_aggregate_report(
        &raw_reports,
        &specs,
        suite,
        task_filter,
        &options.prompt,
        options.seed,
        raw_report_paths,
    );
    print_repeat_summary(&aggregate_report, suite);
    save_results_json(
        &mut aggregate_report,
        options.output_path.as_deref(),
        BenchmarkArtifactKind::Aggregate,
    )
    .await?;

    Ok(aggregate_report)
}

#[derive(Debug, Clone, Copy)]
enum BenchmarkArtifactKind {
    SingleRun,
    RawRun,
    Aggregate,
}

fn benchmark_workspace_dir() -> PathBuf {
    ava_config::benchmark_workspace_dir().unwrap_or_else(|_| {
        dirs::cache_dir()
            .unwrap_or_default()
            .join("ava")
            .join("benchmarks")
            .join("workspace")
    })
}

fn build_task_list(
    workspace_dir: &Path,
    tasks: Option<Vec<BenchmarkTask>>,
    imported_tasks: Vec<BenchmarkTask>,
    suite: BenchmarkSuite,
    language_filter: Option<&Vec<Language>>,
    task_filter: Option<&str>,
) -> Result<Vec<BenchmarkTask>> {
    let mut all_tasks = tasks.unwrap_or_else(default_tasks);
    all_tasks.extend(agentic_tasks(workspace_dir));
    all_tasks.extend(agent_quality_tasks(workspace_dir));
    all_tasks.extend(python_tasks());
    all_tasks.extend(typescript_tasks());
    all_tasks.extend(go_tasks());
    all_tasks.extend(security_tasks(workspace_dir));
    all_tasks.extend(test_generation_tasks());
    all_tasks.extend(advanced_rust_tasks());
    all_tasks.extend(multi_file_tasks(workspace_dir));
    all_tasks.extend(tool_reliability_tasks(workspace_dir));
    all_tasks.extend(normal_coding_tasks(workspace_dir));
    all_tasks.extend(small_coding_tasks(workspace_dir));
    all_tasks.extend(stress_coding_tasks(workspace_dir));
    all_tasks.extend(large_project_tasks(workspace_dir));
    all_tasks.extend(test_heavy_tasks(workspace_dir));
    all_tasks.extend(maintenance_tasks(workspace_dir));
    all_tasks.extend(tool_recovery_tasks(workspace_dir));
    all_tasks.extend(product_smoke_tasks(workspace_dir));
    all_tasks.extend(mcp_integration_tasks(workspace_dir));
    all_tasks.extend(lsp_smoke_tasks(workspace_dir));
    all_tasks.extend(prompt_regression_tasks(workspace_dir));

    if !imported_tasks.is_empty() {
        eprintln!("[benchmark] Adding {} imported tasks", imported_tasks.len());
        all_tasks.extend(imported_tasks);
    }

    all_tasks = filter_tasks_by_suite(all_tasks, suite);
    all_tasks = filter_tasks_by_name(all_tasks, task_filter);

    if let Some(langs) = language_filter {
        all_tasks.retain(|task| langs.contains(&task.language()));
    }

    if all_tasks.is_empty() {
        return Err(eyre!(
            "No benchmark tasks matched the current suite/language/task filter selection"
        ));
    }

    Ok(all_tasks)
}

async fn run_benchmark_once(
    specs: &[ModelSpec],
    all_tasks: &[BenchmarkTask],
    max_turns: usize,
    judge_specs: &[ModelSpec],
    suite: BenchmarkSuite,
    task_filter: Option<&str>,
    prompt: &BenchmarkPromptConfig,
    run_index: usize,
    run_count: usize,
    seed: Option<u64>,
    workspace_dir: &Path,
) -> Result<BenchmarkReport> {
    tokio::fs::create_dir_all(workspace_dir)
        .await
        .map_err(|e| eyre!("Failed to create benchmark workspace: {}", e))?;
    prepare_benchmark_workspace(workspace_dir).await?;
    let _workspace_guard = BenchmarkWorkspaceGuard::activate(workspace_dir);

    let project_cargo = std::env::current_dir()
        .unwrap_or_default()
        .join("Cargo.toml");
    if project_cargo.exists() {
        let dest = workspace_dir.join("Cargo.toml");
        tokio::fs::copy(&project_cargo, &dest)
            .await
            .map_err(|e| eyre!("Failed to copy Cargo.toml to workspace: {}", e))?;
    }

    let mut results = Vec::new();
    let total_runs = all_tasks.len() * specs.len();
    let mut task_idx = 0;

    for task in all_tasks {
        for spec in specs {
            task_idx += 1;
            eprintln!(
                "\n[benchmark {}/{}] task={} model={}",
                task_idx,
                total_runs,
                task.name,
                short_model_name(&spec.model),
            );

            if let Some(ref harness) = task.test_harness {
                if let Some(setup_code) = harness.setup_code {
                    setup_agentic_file(workspace_dir, task.name, setup_code).await?;
                }
            }

            let result =
                run_single_task(task, spec, max_turns, workspace_dir, prompt, run_index).await;
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
                        "  => {}: {:.1}s, {:.1} wall tok/s, ${:.4}, {}{}{}",
                        status,
                        r.total_time_ms as f64 / 1000.0,
                        r.tokens_per_second,
                        r.cost_usd,
                        r.quality_details,
                        compile_info,
                        agent_info,
                    );
                    results.push(r);
                }
                Err(e) => {
                    eprintln!("  => ERROR: {}", e);
                    results.push(make_error_result(
                        task,
                        spec,
                        prompt,
                        run_index,
                        &e.to_string(),
                    ));
                }
            }
        }
    }

    if !judge_specs.is_empty() {
        eprintln!(
            "\n[benchmark] Running LLM-as-Judge evaluation with {} judge(s)...",
            judge_specs.len()
        );
        judge_outputs(&mut results, judge_specs, all_tasks).await;
    }

    Ok(build_report_from_results(
        results,
        specs,
        suite,
        task_filter,
        prompt,
        Some(run_index),
        run_count,
        seed,
        None,
        None,
    ))
}

fn build_report_from_results(
    results: Vec<BenchmarkResult>,
    specs: &[ModelSpec],
    suite: BenchmarkSuite,
    task_filter: Option<&str>,
    prompt: &BenchmarkPromptConfig,
    run_index: Option<usize>,
    run_count: usize,
    seed: Option<u64>,
    repeat_summary: Option<BenchmarkRepeatSummary>,
    raw_report_paths: Option<Vec<String>>,
) -> BenchmarkReport {
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

    let reliability_scores: Vec<f64> = results
        .iter()
        .filter_map(|r| r.tool_reliability_score)
        .collect();
    let aggregate_tool_reliability = if !reliability_scores.is_empty() {
        Some(reliability_scores.iter().sum::<f64>() / reliability_scores.len() as f64)
    } else {
        None
    };

    let delegation_scores: Vec<f64> = results
        .iter()
        .filter_map(|r| r.delegation_efficiency_score)
        .collect();
    let aggregate_delegation_efficiency = if !delegation_scores.is_empty() {
        Some(delegation_scores.iter().sum::<f64>() / delegation_scores.len() as f64)
    } else {
        None
    };

    let delegation_quality_scores: Vec<f64> = results
        .iter()
        .filter_map(|r| r.delegation_quality_score)
        .collect();
    let aggregate_delegation_quality = if !delegation_quality_scores.is_empty() {
        Some(delegation_quality_scores.iter().sum::<f64>() / delegation_quality_scores.len() as f64)
    } else {
        None
    };

    let score_inputs: Vec<ScoreInput> = results.iter().map(score_input_from_result).collect();
    let score_summary = Some(compute_aggregate_summary(&score_inputs));

    BenchmarkReport {
        schema_version: BENCHMARK_REPORT_SCHEMA_VERSION,
        binary_version: Some(env!("CARGO_PKG_VERSION").to_string()),
        binary_commit: option_env!("VERGEN_GIT_SHA").map(str::to_string),
        suite_name: Some(suite.to_string()),
        task_filter: task_filter
            .map(str::trim)
            .filter(|value| !value.is_empty())
            .map(str::to_string),
        provider: single_value(specs.iter().map(|spec| spec.provider.as_str())),
        model: single_value(specs.iter().map(|spec| spec.model.as_str())),
        prompt: BenchmarkPromptConfig {
            family: prompt.family.clone(),
            variant: prompt.variant.clone(),
            file_path: prompt.file_path.clone(),
            version: prompt.version.clone(),
            hash: prompt.hash.clone(),
            file_contents: None,
        },
        run_count,
        run_index,
        run_seed: seed,
        runner_mode: Some("benchmark".to_string()),
        timestamp: chrono::Utc::now().to_rfc3339(),
        results,
        score_summary,
        repeat_summary,
        raw_report_paths: raw_report_paths.unwrap_or_default(),
        saved_path: None,
        aggregate_cost_per_resolved,
        aggregate_tool_efficiency,
        aggregate_tool_reliability,
        aggregate_delegation_efficiency,
        aggregate_delegation_quality,
    }
}

fn build_repeat_aggregate_report(
    raw_reports: &[BenchmarkReport],
    specs: &[ModelSpec],
    suite: BenchmarkSuite,
    task_filter: Option<&str>,
    prompt: &BenchmarkPromptConfig,
    seed: Option<u64>,
    raw_report_paths: Vec<String>,
) -> BenchmarkReport {
    let flattened_results: Vec<BenchmarkResult> = raw_reports
        .iter()
        .flat_map(|report| report.results.clone())
        .collect();
    let repeat_summary = Some(build_repeat_summary(&flattened_results, raw_reports.len()));
    build_report_from_results(
        flattened_results,
        specs,
        suite,
        task_filter,
        prompt,
        None,
        raw_reports.len(),
        seed,
        repeat_summary,
        Some(raw_report_paths),
    )
}

/// Run a single task against a single model, collecting metrics.
async fn run_single_task(
    task: &BenchmarkTask,
    spec: &ModelSpec,
    max_turns: usize,
    workspace_dir: &Path,
    prompt: &BenchmarkPromptConfig,
    run_index: usize,
) -> Result<BenchmarkResult> {
    let data_dir = ava_config::data_dir().unwrap_or_default();

    let effective_turns = if task.needs_tools { max_turns } else { 3 };

    let (stack, question_rx, approval_rx, _plan_rx) =
        AgentStack::new(AgentStackConfig::for_benchmark(
            data_dir,
            spec.provider.clone(),
            spec.model.clone(),
            effective_turns,
            workspace_dir.to_path_buf(),
            prompt.agent_override(),
        ))
        .await?;
    spawn_default_question_responses(question_rx);
    spawn_auto_approve_requests(approval_rx);

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
                None,
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
    let mut tool_error_count: usize = 0;
    let mut tool_error_breakdown: HashMap<String, usize> = HashMap::new();
    let mut turns_used: usize = 0;
    let mut self_corrections: usize = 0;
    let mut subagent_calls_count: usize = 0;
    let mut subagent_types: Vec<String> = Vec::new();
    let mut subagent_providers: Vec<String> = Vec::new();
    let mut subagent_cost_usd: f64 = 0.0;
    let mut resumed_subagent_calls_count: usize = 0;
    let mut last_tool_was_error = false;
    let mut in_assistant_turn = false;
    let mut tool_call_names: HashMap<String, String> = HashMap::new();

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
                accumulate_token_usage(
                    &mut input_tokens,
                    &mut output_tokens,
                    &mut cost_usd,
                    it,
                    ot,
                    c,
                );
                // TokenUsage marks the end of an assistant response; reset turn tracking
                in_assistant_turn = false;
            }
            AgentEvent::ToolCall(tc) => {
                tool_calls_count += 1;
                tool_calls_detail.push(tc.name.clone());
                tool_call_names.insert(tc.id.clone(), tc.name.clone());
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
                if tr.is_error {
                    tool_error_count += 1;
                    let tool_name = tool_call_names
                        .get(&tr.call_id)
                        .cloned()
                        .unwrap_or_else(|| "unknown".to_string());
                    *tool_error_breakdown.entry(tool_name).or_insert(0) += 1;
                }
            }
            AgentEvent::SubAgentComplete {
                description,
                input_tokens: sub_input_tokens,
                output_tokens: sub_output_tokens,
                cost_usd: sub_cost,
                provider,
                resumed,
                ..
            } => {
                subagent_calls_count += 1;
                subagent_types.push(subagent_type_from_description(&description));
                if let Some(provider) = provider {
                    subagent_providers.push(provider);
                }
                if resumed {
                    resumed_subagent_calls_count += 1;
                }
                accumulate_subagent_usage(
                    &mut input_tokens,
                    &mut output_tokens,
                    &mut cost_usd,
                    &mut subagent_cost_usd,
                    sub_input_tokens,
                    sub_output_tokens,
                    sub_cost,
                );
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
    let (pattern_pass, mut quality_details) =
        check_quality(&total_output, &tool_calls_detail, &task.expected_patterns);

    let tokens_per_second = wall_clock_tokens_per_second(output_tokens, total_time_ms);
    let generation_tps = generation_tokens_per_second(output_tokens, total_time_ms, ttft);

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

    let validation_pass = validation_pass(compile_success, tests_passed, tests_total);
    let quality_pass = if task.test_harness.is_some() {
        pattern_pass && validation_pass.unwrap_or(false)
    } else {
        pattern_pass
    };

    if task.test_harness.is_some() {
        if let Some(false) = validation_pass {
            let validation_details = format_validation_failure_details(
                compile_success,
                tests_passed,
                tests_total,
                compile_error.as_deref(),
            );
            quality_details.push_str(&format!("; {validation_details}"));
        } else if !pattern_pass {
            quality_details.push_str("; validation passed");
        }
    }

    // Determine if the task passed using the full quality signal for code tasks.
    let task_passed = quality_pass;

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

    let tool_reliability_score = if task.category.to_string() == "tool_reliability" {
        if tool_calls_count > 0 {
            let success_ratio = 1.0 - (tool_error_count as f64 / tool_calls_count as f64);
            let efficiency = tool_efficiency_score.unwrap_or(1.0).min(1.0);
            let completion = if task_passed { 1.0 } else { 0.0 };
            Some((success_ratio.max(0.0) * 0.7) + (efficiency * 0.2) + (completion * 0.1))
        } else {
            Some(0.0)
        }
    } else {
        None
    };

    let delegation_efficiency_score = if let Some(min) = expected_min_subagents(task.name) {
        if subagent_calls_count > 0 {
            Some(min as f64 / subagent_calls_count as f64)
        } else {
            Some(0.0)
        }
    } else {
        None
    };

    let delegation_quality_score = compute_delegation_quality_score(
        quality_pass,
        compile_success,
        None,
        subagent_calls_count,
        resumed_subagent_calls_count,
        subagent_cost_usd,
        cost_usd,
    );

    // consistency_hash: hash of the output for variance tracking across runs
    let consistency_hash = if !total_output.trim().is_empty() {
        Some(stable_hash_hex(&total_output))
    } else {
        None
    };

    Ok(BenchmarkResult {
        task_name: task.name.to_string(),
        task_category: task.category.to_string(),
        provider: spec.provider.clone(),
        model: spec.model.clone(),
        prompt_family: Some(resolved_prompt_family(
            &spec.model,
            prompt.family.as_deref(),
        )),
        prompt_variant: prompt.variant.clone(),
        prompt_hash: prompt.hash.clone(),
        run_index: Some(run_index),
        ttft_ms: ttft,
        total_time_ms,
        input_tokens,
        output_tokens,
        tokens_per_second,
        generation_tps,
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
        tool_error_count,
        tool_error_breakdown,
        turns_used,
        self_corrections,
        subagent_calls_count,
        subagent_types,
        subagent_providers,
        subagent_cost_usd,
        resumed_subagent_calls_count,
        raw_output: Some(total_output),
        cost_per_task_usd,
        tool_efficiency_score,
        tool_reliability_score,
        delegation_efficiency_score,
        delegation_quality_score,
        consistency_hash,
    })
}

fn make_error_result(
    task: &BenchmarkTask,
    spec: &ModelSpec,
    prompt: &BenchmarkPromptConfig,
    run_index: usize,
    error: &str,
) -> BenchmarkResult {
    BenchmarkResult {
        task_name: task.name.to_string(),
        task_category: task.category.to_string(),
        provider: spec.provider.clone(),
        model: spec.model.clone(),
        prompt_family: Some(resolved_prompt_family(
            &spec.model,
            prompt.family.as_deref(),
        )),
        prompt_variant: prompt.variant.clone(),
        prompt_hash: prompt.hash.clone(),
        run_index: Some(run_index),
        ttft_ms: None,
        total_time_ms: 0,
        input_tokens: 0,
        output_tokens: 0,
        tokens_per_second: 0.0,
        generation_tps: None,
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
        tool_error_count: 0,
        tool_error_breakdown: HashMap::new(),
        turns_used: 0,
        self_corrections: 0,
        subagent_calls_count: 0,
        subagent_types: Vec::new(),
        subagent_providers: Vec::new(),
        subagent_cost_usd: 0.0,
        resumed_subagent_calls_count: 0,
        raw_output: None,
        cost_per_task_usd: None,
        tool_efficiency_score: None,
        tool_reliability_score: None,
        delegation_efficiency_score: None,
        delegation_quality_score: None,
        consistency_hash: None,
    }
}

fn score_input_from_result(result: &BenchmarkResult) -> ScoreInput {
    let validation_ok = validation_pass(
        result.compile_success,
        result.tests_passed,
        result.tests_total,
    )
    .unwrap_or(result.quality_pass);
    ScoreInput {
        task_pass: validation_ok && result.error.is_none(),
        quality_pass: result.quality_pass,
        compile_success: result.compile_success,
        tests_passed: result.tests_passed,
        tests_total: result.tests_total,
        cost_usd: result.cost_usd,
        total_time_ms: result.total_time_ms,
    }
}

fn build_repeat_summary(
    results: &[BenchmarkResult],
    repeat_count: usize,
) -> BenchmarkRepeatSummary {
    let mut grouped: BTreeMap<(String, String, String), Vec<&BenchmarkResult>> = BTreeMap::new();
    for result in results {
        grouped
            .entry((
                result.task_name.clone(),
                result.provider.clone(),
                result.model.clone(),
            ))
            .or_default()
            .push(result);
    }

    let mut worst_task_variance_ms = 0;
    let mut task_summaries = Vec::with_capacity(grouped.len());
    for ((_task_name, _provider, _model), group) in grouped {
        let mut durations: Vec<u64> = group.iter().map(|result| result.total_time_ms).collect();
        let mut tool_counts: Vec<usize> =
            group.iter().map(|result| result.tool_calls_count).collect();
        let mut subagent_counts: Vec<usize> = group
            .iter()
            .map(|result| result.subagent_calls_count)
            .collect();
        durations.sort_unstable();
        tool_counts.sort_unstable();
        subagent_counts.sort_unstable();

        let min_time = durations.first().copied().unwrap_or(0);
        let max_time = durations.last().copied().unwrap_or(0);
        worst_task_variance_ms = worst_task_variance_ms.max(max_time.saturating_sub(min_time));

        let attempts = group.len();
        let passes = group.iter().filter(|result| result.quality_pass).count();
        let failures = attempts.saturating_sub(passes);
        let compile_total = group
            .iter()
            .filter(|result| result.compile_success.is_some())
            .count();
        let compile_passes = group
            .iter()
            .filter(|result| result.compile_success == Some(true))
            .count();
        let average_cost_usd = if attempts > 0 {
            group.iter().map(|result| result.cost_usd).sum::<f64>() / attempts as f64
        } else {
            0.0
        };

        let first = group[0];
        task_summaries.push(BenchmarkRepeatTaskSummary {
            task_name: first.task_name.clone(),
            task_category: first.task_category.clone(),
            provider: first.provider.clone(),
            model: first.model.clone(),
            prompt_family: first.prompt_family.clone(),
            prompt_variant: first.prompt_variant.clone(),
            prompt_hash: first.prompt_hash.clone(),
            attempts,
            passes,
            failures,
            pass_rate: if attempts > 0 {
                passes as f64 / attempts as f64
            } else {
                0.0
            },
            compile_pass_rate: if compile_total > 0 {
                Some(compile_passes as f64 / compile_total as f64)
            } else {
                None
            },
            median_total_time_ms: median_u64(&durations),
            p95_total_time_ms: p95_u64(&durations),
            median_tool_calls_count: median_usize(&tool_counts),
            median_subagent_calls_count: median_usize(&subagent_counts),
            average_cost_usd,
        });
    }

    let mut all_durations: Vec<u64> = results.iter().map(|result| result.total_time_ms).collect();
    all_durations.sort_unstable();
    let overall_passes = results.iter().filter(|result| result.quality_pass).count();

    BenchmarkRepeatSummary {
        repeat_count,
        overall_pass_rate: if results.is_empty() {
            0.0
        } else {
            overall_passes as f64 / results.len() as f64
        },
        median_total_time_ms: median_u64(&all_durations),
        worst_task_variance_ms,
        task_summaries,
    }
}

fn single_value<'a>(mut values: impl Iterator<Item = &'a str>) -> Option<String> {
    let first = values.next()?;
    if values.all(|value| value == first) {
        Some(first.to_string())
    } else {
        None
    }
}

fn stable_hash_hex(value: &str) -> String {
    const FNV_OFFSET_BASIS: u64 = 0xcbf29ce484222325;
    const FNV_PRIME: u64 = 0x100000001b3;

    let mut hash = FNV_OFFSET_BASIS;
    for byte in value.as_bytes() {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(FNV_PRIME);
    }
    format!("{hash:016x}")
}

fn median_u64(values: &[u64]) -> u64 {
    if values.is_empty() {
        0
    } else {
        values[values.len() / 2]
    }
}

fn median_usize(values: &[usize]) -> usize {
    if values.is_empty() {
        0
    } else {
        values[values.len() / 2]
    }
}

fn p95_u64(values: &[u64]) -> u64 {
    if values.is_empty() {
        return 0;
    }
    let idx = ((values.len() - 1) * 95) / 100;
    values[idx]
}

// ---------------------------------------------------------------------------
// Output formatting
// ---------------------------------------------------------------------------

/// Check output quality against expected regex patterns.
fn check_quality(
    output: &str,
    tool_calls: &[String],
    expected_patterns: &[&str],
) -> (bool, String) {
    if output.trim().is_empty() {
        return (false, "empty output".to_string());
    }

    let mut passed = 0;
    let total = expected_patterns.len();
    let mut missing = Vec::new();

    for pattern in expected_patterns {
        if let Some(tool_name) = pattern.strip_prefix("tool:") {
            if tool_calls.iter().any(|call| call == tool_name) {
                passed += 1;
            } else {
                missing.push(*pattern);
            }
            continue;
        }

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

fn validation_pass(
    compile_success: Option<bool>,
    tests_passed: Option<usize>,
    tests_total: Option<usize>,
) -> Option<bool> {
    match (compile_success, tests_passed, tests_total) {
        (Some(false), _, _) => Some(false),
        (Some(true), Some(passed), Some(total)) => Some(passed == total),
        (Some(true), None, None) => Some(true),
        (Some(true), _, _) => Some(false),
        (None, _, _) => None,
    }
}

fn format_validation_failure_details(
    compile_success: Option<bool>,
    tests_passed: Option<usize>,
    tests_total: Option<usize>,
    compile_error: Option<&str>,
) -> String {
    match (compile_success, tests_passed, tests_total) {
        (Some(false), _, _) => {
            if let Some(error) = compile_error {
                format!(
                    "compile/test validation failed: {}",
                    summarize_validation_error(error)
                )
            } else {
                "compile/test validation failed".to_string()
            }
        }
        (Some(true), Some(passed), Some(total)) if passed != total => {
            format!("tests failed ({passed}/{total} passed)")
        }
        _ => "compile/test validation failed".to_string(),
    }
}

fn summarize_validation_error(error: &str) -> String {
    error
        .lines()
        .find(|line| !line.trim().is_empty())
        .map(|line| line.trim().to_string())
        .unwrap_or_else(|| "unknown validation error".to_string())
}

/// Save the full results as JSON to AVA's XDG cache benchmark directory.
async fn save_results_json(
    report: &mut BenchmarkReport,
    output_path: Option<&Path>,
    artifact_kind: BenchmarkArtifactKind,
) -> Result<PathBuf> {
    let path = if let Some(path) = output_path {
        path.to_path_buf()
    } else {
        default_report_path(report, artifact_kind)
    };

    if let Some(parent) = path.parent() {
        tokio::fs::create_dir_all(parent).await.map_err(|e| {
            eyre!(
                "Failed to create benchmark output dir {}: {}",
                parent.display(),
                e
            )
        })?;
    }

    report.saved_path = Some(path.display().to_string());

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

    Ok(path)
}

fn default_report_path(report: &BenchmarkReport, artifact_kind: BenchmarkArtifactKind) -> PathBuf {
    let benchmarks_dir = ava_config::benchmarks_dir().unwrap_or_else(|_| {
        dirs::cache_dir()
            .unwrap_or_default()
            .join("ava")
            .join("benchmarks")
    });
    let date = report.timestamp.get(..10).unwrap_or("unknown-date");
    let suite = sanitize_path_segment(report.suite_name.as_deref().unwrap_or("benchmark"));
    let provider = sanitize_path_segment(report.provider.as_deref().unwrap_or("mixed-provider"));
    let model = sanitize_path_segment(report.model.as_deref().unwrap_or("mixed-model"));
    let family = sanitize_path_segment(report.prompt.family.as_deref().unwrap_or("auto"));
    let variant = sanitize_path_segment(report.prompt.variant.as_deref().unwrap_or("default"));

    let mut base_name = format!("{}-{}-{}-{}-{}", suite, provider, model, family, variant);
    if let Some(run_index) = report.run_index {
        base_name.push_str(&format!("-run{}", run_index));
    }

    match artifact_kind {
        BenchmarkArtifactKind::SingleRun => benchmarks_dir.join(format!("{}.json", base_name)),
        BenchmarkArtifactKind::RawRun => benchmarks_dir
            .join("raw")
            .join(date)
            .join(format!("{}.json", base_name)),
        BenchmarkArtifactKind::Aggregate => benchmarks_dir
            .join("aggregate")
            .join(date)
            .join(format!("{}-summary.json", base_name)),
    }
}

fn sanitize_path_segment(value: &str) -> String {
    value
        .chars()
        .map(|ch| match ch {
            'a'..='z' | 'A'..='Z' | '0'..='9' | '-' | '_' => ch,
            '/' | ':' | '.' => '-',
            _ => '-',
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_agent::AgentEvent;
    use ava_types::{Message, Role, Session};

    #[test]
    fn test_parse_test_output() {
        let output = "test result: ok. 3 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out";
        let (passed, failed) = crate::benchmark_support::parse_test_output(output);
        assert_eq!(passed, 3);
        assert_eq!(failed, 0);
    }

    #[test]
    fn test_parse_test_output_with_failures() {
        let output = "test result: FAILED. 2 passed; 1 failed; 0 ignored";
        let (passed, failed) = crate::benchmark_support::parse_test_output(output);
        assert_eq!(passed, 2);
        assert_eq!(failed, 1);
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

    #[test]
    fn stable_hash_hex_is_pinned() {
        assert_eq!(stable_hash_hex("benchmark-prompt-hash"), "3038090fbdbbbbf4");
    }

    #[test]
    fn check_quality_accepts_tool_patterns() {
        let tool_calls = vec!["bash".to_string(), "read".to_string()];
        let (pass, details) = check_quality("verified and done", &tool_calls, &["tool:bash"]);
        assert!(pass, "details: {details}");
    }

    #[test]
    fn score_input_requires_test_success_for_code_tasks() {
        let result = BenchmarkResult {
            task_name: "code_task".to_string(),
            task_category: "prompt_regression".to_string(),
            provider: "test".to_string(),
            model: "test-model".to_string(),
            prompt_family: None,
            prompt_variant: None,
            prompt_hash: None,
            run_index: None,
            ttft_ms: None,
            total_time_ms: 100,
            input_tokens: 10,
            output_tokens: 10,
            tokens_per_second: 1.0,
            generation_tps: None,
            cost_usd: 0.0,
            quality_pass: false,
            quality_details: "tests failed".to_string(),
            error: None,
            compile_success: Some(true),
            tests_passed: Some(0),
            tests_total: Some(2),
            compile_error: Some("tests failed".to_string()),
            judge_scores: None,
            tool_calls_count: 0,
            tool_calls_detail: Vec::new(),
            tool_error_count: 0,
            tool_error_breakdown: std::collections::HashMap::new(),
            turns_used: 0,
            self_corrections: 0,
            subagent_calls_count: 0,
            subagent_types: Vec::new(),
            subagent_providers: Vec::new(),
            subagent_cost_usd: 0.0,
            resumed_subagent_calls_count: 0,
            raw_output: None,
            cost_per_task_usd: None,
            tool_efficiency_score: None,
            tool_reliability_score: None,
            delegation_efficiency_score: None,
            delegation_quality_score: None,
            consistency_hash: None,
        };

        assert!(!score_input_from_result(&result).task_pass);
    }

    #[test]
    fn validation_pass_allows_compile_only_success() {
        assert_eq!(validation_pass(Some(true), None, None), Some(true));
    }

    #[test]
    fn wall_clock_tps_uses_inclusive_output_tokens() {
        assert_eq!(wall_clock_tokens_per_second(130, 1000), 130.0);
        assert_eq!(wall_clock_tokens_per_second(130, 0), 0.0);
    }

    #[test]
    fn generation_tps_uses_post_ttft_window() {
        assert_eq!(
            generation_tokens_per_second(130, 2000, Some(1000)),
            Some(130.0)
        );
        assert_eq!(generation_tokens_per_second(130, 1000, Some(1000)), None);
        assert_eq!(generation_tokens_per_second(130, 1000, None), None);
    }

    #[test]
    fn subagent_usage_counts_toward_benchmark_totals() {
        let mut input_tokens = 0;
        let mut output_tokens = 0;
        let mut cost_usd = 0.0;
        let mut subagent_cost_usd = 0.0;

        let events = vec![
            AgentEvent::TokenUsage {
                input_tokens: 100,
                output_tokens: 50,
                cost_usd: 0.01,
            },
            AgentEvent::SubAgentComplete {
                call_id: "call-1".to_string(),
                session_id: "session-1".to_string(),
                messages: vec![Message::new(Role::Assistant, "child")],
                description: "Review code".to_string(),
                input_tokens: 200,
                output_tokens: 80,
                cost_usd: 0.02,
                agent_type: Some("reviewer".to_string()),
                provider: Some("test-provider".to_string()),
                resumed: false,
            },
            AgentEvent::Complete(Session::new()),
        ];

        for event in events {
            match event {
                AgentEvent::TokenUsage {
                    input_tokens: it,
                    output_tokens: ot,
                    cost_usd: c,
                } => accumulate_token_usage(
                    &mut input_tokens,
                    &mut output_tokens,
                    &mut cost_usd,
                    it,
                    ot,
                    c,
                ),
                AgentEvent::SubAgentComplete {
                    input_tokens: sub_it,
                    output_tokens: sub_ot,
                    cost_usd: sub_cost,
                    ..
                } => accumulate_subagent_usage(
                    &mut input_tokens,
                    &mut output_tokens,
                    &mut cost_usd,
                    &mut subagent_cost_usd,
                    sub_it,
                    sub_ot,
                    sub_cost,
                ),
                AgentEvent::Complete(_) => break,
                _ => {}
            }
        }

        assert_eq!(input_tokens, 300);
        assert_eq!(output_tokens, 130);
        assert!((cost_usd - 0.03).abs() < 1e-9);
        assert!((subagent_cost_usd - 0.02).abs() < 1e-9);
        assert_eq!(wall_clock_tokens_per_second(output_tokens, 1000), 130.0);
    }
}
