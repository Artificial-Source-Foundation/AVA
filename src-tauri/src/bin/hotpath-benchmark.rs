#[path = "../commands/compute_fuzzy.rs"]
mod compute_fuzzy;
#[path = "../commands/compute_grep.rs"]
mod compute_grep;

use compute_fuzzy::{compute_fuzzy_replace, ComputeFuzzyReplaceInput};
use compute_grep::{compute_grep, ComputeGrepInput};
use serde::Serialize;
use serde_json::json;
use std::env;
use std::fs;
use std::path::Path;
use std::time::Instant;

#[derive(Debug, Serialize)]
struct BenchmarkSummary {
    iterations: usize,
    min: f64,
    max: f64,
    p50: f64,
    p95: f64,
    mean: f64,
}

#[derive(Debug, Serialize)]
struct BenchmarkOutput {
    mode: String,
    warmup: usize,
    summary: BenchmarkSummary,
    samples_ms: Vec<f64>,
    details: serde_json::Value,
}

fn main() {
    if let Err(err) = run() {
        eprintln!("{err}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), String> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        return Err(usage());
    }

    let output = match args[1].as_str() {
        "grep" => run_grep_mode(&args[2..])?,
        "fuzzy" => run_fuzzy_mode(&args[2..])?,
        _ => return Err(usage()),
    };

    println!(
        "{}",
        serde_json::to_string_pretty(&output)
            .map_err(|err| format!("Failed to serialize benchmark output: {err}"))?
    );

    Ok(())
}

fn run_grep_mode(args: &[String]) -> Result<BenchmarkOutput, String> {
    if args.len() != 6 {
        return Err(format!(
            "Invalid grep arguments. Expected 6 args, got {}.\n{}",
            args.len(),
            usage()
        ));
    }

    let path = args[0].clone();
    let pattern = args[1].clone();
    let include = if args[2] == "-" {
        None
    } else {
        Some(args[2].clone())
    };
    let iterations = parse_usize("iterations", &args[3])?;
    let warmup = parse_usize("warmup", &args[4])?;
    let max_results = parse_usize("max_results", &args[5])?;

    let mut last_count = 0usize;
    let mut last_truncated = false;
    let samples = benchmark(warmup, iterations, || {
        let output = compute_grep(ComputeGrepInput {
            path: path.clone(),
            pattern: pattern.clone(),
            include: include.clone(),
            max_results: Some(max_results),
        })?;

        last_count = output.matches.len();
        last_truncated = output.truncated;
        Ok(())
    })?;

    Ok(BenchmarkOutput {
        mode: "grep".to_string(),
        warmup,
        summary: summarize(&samples),
        samples_ms: samples,
        details: json!({
            "path": path,
            "pattern": pattern,
            "include": include,
            "maxResults": max_results,
            "lastMatchCount": last_count,
            "lastTruncated": last_truncated
        }),
    })
}

fn run_fuzzy_mode(args: &[String]) -> Result<BenchmarkOutput, String> {
    if args.len() != 6 {
        return Err(format!(
            "Invalid fuzzy arguments. Expected 6 args, got {}.\n{}",
            args.len(),
            usage()
        ));
    }

    let content_path = args[0].clone();
    let old_path = args[1].clone();
    let new_path = args[2].clone();
    let iterations = parse_usize("iterations", &args[3])?;
    let warmup = parse_usize("warmup", &args[4])?;
    let replace_all = parse_bool("replace_all", &args[5])?;

    let content = fs::read_to_string(&content_path)
        .map_err(|err| format!("Failed to read content file '{}': {err}", content_path))?;
    let old_string = fs::read_to_string(&old_path)
        .map_err(|err| format!("Failed to read oldString file '{}': {err}", old_path))?;
    let new_string = fs::read_to_string(&new_path)
        .map_err(|err| format!("Failed to read newString file '{}': {err}", new_path))?;

    let mut last_strategy = String::new();
    let mut last_content_length = 0usize;
    let samples = benchmark(warmup, iterations, || {
        let output = compute_fuzzy_replace(ComputeFuzzyReplaceInput {
            content: content.clone(),
            old_string: old_string.clone(),
            new_string: new_string.clone(),
            replace_all: Some(replace_all),
        })?;

        last_strategy = output.strategy;
        last_content_length = output.content.len();
        Ok(())
    })?;

    Ok(BenchmarkOutput {
        mode: "fuzzy".to_string(),
        warmup,
        summary: summarize(&samples),
        samples_ms: samples,
        details: json!({
            "contentPath": content_path,
            "oldStringPath": old_path,
            "newStringPath": new_path,
            "replaceAll": replace_all,
            "lastStrategy": last_strategy,
            "lastContentLength": last_content_length
        }),
    })
}

fn benchmark<F>(warmup: usize, iterations: usize, mut operation: F) -> Result<Vec<f64>, String>
where
    F: FnMut() -> Result<(), String>,
{
    for _ in 0..warmup {
        operation()?;
    }

    let mut samples = Vec::with_capacity(iterations);
    for _ in 0..iterations {
        let started = Instant::now();
        operation()?;
        samples.push(started.elapsed().as_secs_f64() * 1000.0);
    }

    Ok(samples)
}

fn summarize(samples: &[f64]) -> BenchmarkSummary {
    let iterations = samples.len();
    let min = samples.iter().copied().fold(f64::INFINITY, f64::min);
    let max = samples.iter().copied().fold(f64::NEG_INFINITY, f64::max);
    let sum: f64 = samples.iter().sum();

    BenchmarkSummary {
        iterations,
        min,
        max,
        p50: percentile(samples, 50.0),
        p95: percentile(samples, 95.0),
        mean: sum / iterations as f64,
    }
}

fn percentile(samples: &[f64], percent: f64) -> f64 {
    let mut sorted = samples.to_vec();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

    let clamped = percent.clamp(0.0, 100.0);
    let rank = ((clamped / 100.0) * sorted.len() as f64).ceil() as usize;
    let index = rank.saturating_sub(1).min(sorted.len().saturating_sub(1));
    sorted[index]
}

fn parse_usize(name: &str, value: &str) -> Result<usize, String> {
    value
        .parse::<usize>()
        .map_err(|err| format!("Invalid {name} '{value}': {err}"))
}

fn parse_bool(name: &str, value: &str) -> Result<bool, String> {
    match value {
        "1" | "true" | "TRUE" | "yes" | "YES" => Ok(true),
        "0" | "false" | "FALSE" | "no" | "NO" => Ok(false),
        _ => Err(format!(
            "Invalid {name} '{value}'. Use 1/0, true/false, yes/no."
        )),
    }
}

fn usage() -> String {
    format!(
        "Usage:\n  {} grep <path> <pattern> <include-or--> <iterations> <warmup> <max-results>\n  {} fuzzy <content-file> <old-file> <new-file> <iterations> <warmup> <replace-all-1-or-0>",
        executable_name(),
        executable_name()
    )
}

fn executable_name() -> String {
    env::args()
        .next()
        .and_then(|arg| {
            Path::new(&arg)
                .file_name()
                .map(|name| name.to_string_lossy().into_owned())
        })
        .unwrap_or_else(|| "hotpath-benchmark".to_string())
}
