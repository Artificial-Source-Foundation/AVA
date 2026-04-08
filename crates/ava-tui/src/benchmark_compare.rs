use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

use color_eyre::eyre::{eyre, Result};
use serde::{Deserialize, Serialize};

use crate::benchmark::{BenchmarkReport, BenchmarkResult};
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

pub async fn run_ava_vs_opencode_comparison(
    ava_report_path: &Path,
    opencode_report_path: &Path,
    output_path: Option<&Path>,
) -> Result<BenchmarkComparisonReport> {
    let ava_raw = tokio::fs::read_to_string(ava_report_path)
        .await
        .map_err(|e| {
            eyre!(
                "Failed to read AVA report {}: {}",
                ava_report_path.display(),
                e
            )
        })?;
    let opencode_raw = tokio::fs::read_to_string(opencode_report_path)
        .await
        .map_err(|e| {
            eyre!(
                "Failed to read OpenCode report {}: {}",
                opencode_report_path.display(),
                e
            )
        })?;

    let ava_report: BenchmarkReport = serde_json::from_str(&ava_raw)
        .map_err(|e| eyre!("Failed to parse AVA report JSON: {}", e))?;
    let opencode_report: BenchmarkReport = serde_json::from_str(&opencode_raw)
        .map_err(|e| eyre!("Failed to parse OpenCode report JSON: {}", e))?;

    let comparison = compare_reports(
        &ava_report,
        &opencode_report,
        "AVA",
        "OpenCode",
        ava_report_path.display().to_string(),
        opencode_report_path.display().to_string(),
    )?;

    print_comparison_summary(&comparison);

    if let Some(path) = output_path {
        let json = serde_json::to_string_pretty(&comparison)
            .map_err(|e| eyre!("Failed to serialize comparison JSON: {}", e))?;
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

fn compare_reports(
    left_report: &BenchmarkReport,
    right_report: &BenchmarkReport,
    left_label: &str,
    right_label: &str,
    left_report_path: String,
    right_report_path: String,
) -> Result<BenchmarkComparisonReport> {
    let left_by_task = first_result_by_task_name(&left_report.results);
    let right_by_task = first_result_by_task_name(&right_report.results);

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
        left_report.results.len(),
        &task_rows,
        &left_inputs,
    );
    let right_summary = compute_side_summary(
        right_label,
        right_report.results.len(),
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

fn first_result_by_task_name<'a>(
    results: &'a [BenchmarkResult],
) -> BTreeMap<String, &'a BenchmarkResult> {
    let mut map = BTreeMap::new();
    for result in results {
        map.entry(result.task_name.clone()).or_insert(result);
    }
    map
}

fn score_input_from_result(result: &BenchmarkResult) -> ScoreInput {
    ScoreInput {
        task_pass: result.quality_pass && result.error.is_none(),
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
    left: &BenchmarkResult,
    right: &BenchmarkResult,
) -> TaskComparisonRow {
    let quality_winner = bool_winner(left.quality_pass, right.quality_pass);
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
            timestamp: "2026-04-08T00:00:00Z".to_string(),
            results,
            aggregate_cost_per_resolved: None,
            aggregate_tool_efficiency: None,
            aggregate_tool_reliability: None,
            aggregate_delegation_efficiency: None,
            aggregate_delegation_quality: None,
        }
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
}
