use crate::benchmark::{BenchmarkReport, BenchmarkResult};
use crate::benchmark_harness::HarnessReport;
use crate::benchmark_tasks::BenchmarkSuite;

fn short_timestamp(timestamp: &str) -> &str {
    timestamp.get(..19).unwrap_or(timestamp)
}

/// Short display name for a model (last segment of model path).
pub(crate) fn short_model_name(model: &str) -> String {
    model.rsplit('/').next().unwrap_or(model).to_string()
}

pub(crate) fn format_subagent_mix(types: &[String]) -> Option<String> {
    if types.is_empty() {
        return None;
    }

    let mut counts = std::collections::BTreeMap::new();
    for agent_type in types {
        *counts.entry(agent_type.as_str()).or_insert(0usize) += 1;
    }

    Some(
        counts
            .into_iter()
            .map(|(agent_type, count)| format!("{agent_type} x{count}"))
            .collect::<Vec<_>>()
            .join(", "),
    )
}

pub(crate) fn format_delegation_mix(results: &[BenchmarkResult]) -> Option<String> {
    let mut counts = std::collections::BTreeMap::new();
    for result in results {
        for agent_type in &result.subagent_types {
            *counts.entry(agent_type.as_str()).or_insert(0usize) += 1;
        }
    }

    if counts.is_empty() {
        return None;
    }

    Some(
        counts
            .into_iter()
            .map(|(agent_type, count)| format!("{agent_type} x{count}"))
            .collect::<Vec<_>>()
            .join(", "),
    )
}

/// Print a formatted results table to stdout.
pub(crate) fn print_results_table(report: &BenchmarkReport, suite: BenchmarkSuite) {
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
    println!(
        "                     {}",
        short_timestamp(&report.timestamp)
    );
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
        let has_subagents = task_results.iter().any(|r| r.subagent_calls_count > 0);
        let has_activity_metrics = has_tools || has_subagents;

        println!();
        println!("  Task: {} [{}]", task_name, task_results[0].task_category);

        if has_compile && has_judges {
            println!(
                "  {:<22} {:>9} {:>9} {:>9} {:>8} {:>8} {:>7} {:>6} {:>5} {:>6} {:>7}",
                "Model",
                "TTFT(ms)",
                "Total(s)",
                "WallTok/s",
                "GenTok/s",
                "Compile",
                "Tests",
                "Tools",
                "Subs",
                "Turns",
                "Score",
            );
            println!(
                "  {:-<22} {:-<9} {:-<9} {:-<9} {:-<8} {:-<8} {:-<7} {:-<6} {:-<5} {:-<6} {:-<7}",
                "", "", "", "", "", "", "", "", "", "", ""
            );
        } else if has_compile {
            println!(
                "  {:<22} {:>9} {:>9} {:>9} {:>8} {:>8} {:>7} {:>6} {:>5} {:>6}  Quality",
                "Model",
                "TTFT(ms)",
                "Total(s)",
                "WallTok/s",
                "GenTok/s",
                "Compile",
                "Tests",
                "Tools",
                "Subs",
                "Turns",
            );
            println!(
                "  {:-<22} {:-<9} {:-<9} {:-<9} {:-<8} {:-<8} {:-<7} {:-<6} {:-<5} {:-<6}  {:-<20}",
                "", "", "", "", "", "", "", "", "", "", ""
            );
        } else if has_activity_metrics {
            println!(
                "  {:<22} {:>9} {:>9} {:>9} {:>8} {:>8} {:>8} {:>6} {:>5} {:>6}  Quality",
                "Model",
                "TTFT(ms)",
                "Total(s)",
                "WallTok/s",
                "GenTok/s",
                "In Tok",
                "Cost",
                "Tools",
                "Subs",
                "Turns",
            );
            println!(
                "  {:-<22} {:-<9} {:-<9} {:-<9} {:-<8} {:-<8} {:-<8} {:-<6} {:-<5} {:-<6}  {:-<20}",
                "", "", "", "", "", "", "", "", "", "", ""
            );
        } else if has_judges {
            println!(
                "  {:<22} {:>9} {:>9} {:>9} {:>8} {:>8} {:>8} {:>7}",
                "Model", "TTFT(ms)", "Total(s)", "WallTok/s", "GenTok/s", "In Tok", "Cost", "Score",
            );
            println!(
                "  {:-<22} {:-<9} {:-<9} {:-<9} {:-<8} {:-<8} {:-<8} {:-<7}",
                "", "", "", "", "", "", "", ""
            );
        } else {
            println!(
                "  {:<22} {:>9} {:>9} {:>9} {:>8} {:>8} {:>8}  Quality",
                "Model", "TTFT(ms)", "Total(s)", "WallTok/s", "GenTok/s", "In Tok", "Cost",
            );
            println!(
                "  {:-<22} {:-<9} {:-<9} {:-<9} {:-<8} {:-<8} {:-<8}  {:-<20}",
                "", "", "", "", "", "", "", ""
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

            let generation_tps_str = r
                .generation_tps
                .filter(|tps| *tps > 0.0)
                .map(|tps| format!("{:.0}", tps))
                .unwrap_or_else(|| "-".to_string());

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

            let subs_str = if r.subagent_calls_count > 0 {
                r.subagent_calls_count.to_string()
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
                    "  {:<22} {:>9} {:>9} {:>9} {:>8} {:>8} {:>7} {:>6} {:>5} {:>6} {:>7}",
                    model_display,
                    ttft_str,
                    total_str,
                    tps_str,
                    generation_tps_str,
                    compile_str,
                    tests_str,
                    tools_str,
                    subs_str,
                    turns_str,
                    score_str,
                );
            } else if has_compile {
                println!(
                    "  {:<22} {:>9} {:>9} {:>9} {:>8} {:>8} {:>7} {:>6} {:>5} {:>6}  {}",
                    model_display,
                    ttft_str,
                    total_str,
                    tps_str,
                    generation_tps_str,
                    compile_str,
                    tests_str,
                    tools_str,
                    subs_str,
                    turns_str,
                    quality_str,
                );
            } else if has_activity_metrics {
                println!(
                    "  {:<22} {:>9} {:>9} {:>9} {:>8} {:>8} {:>8} {:>6} {:>5} {:>6}  {}",
                    model_display,
                    ttft_str,
                    total_str,
                    tps_str,
                    generation_tps_str,
                    r.input_tokens,
                    cost_str,
                    tools_str,
                    subs_str,
                    turns_str,
                    quality_str,
                );
            } else if has_judges {
                println!(
                    "  {:<22} {:>9} {:>9} {:>9} {:>8} {:>8} {:>8} {:>7}",
                    model_display,
                    ttft_str,
                    total_str,
                    tps_str,
                    generation_tps_str,
                    r.input_tokens,
                    cost_str,
                    score_str,
                );
            } else {
                println!(
                    "  {:<22} {:>9} {:>9} {:>9} {:>8} {:>8} {:>8}  {}",
                    model_display,
                    ttft_str,
                    total_str,
                    tps_str,
                    generation_tps_str,
                    r.input_tokens,
                    cost_str,
                    quality_str,
                );
            }

            if let Some(summary) = r.delegation_summary() {
                println!("  {:<22} delegation: {}", "", summary);
            }
        }
    }

    // Summary
    println!();
    println!("-----------------------------------------------------------------------");

    let total_cost: f64 = report.results.iter().map(|r| r.cost_usd).sum();
    let total_subagents: usize = report.results.iter().map(|r| r.subagent_calls_count).sum();
    let total_subagent_cost: f64 = report.results.iter().map(|r| r.subagent_cost_usd).sum();
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

    if total_subagents > 0 {
        summary.push_str(&format!(
            ", {} subagents, ${:.4} delegated",
            total_subagents, total_subagent_cost
        ));
    }

    if let Some(eff) = report.aggregate_delegation_efficiency {
        summary.push_str(&format!(", delegation efficiency: {:.2}", eff));
    }

    if let Some(score) = report.aggregate_delegation_quality {
        summary.push_str(&format!(", delegation quality: {:.2}", score));
    }

    println!("{}", summary);
    if total_subagents > 0 {
        let all_subagent_types: Vec<String> = report
            .results
            .iter()
            .flat_map(|result| result.subagent_types.iter().cloned())
            .collect();
        if let Some(mix) = format_subagent_mix(&all_subagent_types) {
            println!("  Delegation mix: {}", mix);
        }
    }
    println!("=======================================================================");
    println!();
}

pub(crate) fn print_repeat_summary(report: &BenchmarkReport, suite: BenchmarkSuite) {
    let Some(summary) = report.repeat_summary.as_ref() else {
        print_results_table(report, suite);
        return;
    };

    println!();
    println!("=======================================================================");
    println!(
        "      AVA Benchmark Repeat Summary ({} suite, {} runs)",
        suite, summary.repeat_count
    );
    println!(
        "                     {}",
        short_timestamp(&report.timestamp)
    );
    println!("=======================================================================");
    println!(
        "  Overall pass rate: {:.1}% | median runtime: {:.1}s | worst task variance: {:.1}s",
        summary.overall_pass_rate * 100.0,
        summary.median_total_time_ms as f64 / 1000.0,
        summary.worst_task_variance_ms as f64 / 1000.0,
    );

    println!();
    println!(
        "  {:<22} {:<18} {:>8} {:>9} {:>9} {:>7} {:>7}",
        "Task", "Model", "Pass%", "Median(s)", "P95(s)", "Tools", "Subs"
    );
    println!(
        "  {:-<22} {:-<18} {:-<8} {:-<9} {:-<9} {:-<7} {:-<7}",
        "", "", "", "", "", "", ""
    );
    for task in &summary.task_summaries {
        let model = short_model_name(&task.model);
        let task_display = if task.task_name.len() > 22 {
            format!("{}...", &task.task_name[..19])
        } else {
            task.task_name.clone()
        };
        let model_display = if model.len() > 18 {
            format!("{}...", &model[..15])
        } else {
            model
        };
        println!(
            "  {:<22} {:<18} {:>7.1}% {:>9.1} {:>9.1} {:>7} {:>7}",
            task_display,
            model_display,
            task.pass_rate * 100.0,
            task.median_total_time_ms as f64 / 1000.0,
            task.p95_total_time_ms as f64 / 1000.0,
            task.median_tool_calls_count,
            task.median_subagent_calls_count,
        );
    }

    if let Some(score_summary) = report.score_summary.as_ref() {
        println!();
        println!(
            "  Aggregate score: {:.3} | task pass: {:.1}% | quality signal: {:.1}%",
            score_summary.aggregate_score,
            score_summary.task_pass_rate * 100.0,
            score_summary.quality_signal_score * 100.0,
        );
    }

    println!("=======================================================================");
    println!();
}

/// Print the harnessed-pair comparison table.
pub(crate) fn print_harness_table(report: &HarnessReport) {
    let dir_name = short_model_name(&report.config.director.model);
    let wrk_name = short_model_name(&report.config.worker.model);

    println!();
    println!("=======================================================================");
    println!("           AVA Harnessed-Pair Benchmark Results");
    println!("           Director: {} | Worker: {}", dir_name, wrk_name);
    println!("           {}", short_timestamp(&report.timestamp));
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
            if let Some(summary) = r.delegation_summary() {
                println!("  {:<20} delegation: {}", "", summary);
            }
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
            if let Some(summary) = r.delegation_summary() {
                println!("  {:<20} delegation: {}", "", summary);
            }
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
    let dir_total_subagents: usize = report
        .solo_director_results
        .as_ref()
        .map(|v| v.iter().map(|r| r.subagent_calls_count).sum())
        .unwrap_or(0);
    let wrk_total_subagents: usize = report
        .solo_worker_results
        .as_ref()
        .map(|v| v.iter().map(|r| r.subagent_calls_count).sum())
        .unwrap_or(0);
    let dir_total_delegated_cost: f64 = report
        .solo_director_results
        .as_ref()
        .map(|v| v.iter().map(|r| r.subagent_cost_usd).sum())
        .unwrap_or(0.0);
    let wrk_total_delegated_cost: f64 = report
        .solo_worker_results
        .as_ref()
        .map(|v| v.iter().map(|r| r.subagent_cost_usd).sum())
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

    if dir_total_subagents > 0 {
        println!(
            "  {} solo delegation: {} helper runs, ${:.4} delegated{}",
            dir_name,
            dir_total_subagents,
            dir_total_delegated_cost,
            report
                .solo_director_results
                .as_ref()
                .and_then(|results| format_delegation_mix(results))
                .map(|mix| format!(", mix: {mix}"))
                .unwrap_or_default()
        );
    }

    if wrk_total_subagents > 0 {
        println!(
            "  {} solo delegation: {} helper runs, ${:.4} delegated{}",
            wrk_name,
            wrk_total_subagents,
            wrk_total_delegated_cost,
            report
                .solo_worker_results
                .as_ref()
                .and_then(|results| format_delegation_mix(results))
                .map(|mix| format!(", mix: {mix}"))
                .unwrap_or_default()
        );
    }

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

#[cfg(test)]
mod tests {
    use std::collections::HashMap;

    use super::*;

    #[test]
    fn format_subagent_mix_empty() {
        assert_eq!(format_subagent_mix(&[]), None);
    }

    #[test]
    fn format_subagent_mix_counts_types() {
        let types = vec![
            "planner".to_string(),
            "helper".to_string(),
            "helper".to_string(),
        ];
        assert_eq!(
            format_subagent_mix(&types),
            Some("helper x2, planner x1".to_string())
        );
    }

    fn make_result(subagent_types: Vec<&str>) -> BenchmarkResult {
        BenchmarkResult {
            task_name: "task".to_string(),
            task_category: "category".to_string(),
            provider: "provider".to_string(),
            model: "model".to_string(),
            prompt_family: None,
            prompt_variant: None,
            prompt_hash: None,
            run_index: None,
            ttft_ms: None,
            total_time_ms: 0,
            input_tokens: 0,
            output_tokens: 0,
            tokens_per_second: 0.0,
            generation_tps: None,
            cost_usd: 0.0,
            quality_pass: true,
            quality_details: String::new(),
            error: None,
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
            subagent_calls_count: subagent_types.len(),
            subagent_types: subagent_types.into_iter().map(str::to_string).collect(),
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

    #[test]
    fn format_delegation_mix_empty_results() {
        let results = vec![];
        assert_eq!(format_delegation_mix(&results), None);
    }

    #[test]
    fn format_delegation_mix_counts_types_across_results() {
        let results = vec![
            make_result(vec!["planner", "helper"]),
            make_result(vec!["helper", "planner"]),
        ];

        assert_eq!(
            format_delegation_mix(&results),
            Some("helper x2, planner x2".to_string())
        );
    }
}
