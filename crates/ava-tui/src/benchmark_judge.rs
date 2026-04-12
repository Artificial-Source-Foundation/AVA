use std::collections::HashMap;
use std::sync::Arc;

use ava_config::CredentialStore;
use ava_llm::pool::ConnectionPool;
use ava_llm::providers::create_provider;
use ava_types::{Message, Role, ThinkingLevel};
use color_eyre::eyre::{eyre, Result};
use regex::Regex;

use crate::benchmark::{
    compute_delegation_quality_score, BenchmarkResult, JudgeEvaluation, JudgeScores, ModelSpec,
};
use crate::benchmark_format::short_model_name;
use crate::benchmark_tasks::BenchmarkTask;

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
pub(crate) async fn judge_outputs(
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
            format!(
                "{}...(truncated)",
                truncate_utf8_to_byte_boundary(&raw_output, 4000)
            )
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
            result.delegation_quality_score = compute_delegation_quality_score(
                result.quality_pass,
                result.compile_success,
                result.judge_scores.as_ref(),
                result.subagent_calls_count,
                result.resumed_subagent_calls_count,
                result.subagent_cost_usd,
                result.cost_usd,
            );
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
                format!("{}...", truncate_utf8_to_byte_boundary(response, 200))
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

fn truncate_utf8_to_byte_boundary(s: &str, max_bytes: usize) -> &str {
    if s.len() <= max_bytes {
        return s;
    }

    let mut boundary = max_bytes;
    while !s.is_char_boundary(boundary) {
        boundary -= 1;
    }

    &s[..boundary]
}

#[cfg(test)]
mod tests {
    use super::*;

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
    fn test_truncate_utf8_to_byte_boundary() {
        assert_eq!(truncate_utf8_to_byte_boundary("hello world", 5), "hello");
        assert_eq!(truncate_utf8_to_byte_boundary("abc", 10), "abc");

        let emoji = "a🙂b";
        assert_eq!(truncate_utf8_to_byte_boundary(emoji, 6), emoji);
        assert_eq!(truncate_utf8_to_byte_boundary(emoji, 4), "a");
    }
}
