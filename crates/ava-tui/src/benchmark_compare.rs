use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

use color_eyre::eyre::{eyre, Result};
use serde::{Deserialize, Serialize};

use crate::benchmark::{BenchmarkRepeatTaskSummary, BenchmarkReport, BenchmarkResult};
use crate::benchmark_format::short_model_name;
use crate::benchmark_reporting::{compute_aggregate_summary, AggregateScoreSummary, ScoreInput};

pub const COMPARISON_SCHEMA_VERSION: u32 = 1;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ComparisonSideSummary {
    pub label: String,
    pub report_task_count: usize,
    pub aligned_task_count: usize,
    pub quality_pass_count: usize,
    pub compile_pass_count: usize,
    pub total_time_ms: u64,
    pub total_cost_usd: f64,
    pub aggregate: AggregateScoreSummary,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ComparisonSavings {
    /// Positive means the left side saved this much time versus right.
    pub time_ms_saved_by_left: i64,
    /// Positive means the left side saved this much cost versus right.
    pub cost_usd_saved_by_left: f64,
    pub time_pct_saved_by_left: f64,
    pub cost_pct_saved_by_left: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SideWinner {
    Left,
    Right,
    Tie,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TaskComparisonRow {
    pub task_name: String,
    pub left_quality_pass: bool,
    pub right_quality_pass: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub left_quality_pass_rate: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub right_quality_pass_rate: Option<f64>,
    pub left_time_ms: u64,
    pub right_time_ms: u64,
    pub left_cost_usd: f64,
    pub right_cost_usd: f64,
    pub quality_winner: SideWinner,
    pub time_winner: SideWinner,
    pub cost_winner: SideWinner,
    pub overall_winner: SideWinner,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct ComparisonWinCounts {
    pub left_quality_wins: usize,
    pub right_quality_wins: usize,
    pub tied_quality: usize,
    pub left_time_wins: usize,
    pub right_time_wins: usize,
    pub tied_time: usize,
    pub left_cost_wins: usize,
    pub right_cost_wins: usize,
    pub tied_cost: usize,
    pub left_overall_wins: usize,
    pub right_overall_wins: usize,
    pub tied_overall: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct BenchmarkComparisonReport {
    pub schema_version: u32,
    pub generated_at: String,
    pub left_label: String,
    pub right_label: String,
    pub left_report_path: String,
    pub right_report_path: String,
    pub aligned_task_count: usize,
    pub left_only_tasks: Vec<String>,
    pub right_only_tasks: Vec<String>,
    pub left_summary: ComparisonSideSummary,
    pub right_summary: ComparisonSideSummary,
    pub savings: ComparisonSavings,
    pub win_counts: ComparisonWinCounts,
    pub task_rows: Vec<TaskComparisonRow>,
}

pub async fn run_benchmark_report_comparison(
    left_report_path: &Path,
    right_report_path: &Path,
    output_path: Option<&Path>,
) -> Result<BenchmarkComparisonReport> {
    let left_raw = tokio::fs::read_to_string(left_report_path)
        .await
        .map_err(|e| {
            eyre!(
                "Failed to read benchmark report {}: {}",
                left_report_path.display(),
                e
            )
        })?;
    let right_raw = tokio::fs::read_to_string(right_report_path)
        .await
        .map_err(|e| {
            eyre!(
                "Failed to read benchmark report {}: {}",
                right_report_path.display(),
                e
            )
        })?;

    let left_report: BenchmarkReport = serde_json::from_str(&left_raw)
        .map_err(|e| eyre!("Failed to parse benchmark report JSON: {}", e))?;
    let right_report: BenchmarkReport = serde_json::from_str(&right_raw)
        .map_err(|e| eyre!("Failed to parse benchmark report JSON: {}", e))?;

    let comparison = compare_reports(
        &left_report,
        &right_report,
        &default_report_label(&left_report, "left"),
        &default_report_label(&right_report, "right"),
        left_report_path.display().to_string(),
        right_report_path.display().to_string(),
    )?;

    print_comparison_summary(&comparison);

    if let Some(path) = output_path {
        let json = serde_json::to_string_pretty(&comparison)
            .map_err(|e| eyre!("Failed to serialize comparison JSON: {}", e))?;
        if let Some(parent) = path.parent() {
            tokio::fs::create_dir_all(parent).await.map_err(|e| {
                eyre!(
                    "Failed to create comparison output directory {}: {}",
                    parent.display(),
                    e
                )
            })?;
        }
        tokio::fs::write(path, json).await.map_err(|e| {
            eyre!(
                "Failed to write comparison JSON to {}: {}",
                path.display(),
                e
            )
        })?;
        println!("Saved comparison JSON: {}", path.display());
    }

    Ok(comparison)
}

pub async fn run_ava_vs_opencode_comparison(
    ava_report_path: &Path,
    opencode_report_path: &Path,
    output_path: Option<&Path>,
) -> Result<BenchmarkComparisonReport> {
    run_benchmark_report_comparison(ava_report_path, opencode_report_path, output_path).await
}

fn compare_reports(
    left_report: &BenchmarkReport,
    right_report: &BenchmarkReport,
    left_label: &str,
    right_label: &str,
    left_report_path: String,
    right_report_path: String,
) -> Result<BenchmarkComparisonReport> {
    let left_by_task = comparable_results_by_task_name(left_report);
    let right_by_task = comparable_results_by_task_name(right_report);

    let left_tasks: BTreeSet<String> = left_by_task.keys().cloned().collect();
    let right_tasks: BTreeSet<String> = right_by_task.keys().cloned().collect();

    let aligned_tasks: Vec<String> = left_tasks.intersection(&right_tasks).cloned().collect();
    if aligned_tasks.is_empty() {
        return Err(eyre!(
            "No aligned tasks by name between {} and {} reports",
            left_label,
            right_label
        ));
    }

    let left_only_tasks: Vec<String> = left_tasks.difference(&right_tasks).cloned().collect();
    let right_only_tasks: Vec<String> = right_tasks.difference(&left_tasks).cloned().collect();

    let mut left_inputs = Vec::with_capacity(aligned_tasks.len());
    let mut right_inputs = Vec::with_capacity(aligned_tasks.len());
    let mut task_rows = Vec::with_capacity(aligned_tasks.len());

    for task_name in &aligned_tasks {
        let left = left_by_task
            .get(task_name)
            .ok_or_else(|| eyre!("Missing left task {} after alignment", task_name))?;
        let right = right_by_task
            .get(task_name)
            .ok_or_else(|| eyre!("Missing right task {} after alignment", task_name))?;

        left_inputs.push(score_input_from_result(left));
        right_inputs.push(score_input_from_result(right));
        task_rows.push(compare_task_row(task_name.clone(), left, right));
    }

    let left_summary = compute_side_summary(
        left_label,
        report_task_count(left_report),
        &task_rows,
        &left_inputs,
    );
    let right_summary = compute_side_summary(
        right_label,
        report_task_count(right_report),
        &task_rows,
        &right_inputs,
    );

    let savings = compute_savings(&left_summary, &right_summary);
    let win_counts = compute_win_counts(&task_rows);

    Ok(BenchmarkComparisonReport {
        schema_version: COMPARISON_SCHEMA_VERSION,
        generated_at: chrono::Utc::now().to_rfc3339(),
        left_label: left_label.to_string(),
        right_label: right_label.to_string(),
        left_report_path,
        right_report_path,
        aligned_task_count: aligned_tasks.len(),
        left_only_tasks,
        right_only_tasks,
        left_summary,
        right_summary,
        savings,
        win_counts,
        task_rows,
    })
}

#[derive(Debug, Clone)]
struct ComparableTaskMetrics {
    quality_pass: bool,
    quality_pass_rate: Option<f64>,
    total_time_ms: u64,
    cost_usd: f64,
    compile_success: Option<bool>,
    tests_passed: Option<usize>,
    tests_total: Option<usize>,
    had_error: bool,
}

fn comparable_results_by_task_name(
    report: &BenchmarkReport,
) -> BTreeMap<String, ComparableTaskMetrics> {
    let mut map = BTreeMap::new();
    if let Some(repeat_summary) = report.repeat_summary.as_ref() {
        for summary in &repeat_summary.task_summaries {
            map.entry(summary.task_name.clone())
                .or_insert_with(|| comparable_from_repeat_summary(summary));
        }
        return map;
    }

    for result in &report.results {
        map.entry(result.task_name.clone())
            .or_insert_with(|| comparable_from_result(result));
    }
    map
}

fn comparable_from_result(result: &BenchmarkResult) -> ComparableTaskMetrics {
    ComparableTaskMetrics {
        quality_pass: result.quality_pass,
        quality_pass_rate: Some(if result.quality_pass { 1.0 } else { 0.0 }),
        total_time_ms: result.total_time_ms,
        cost_usd: result.cost_usd,
        compile_success: result.compile_success,
        tests_passed: result.tests_passed,
        tests_total: result.tests_total,
        had_error: result.error.is_some(),
    }
}

fn comparable_from_repeat_summary(summary: &BenchmarkRepeatTaskSummary) -> ComparableTaskMetrics {
    ComparableTaskMetrics {
        quality_pass: summary.pass_rate >= 0.5,
        quality_pass_rate: Some(summary.pass_rate),
        total_time_ms: summary.median_total_time_ms,
        cost_usd: summary.average_cost_usd,
        compile_success: summary.compile_pass_rate.map(|value| value >= 0.5),
        tests_passed: None,
        tests_total: None,
        had_error: false,
    }
}

fn report_task_count(report: &BenchmarkReport) -> usize {
    report
        .repeat_summary
        .as_ref()
        .map(|summary| summary.task_summaries.len())
        .unwrap_or_else(|| comparable_results_by_task_name(report).len())
}

fn score_input_from_result(result: &ComparableTaskMetrics) -> ScoreInput {
    let validation_ok = match (
        result.compile_success,
        result.tests_passed,
        result.tests_total,
    ) {
        (Some(false), _, _) => false,
        (Some(true), Some(passed), Some(total)) => passed == total,
        (Some(true), None, None) => true,
        (Some(true), _, _) => false,
        (None, _, _) => result.quality_pass,
    };
    ScoreInput {
        task_pass: validation_ok && !result.had_error,
        quality_pass: result.quality_pass,
        compile_success: result.compile_success,
        tests_passed: result.tests_passed,
        tests_total: result.tests_total,
        cost_usd: result.cost_usd,
        total_time_ms: result.total_time_ms,
    }
}

fn compute_side_summary(
    label: &str,
    report_task_count: usize,
    task_rows: &[TaskComparisonRow],
    inputs: &[ScoreInput],
) -> ComparisonSideSummary {
    let aggregate = compute_aggregate_summary(inputs);
    let quality_pass_count = inputs.iter().filter(|i| i.quality_pass).count();
    let compile_pass_count = inputs
        .iter()
        .filter(|i| i.compile_success == Some(true))
        .count();
    let total_time_ms = inputs.iter().map(|i| i.total_time_ms).sum();
    let total_cost_usd: f64 = inputs.iter().map(|i| i.cost_usd).sum();

    ComparisonSideSummary {
        label: label.to_string(),
        report_task_count,
        aligned_task_count: task_rows.len(),
        quality_pass_count,
        compile_pass_count,
        total_time_ms,
        total_cost_usd,
        aggregate,
    }
}

fn compare_task_row(
    task_name: String,
    left: &ComparableTaskMetrics,
    right: &ComparableTaskMetrics,
) -> TaskComparisonRow {
    let quality_winner = quality_winner(
        left.quality_pass,
        right.quality_pass,
        left.quality_pass_rate,
        right.quality_pass_rate,
    );
    let time_winner = numeric_winner(left.total_time_ms as f64, right.total_time_ms as f64, 1.0);
    let cost_winner = numeric_winner(left.cost_usd, right.cost_usd, 0.0001);

    let overall_winner = if quality_winner != SideWinner::Tie {
        quality_winner.clone()
    } else if time_winner != SideWinner::Tie {
        time_winner.clone()
    } else {
        cost_winner.clone()
    };

    TaskComparisonRow {
        task_name,
        left_quality_pass: left.quality_pass,
        right_quality_pass: right.quality_pass,
        left_quality_pass_rate: left.quality_pass_rate,
        right_quality_pass_rate: right.quality_pass_rate,
        left_time_ms: left.total_time_ms,
        right_time_ms: right.total_time_ms,
        left_cost_usd: left.cost_usd,
        right_cost_usd: right.cost_usd,
        quality_winner,
        time_winner,
        cost_winner,
        overall_winner,
    }
}

fn quality_winner(
    left_pass: bool,
    right_pass: bool,
    left_rate: Option<f64>,
    right_rate: Option<f64>,
) -> SideWinner {
    match (left_rate, right_rate) {
        (Some(left), Some(right)) if (left - right).abs() > 0.0001 => {
            if left > right {
                SideWinner::Left
            } else {
                SideWinner::Right
            }
        }
        _ => bool_winner(left_pass, right_pass),
    }
}

fn bool_winner(left: bool, right: bool) -> SideWinner {
    match (left, right) {
        (true, false) => SideWinner::Left,
        (false, true) => SideWinner::Right,
        _ => SideWinner::Tie,
    }
}

fn numeric_winner(left: f64, right: f64, tolerance: f64) -> SideWinner {
    if (left - right).abs() <= tolerance {
        SideWinner::Tie
    } else if left < right {
        SideWinner::Left
    } else {
        SideWinner::Right
    }
}

fn compute_savings(
    left_summary: &ComparisonSideSummary,
    right_summary: &ComparisonSideSummary,
) -> ComparisonSavings {
    let time_saved = right_summary.total_time_ms as i64 - left_summary.total_time_ms as i64;
    let cost_saved = right_summary.total_cost_usd - left_summary.total_cost_usd;

    let time_pct = if right_summary.total_time_ms == 0 {
        0.0
    } else {
        time_saved as f64 / right_summary.total_time_ms as f64
    };

    let cost_pct = if right_summary.total_cost_usd.abs() < f64::EPSILON {
        0.0
    } else {
        cost_saved / right_summary.total_cost_usd
    };

    ComparisonSavings {
        time_ms_saved_by_left: time_saved,
        cost_usd_saved_by_left: cost_saved,
        time_pct_saved_by_left: time_pct,
        cost_pct_saved_by_left: cost_pct,
    }
}

fn compute_win_counts(rows: &[TaskComparisonRow]) -> ComparisonWinCounts {
    let mut wins = ComparisonWinCounts {
        left_quality_wins: 0,
        right_quality_wins: 0,
        tied_quality: 0,
        left_time_wins: 0,
        right_time_wins: 0,
        tied_time: 0,
        left_cost_wins: 0,
        right_cost_wins: 0,
        tied_cost: 0,
        left_overall_wins: 0,
        right_overall_wins: 0,
        tied_overall: 0,
    };

    for row in rows {
        bump_winner(
            &row.quality_winner,
            &mut wins.left_quality_wins,
            &mut wins.right_quality_wins,
            &mut wins.tied_quality,
        );
        bump_winner(
            &row.time_winner,
            &mut wins.left_time_wins,
            &mut wins.right_time_wins,
            &mut wins.tied_time,
        );
        bump_winner(
            &row.cost_winner,
            &mut wins.left_cost_wins,
            &mut wins.right_cost_wins,
            &mut wins.tied_cost,
        );
        bump_winner(
            &row.overall_winner,
            &mut wins.left_overall_wins,
            &mut wins.right_overall_wins,
            &mut wins.tied_overall,
        );
    }

    wins
}

fn bump_winner(winner: &SideWinner, left: &mut usize, right: &mut usize, tied: &mut usize) {
    match winner {
        SideWinner::Left => *left += 1,
        SideWinner::Right => *right += 1,
        SideWinner::Tie => *tied += 1,
    }
}

fn default_report_label(report: &BenchmarkReport, fallback: &str) -> String {
    let mut parts = Vec::new();
    if let (Some(provider), Some(model)) = (&report.provider, &report.model) {
        parts.push(format!("{}:{}", provider, short_model_name(model)));
    } else if let Some(model) = &report.model {
        parts.push(short_model_name(model));
    }

    match (
        report.prompt.family.as_deref(),
        report.prompt.variant.as_deref(),
    ) {
        (Some(family), Some(variant)) => parts.push(format!("{family}/{variant}")),
        (Some(family), None) => parts.push(family.to_string()),
        (None, Some(variant)) => parts.push(variant.to_string()),
        (None, None) => {}
    }

    if parts.is_empty() {
        fallback.to_string()
    } else {
        parts.join(" | ")
    }
}

fn print_comparison_summary(report: &BenchmarkComparisonReport) {
    println!();
    println!("=======================================================================");
    println!("                Benchmark Comparison Summary");
    println!(
        "                     {}",
        report
            .generated_at
            .get(..19)
            .unwrap_or(&report.generated_at)
    );
    println!("=======================================================================");
    println!(
        "  {} report: {}",
        report.left_label, report.left_report_path
    );
    println!(
        "  {} report: {}",
        report.right_label, report.right_report_path
    );
    println!();
    println!(
        "  Aligned tasks: {} ({}-only: {}, {}-only: {})",
        report.aligned_task_count,
        report.left_label,
        report.left_only_tasks.len(),
        report.right_label,
        report.right_only_tasks.len(),
    );
    println!();
    println!(
        "  {:<24} {:>12} {:>12}",
        "Metric", report.left_label, report.right_label
    );
    println!("  {:-<24} {:-<12} {:-<12}", "", "", "");
    println!(
        "  {:<24} {:>12} {:>12}",
        "Quality pass",
        format!(
            "{}/{}",
            report.left_summary.quality_pass_count, report.left_summary.aligned_task_count
        ),
        format!(
            "{}/{}",
            report.right_summary.quality_pass_count, report.right_summary.aligned_task_count
        ),
    );
    println!(
        "  {:<24} {:>12} {:>12}",
        "Aggregate score",
        format!("{:.3}", report.left_summary.aggregate.aggregate_score),
        format!("{:.3}", report.right_summary.aggregate.aggregate_score),
    );
    println!(
        "  {:<24} {:>12} {:>12}",
        "Total time (s)",
        format!("{:.1}", report.left_summary.total_time_ms as f64 / 1000.0),
        format!("{:.1}", report.right_summary.total_time_ms as f64 / 1000.0),
    );
    println!(
        "  {:<24} {:>12} {:>12}",
        "Total cost (USD)",
        format!("${:.4}", report.left_summary.total_cost_usd),
        format!("${:.4}", report.right_summary.total_cost_usd),
    );
    println!();
    println!(
        "  Savings ({} vs {} on aligned tasks):",
        report.left_label, report.right_label
    );
    println!(
        "    Time: {:+.1}s ({:+.1}%)",
        report.savings.time_ms_saved_by_left as f64 / 1000.0,
        report.savings.time_pct_saved_by_left * 100.0,
    );
    println!(
        "    Cost: {:+.4} USD ({:+.1}%)",
        report.savings.cost_usd_saved_by_left,
        report.savings.cost_pct_saved_by_left * 100.0,
    );
    println!();
    println!("  Win counts:");
    println!(
        "    Quality wins: {} {} / {} {} / {} tied",
        report.left_label,
        report.win_counts.left_quality_wins,
        report.right_label,
        report.win_counts.right_quality_wins,
        report.win_counts.tied_quality,
    );
    println!(
        "    Time wins:    {} {} / {} {} / {} tied",
        report.left_label,
        report.win_counts.left_time_wins,
        report.right_label,
        report.win_counts.right_time_wins,
        report.win_counts.tied_time,
    );
    println!(
        "    Cost wins:    {} {} / {} {} / {} tied",
        report.left_label,
        report.win_counts.left_cost_wins,
        report.right_label,
        report.win_counts.right_cost_wins,
        report.win_counts.tied_cost,
    );
    println!(
        "    Overall wins: {} {} / {} {} / {} tied",
        report.left_label,
        report.win_counts.left_overall_wins,
        report.right_label,
        report.win_counts.right_overall_wins,
        report.win_counts.tied_overall,
    );
    println!("=======================================================================");
    println!();
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::benchmark::BenchmarkReport;
    use std::collections::HashMap;

    fn result(
        task_name: &str,
        quality_pass: bool,
        total_time_ms: u64,
        cost_usd: f64,
    ) -> BenchmarkResult {
        BenchmarkResult {
            task_name: task_name.to_string(),
            task_category: "test".to_string(),
            provider: "test".to_string(),
            model: "test-model".to_string(),
            prompt_family: None,
            prompt_variant: None,
            prompt_hash: None,
            run_index: None,
            ttft_ms: Some(100),
            total_time_ms,
            input_tokens: 10,
            output_tokens: 20,
            tokens_per_second: 5.0,
            cost_usd,
            quality_pass,
            quality_details: "ok".to_string(),
            error: None,
            compile_success: Some(quality_pass),
            tests_passed: Some(if quality_pass { 1 } else { 0 }),
            tests_total: Some(1),
            compile_error: None,
            judge_scores: None,
            tool_calls_count: 0,
            tool_calls_detail: Vec::new(),
            tool_error_count: 0,
            tool_error_breakdown: HashMap::new(),
            turns_used: 1,
            self_corrections: 0,
            subagent_calls_count: 0,
            subagent_types: Vec::new(),
            subagent_providers: Vec::new(),
            subagent_cost_usd: 0.0,
            resumed_subagent_calls_count: 0,
            raw_output: None,
            cost_per_task_usd: Some(cost_usd),
            tool_efficiency_score: None,
            tool_reliability_score: None,
            delegation_efficiency_score: None,
            delegation_quality_score: None,
            consistency_hash: None,
        }
    }

    fn report(results: Vec<BenchmarkResult>) -> BenchmarkReport {
        BenchmarkReport {
            schema_version: 2,
            binary_version: Some(env!("CARGO_PKG_VERSION").to_string()),
            binary_commit: None,
            suite_name: Some("test".to_string()),
            task_filter: None,
            provider: Some("test".to_string()),
            model: Some("test-model".to_string()),
            prompt: crate::benchmark::BenchmarkPromptConfig::default(),
            run_count: 1,
            run_index: None,
            run_seed: None,
            runner_mode: Some("benchmark".to_string()),
            timestamp: "2026-04-08T00:00:00Z".to_string(),
            results,
            score_summary: None,
            repeat_summary: None,
            raw_report_paths: Vec::new(),
            saved_path: None,
            aggregate_cost_per_resolved: None,
            aggregate_tool_efficiency: None,
            aggregate_tool_reliability: None,
            aggregate_delegation_efficiency: None,
            aggregate_delegation_quality: None,
        }
    }

    fn repeat_summary_report(
        task_name: &str,
        pass_rate: f64,
        total_time_ms: u64,
    ) -> BenchmarkReport {
        let mut report = report(Vec::new());
        report.repeat_summary = Some(crate::benchmark::BenchmarkRepeatSummary {
            repeat_count: 3,
            overall_pass_rate: pass_rate,
            median_total_time_ms: total_time_ms,
            worst_task_variance_ms: 250,
            task_summaries: vec![crate::benchmark::BenchmarkRepeatTaskSummary {
                task_name: task_name.to_string(),
                task_category: "test".to_string(),
                provider: "test".to_string(),
                model: "test-model".to_string(),
                prompt_family: Some("gpt".to_string()),
                prompt_variant: Some("candidate".to_string()),
                prompt_hash: Some("abc123".to_string()),
                attempts: 3,
                passes: (pass_rate * 3.0).round() as usize,
                failures: 3 - (pass_rate * 3.0).round() as usize,
                pass_rate,
                compile_pass_rate: Some(pass_rate),
                median_total_time_ms: total_time_ms,
                p95_total_time_ms: total_time_ms + 250,
                median_tool_calls_count: 2,
                median_subagent_calls_count: 0,
                average_cost_usd: 0.10,
            }],
        });
        report
    }

    #[test]
    fn aligns_tasks_and_computes_savings_and_wins() {
        let left = report(vec![
            result("task_a", true, 1_000, 0.10),
            result("task_b", false, 4_000, 0.40),
            result("left_only", true, 2_000, 0.20),
        ]);
        let right = report(vec![
            result("task_a", true, 2_000, 0.20),
            result("task_b", true, 3_000, 0.35),
            result("right_only", true, 1_000, 0.10),
        ]);

        let comparison = compare_reports(
            &left,
            &right,
            "AVA",
            "OpenCode",
            "left.json".to_string(),
            "right.json".to_string(),
        )
        .unwrap();

        assert_eq!(comparison.aligned_task_count, 2);
        assert_eq!(comparison.left_only_tasks, vec!["left_only".to_string()]);
        assert_eq!(comparison.right_only_tasks, vec!["right_only".to_string()]);

        assert_eq!(comparison.left_summary.total_time_ms, 5_000);
        assert_eq!(comparison.right_summary.total_time_ms, 5_000);
        assert!((comparison.savings.cost_usd_saved_by_left - 0.05).abs() < 1e-9);

        assert_eq!(comparison.win_counts.left_time_wins, 1);
        assert_eq!(comparison.win_counts.right_time_wins, 1);
        assert_eq!(comparison.win_counts.left_cost_wins, 1);
        assert_eq!(comparison.win_counts.right_cost_wins, 1);
        assert_eq!(comparison.win_counts.right_quality_wins, 1);
        assert_eq!(comparison.win_counts.right_overall_wins, 1);
    }

    #[test]
    fn errors_when_no_tasks_align() {
        let left = report(vec![result("left_only", true, 100, 0.01)]);
        let right = report(vec![result("right_only", true, 100, 0.01)]);

        let err = compare_reports(
            &left,
            &right,
            "AVA",
            "OpenCode",
            "left.json".to_string(),
            "right.json".to_string(),
        )
        .unwrap_err();

        assert!(
            err.to_string().contains("No aligned tasks"),
            "unexpected error: {err}"
        );
    }

    #[test]
    fn computes_tied_rows_and_zero_denominator_savings_without_nan() {
        let left = report(vec![result("task_a", true, 0, 0.0)]);
        let right = report(vec![result("task_a", true, 0, 0.0)]);

        let comparison = compare_reports(
            &left,
            &right,
            "AVA",
            "OpenCode",
            "left.json".to_string(),
            "right.json".to_string(),
        )
        .unwrap();

        assert_eq!(comparison.win_counts.tied_quality, 1);
        assert_eq!(comparison.win_counts.tied_time, 1);
        assert_eq!(comparison.win_counts.tied_cost, 1);
        assert_eq!(comparison.win_counts.tied_overall, 1);
        assert_eq!(comparison.savings.time_pct_saved_by_left, 0.0);
        assert_eq!(comparison.savings.cost_pct_saved_by_left, 0.0);
    }

    #[test]
    fn treats_nearly_identical_costs_as_ties() {
        assert_eq!(numeric_winner(0.10000, 0.10005, 0.0001), SideWinner::Tie);
        assert_eq!(numeric_winner(1000.0, 1000.5, 1.0), SideWinner::Tie);
    }

    #[test]
    fn compares_repeat_summary_reports_by_pass_rate() {
        let left = repeat_summary_report("task_a", 1.0, 900);
        let right = repeat_summary_report("task_a", 0.33, 700);

        let comparison = compare_reports(
            &left,
            &right,
            "baseline",
            "candidate",
            "left.json".to_string(),
            "right.json".to_string(),
        )
        .unwrap();

        assert_eq!(comparison.task_rows[0].quality_winner, SideWinner::Left);
        assert_eq!(comparison.task_rows[0].left_quality_pass_rate, Some(1.0));
        assert_eq!(comparison.task_rows[0].right_quality_pass_rate, Some(0.33));
    }

    #[test]
    fn compare_score_input_requires_tests_when_present() {
        let metrics = ComparableTaskMetrics {
            quality_pass: false,
            quality_pass_rate: Some(0.0),
            total_time_ms: 100,
            cost_usd: 0.0,
            compile_success: Some(true),
            tests_passed: Some(0),
            tests_total: Some(2),
            had_error: false,
        };

        assert!(!score_input_from_result(&metrics).task_pass);
    }
}
