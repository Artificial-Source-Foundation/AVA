use serde::{Deserialize, Serialize};

pub const AGGREGATE_SCHEMA_VERSION: u32 = 1;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct AggregateScoreSummary {
    pub schema_version: u32,
    pub sample_size: usize,
    pub resolved_tasks: usize,
    pub task_pass_rate: f64,
    pub quality_pass_rate: f64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub compile_success_rate: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub test_pass_rate: Option<f64>,
    pub avg_cost_usd: f64,
    pub avg_time_ms: f64,
    pub cost_normalization: f64,
    pub time_normalization: f64,
    pub quality_signal_score: f64,
    pub aggregate_score: f64,
    pub ci_pass: bool,
}

#[derive(Debug, Clone)]
pub struct ScoreInput {
    pub task_pass: bool,
    pub quality_pass: bool,
    pub compile_success: Option<bool>,
    pub tests_passed: Option<usize>,
    pub tests_total: Option<usize>,
    pub cost_usd: f64,
    pub total_time_ms: u64,
}

pub fn compute_aggregate_summary(results: &[ScoreInput]) -> AggregateScoreSummary {
    if results.is_empty() {
        return AggregateScoreSummary {
            schema_version: AGGREGATE_SCHEMA_VERSION,
            sample_size: 0,
            resolved_tasks: 0,
            task_pass_rate: 0.0,
            quality_pass_rate: 0.0,
            compile_success_rate: None,
            test_pass_rate: None,
            avg_cost_usd: 0.0,
            avg_time_ms: 0.0,
            cost_normalization: 1.0,
            time_normalization: 1.0,
            quality_signal_score: 0.0,
            aggregate_score: 0.0,
            ci_pass: false,
        };
    }

    let sample_size = results.len();
    let resolved_tasks = results.iter().filter(|r| r.task_pass).count();
    let quality_pass_count = results.iter().filter(|r| r.quality_pass).count();

    let compile_total = results
        .iter()
        .filter(|r| r.compile_success.is_some())
        .count();
    let compile_success_count = results
        .iter()
        .filter(|r| matches!(r.compile_success, Some(true)))
        .count();

    let mut test_passed_sum = 0usize;
    let mut test_total_sum = 0usize;
    for result in results {
        if let (Some(passed), Some(total)) = (result.tests_passed, result.tests_total) {
            if total > 0 {
                test_passed_sum += passed;
                test_total_sum += total;
            }
        }
    }

    let total_cost: f64 = results.iter().map(|r| r.cost_usd).sum();
    let total_time_ms: u64 = results.iter().map(|r| r.total_time_ms).sum();

    let task_pass_rate = ratio(resolved_tasks, sample_size);
    let quality_pass_rate = ratio(quality_pass_count, sample_size);
    let compile_success_rate = if compile_total > 0 {
        Some(ratio(compile_success_count, compile_total))
    } else {
        None
    };
    let test_pass_rate = if test_total_sum > 0 {
        Some(test_passed_sum as f64 / test_total_sum as f64)
    } else {
        None
    };

    let avg_cost_usd = total_cost / sample_size as f64;
    let avg_time_ms = total_time_ms as f64 / sample_size as f64;

    let cost_normalization = normalize_cost(avg_cost_usd);
    let time_normalization = normalize_time_ms(avg_time_ms);

    let compile_signal = compile_success_rate.unwrap_or(quality_pass_rate);
    let test_signal = test_pass_rate.unwrap_or(quality_pass_rate);
    let quality_signal_score = clamp_01((quality_pass_rate + compile_signal + test_signal) / 3.0);

    let aggregate_score = clamp_01(
        (0.60 * task_pass_rate)
            + (0.20 * quality_signal_score)
            + (0.10 * cost_normalization)
            + (0.10 * time_normalization),
    );

    let ci_pass = task_pass_rate >= 0.80
        && compile_success_rate.unwrap_or(1.0) >= 0.70
        && test_pass_rate.unwrap_or(1.0) >= 0.70
        && aggregate_score >= 0.70;

    AggregateScoreSummary {
        schema_version: AGGREGATE_SCHEMA_VERSION,
        sample_size,
        resolved_tasks,
        task_pass_rate,
        quality_pass_rate,
        compile_success_rate,
        test_pass_rate,
        avg_cost_usd,
        avg_time_ms,
        cost_normalization,
        time_normalization,
        quality_signal_score,
        aggregate_score,
        ci_pass,
    }
}

pub fn render_ci_summary_line(label: &str, summary: &AggregateScoreSummary) -> String {
    let compile_str = summary
        .compile_success_rate
        .map(|v| format!("{:.4}", v))
        .unwrap_or_else(|| "na".to_string());
    let test_str = summary
        .test_pass_rate
        .map(|v| format!("{:.4}", v))
        .unwrap_or_else(|| "na".to_string());
    format!(
        "CI_BENCHMARK label={} status={} aggregate={:.4} task_pass={:.4} quality={:.4} compile={} tests={} cost_norm={:.4} time_norm={:.4} n={}",
        label,
        if summary.ci_pass { "PASS" } else { "FAIL" },
        summary.aggregate_score,
        summary.task_pass_rate,
        summary.quality_signal_score,
        compile_str,
        test_str,
        summary.cost_normalization,
        summary.time_normalization,
        summary.sample_size,
    )
}

fn ratio(num: usize, denom: usize) -> f64 {
    if denom == 0 {
        0.0
    } else {
        num as f64 / denom as f64
    }
}

fn normalize_cost(avg_cost_usd: f64) -> f64 {
    clamp_01(1.0 / (1.0 + avg_cost_usd.max(0.0)))
}

fn normalize_time_ms(avg_time_ms: f64) -> f64 {
    clamp_01(1.0 / (1.0 + (avg_time_ms.max(0.0) / 1000.0)))
}

fn clamp_01(value: f64) -> f64 {
    value.clamp(0.0, 1.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn computes_expected_aggregate_summary_rates() {
        let inputs = vec![
            ScoreInput {
                task_pass: true,
                quality_pass: true,
                compile_success: Some(true),
                tests_passed: Some(2),
                tests_total: Some(2),
                cost_usd: 0.20,
                total_time_ms: 1_000,
            },
            ScoreInput {
                task_pass: false,
                quality_pass: false,
                compile_success: Some(false),
                tests_passed: Some(1),
                tests_total: Some(2),
                cost_usd: 0.30,
                total_time_ms: 2_000,
            },
        ];

        let summary = compute_aggregate_summary(&inputs);

        assert_eq!(summary.schema_version, AGGREGATE_SCHEMA_VERSION);
        assert_eq!(summary.sample_size, 2);
        assert_eq!(summary.resolved_tasks, 1);
        assert!((summary.task_pass_rate - 0.5).abs() < 1e-9);
        assert!((summary.quality_pass_rate - 0.5).abs() < 1e-9);
        assert!((summary.compile_success_rate.unwrap() - 0.5).abs() < 1e-9);
        assert!((summary.test_pass_rate.unwrap() - 0.75).abs() < 1e-9);
        assert!(summary.aggregate_score > 0.0);
        assert!(summary.aggregate_score <= 1.0);
        assert!(!summary.ci_pass);
    }

    #[test]
    fn aggregate_summary_schema_is_stable() {
        let summary = AggregateScoreSummary {
            schema_version: AGGREGATE_SCHEMA_VERSION,
            sample_size: 4,
            resolved_tasks: 3,
            task_pass_rate: 0.75,
            quality_pass_rate: 1.0,
            compile_success_rate: Some(0.5),
            test_pass_rate: Some(0.66),
            avg_cost_usd: 0.12,
            avg_time_ms: 850.0,
            cost_normalization: 0.89,
            time_normalization: 0.54,
            quality_signal_score: 0.72,
            aggregate_score: 0.74,
            ci_pass: true,
        };

        let value = serde_json::to_value(&summary).unwrap();
        assert_eq!(value["schema_version"], 1);
        assert!(value.get("aggregate_score").is_some());
        assert!(value.get("task_pass_rate").is_some());
        assert!(value.get("quality_signal_score").is_some());
        assert!(value.get("cost_normalization").is_some());
        assert!(value.get("time_normalization").is_some());
        assert!(value.get("ci_pass").is_some());

        let round_trip: AggregateScoreSummary = serde_json::from_value(value).unwrap();
        assert_eq!(round_trip.schema_version, AGGREGATE_SCHEMA_VERSION);
        assert_eq!(round_trip.sample_size, 4);
    }

    #[test]
    fn ci_line_contains_parseable_status() {
        let summary = AggregateScoreSummary {
            schema_version: AGGREGATE_SCHEMA_VERSION,
            sample_size: 1,
            resolved_tasks: 1,
            task_pass_rate: 1.0,
            quality_pass_rate: 1.0,
            compile_success_rate: None,
            test_pass_rate: None,
            avg_cost_usd: 0.01,
            avg_time_ms: 120.0,
            cost_normalization: 0.99,
            time_normalization: 0.89,
            quality_signal_score: 1.0,
            aggregate_score: 0.95,
            ci_pass: true,
        };

        let line = render_ci_summary_line("benchmark", &summary);
        assert!(line.contains("CI_BENCHMARK"));
        assert!(line.contains("status=PASS"));
        assert!(line.contains("label=benchmark"));
    }
}
