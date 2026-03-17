//! Reviewer retry loop (BG2-13, inspired by SWE-Agent).
//!
//! Runs an agent task multiple times, uses an LLM judge to score solutions,
//! and selects the best one. Useful for hard tasks where the first attempt
//! may not be optimal.

use ava_types::{Message, Role};

/// A single solution attempt with its score.
#[derive(Debug, Clone)]
pub struct ScoredSolution {
    /// The messages produced by this attempt.
    pub messages: Vec<Message>,
    /// Score from the reviewer (0.0 to 1.0).
    pub score: f64,
    /// Reviewer's rationale for the score.
    pub rationale: String,
    /// Which attempt number this was (1-indexed).
    pub attempt: usize,
}

/// Configuration for the reviewer retry loop.
#[derive(Debug, Clone)]
pub struct ReviewerConfig {
    /// Maximum number of attempts to run.
    pub max_attempts: usize,
    /// Minimum acceptable score (0.0 to 1.0). If an attempt scores above this,
    /// stop early and use it.
    pub accept_threshold: f64,
}

impl Default for ReviewerConfig {
    fn default() -> Self {
        Self {
            max_attempts: 3,
            accept_threshold: 0.85,
        }
    }
}

/// Trait for scoring a solution attempt.
/// Implemented by wrapping an LLM provider call.
#[async_trait::async_trait]
pub trait SolutionReviewer: Send + Sync {
    /// Score a solution given the original goal and the attempt's messages.
    /// Returns (score 0.0-1.0, rationale string).
    async fn review(&self, goal: &str, messages: &[Message]) -> Result<(f64, String), String>;
}

/// A simple heuristic reviewer that scores based on message characteristics.
/// Used as a fallback when no LLM reviewer is available.
pub struct HeuristicReviewer;

#[async_trait::async_trait]
impl SolutionReviewer for HeuristicReviewer {
    async fn review(&self, _goal: &str, messages: &[Message]) -> Result<(f64, String), String> {
        let mut score = 0.5; // baseline

        // Bonus for having tool calls (agent did work)
        let tool_call_count: usize = messages.iter().map(|m| m.tool_calls.len()).sum();
        if tool_call_count > 0 {
            score += 0.1;
        }

        // Bonus for assistant messages with substantial content
        let has_substantial_response = messages
            .iter()
            .any(|m| m.role == Role::Assistant && m.content.len() > 100);
        if has_substantial_response {
            score += 0.1;
        }

        // Penalty for error results
        let error_count: usize = messages
            .iter()
            .flat_map(|m| m.tool_results.iter())
            .filter(|tr| tr.is_error)
            .count();
        score -= (error_count as f64) * 0.05;

        // Bonus for final message being from assistant (completed)
        if messages
            .last()
            .map(|m| m.role == Role::Assistant)
            .unwrap_or(false)
        {
            score += 0.1;
        }

        score = score.clamp(0.0, 1.0);
        let rationale = format!(
            "Heuristic: {tool_call_count} tool calls, {error_count} errors, \
             substantial={has_substantial_response}"
        );

        Ok((score, rationale))
    }
}

/// Run the reviewer retry loop.
///
/// Takes a closure that produces a solution attempt (Vec<Message>),
/// runs it up to `config.max_attempts` times, scores each, and returns
/// the best-scoring solution.
pub async fn review_and_select<F, Fut>(
    config: &ReviewerConfig,
    reviewer: &dyn SolutionReviewer,
    goal: &str,
    mut run_attempt: F,
) -> Result<ScoredSolution, String>
where
    F: FnMut(usize) -> Fut,
    Fut: std::future::Future<Output = Result<Vec<Message>, String>>,
{
    let mut solutions = Vec::new();

    for attempt in 1..=config.max_attempts {
        let messages = run_attempt(attempt).await?;
        let (score, rationale) = reviewer.review(goal, &messages).await?;

        tracing::info!(
            "Reviewer: attempt {attempt}/{} scored {score:.2} — {rationale}",
            config.max_attempts
        );

        let solution = ScoredSolution {
            messages,
            score,
            rationale,
            attempt,
        };

        // Early exit if score is good enough
        if score >= config.accept_threshold {
            return Ok(solution);
        }

        solutions.push(solution);
    }

    // Return the best-scoring solution
    solutions
        .into_iter()
        .max_by(|a, b| {
            a.score
                .partial_cmp(&b.score)
                .unwrap_or(std::cmp::Ordering::Equal)
        })
        .ok_or_else(|| "No solutions produced".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::ToolResult;

    #[tokio::test]
    async fn heuristic_reviewer_scores_good_attempt() {
        let reviewer = HeuristicReviewer;
        let mut msg = Message::new(Role::Assistant, "I fixed the bug by editing the file.");
        msg.tool_calls.push(ava_types::ToolCall {
            id: "1".to_string(),
            name: "edit".to_string(),
            arguments: serde_json::json!({}),
        });
        msg.tool_results.push(ToolResult {
            call_id: "1".to_string(),
            content: "ok".to_string(),
            is_error: false,
        });

        let messages = vec![Message::new(Role::User, "fix the bug"), msg];

        let (score, _rationale) = reviewer.review("fix the bug", &messages).await.unwrap();
        assert!(score > 0.6, "Good attempt should score > 0.6, got {score}");
    }

    #[tokio::test]
    async fn heuristic_reviewer_penalizes_errors() {
        let reviewer = HeuristicReviewer;
        let mut msg = Message::new(Role::Assistant, "Failed");
        msg.tool_results.push(ToolResult {
            call_id: "1".to_string(),
            content: "error: file not found".to_string(),
            is_error: true,
        });
        msg.tool_results.push(ToolResult {
            call_id: "2".to_string(),
            content: "error: permission denied".to_string(),
            is_error: true,
        });

        let messages = vec![Message::new(Role::User, "goal"), msg];
        let (score, _) = reviewer.review("goal", &messages).await.unwrap();
        assert!(
            score <= 0.5,
            "Errored attempt should score <= 0.5, got {score}"
        );
    }

    #[tokio::test]
    async fn review_and_select_picks_best() {
        let reviewer = HeuristicReviewer;
        let config = ReviewerConfig {
            max_attempts: 3,
            accept_threshold: 0.99, // Force all attempts to run
        };

        let mut call_count = 0;
        let result = review_and_select(&config, &reviewer, "test goal", |attempt| {
            call_count += 1;
            async move {
                let mut messages = vec![Message::new(Role::User, "test goal")];
                // Attempt 2 gets the best response
                if attempt == 2 {
                    let mut msg = Message::new(
                        Role::Assistant,
                        "Comprehensive solution with detailed explanation of the changes.",
                    );
                    msg.tool_calls.push(ava_types::ToolCall {
                        id: "1".to_string(),
                        name: "edit".to_string(),
                        arguments: serde_json::json!({}),
                    });
                    messages.push(msg);
                } else {
                    messages.push(Message::new(Role::Assistant, "ok"));
                }
                Ok(messages)
            }
        })
        .await
        .unwrap();

        assert_eq!(result.attempt, 2, "Should select attempt 2 as best");
        assert!(result.score > 0.5);
    }

    #[tokio::test]
    async fn review_and_select_early_exit() {
        let reviewer = HeuristicReviewer;
        let config = ReviewerConfig {
            max_attempts: 5,
            accept_threshold: 0.5, // Low threshold — first decent attempt accepted
        };

        let mut attempts_run = 0;
        let result = review_and_select(&config, &reviewer, "goal", |_attempt| {
            attempts_run += 1;
            async move {
                let mut msg = Message::new(Role::Assistant, "Good enough solution with details.");
                msg.tool_calls.push(ava_types::ToolCall {
                    id: "1".to_string(),
                    name: "read".to_string(),
                    arguments: serde_json::json!({}),
                });
                Ok(vec![Message::new(Role::User, "goal"), msg])
            }
        })
        .await
        .unwrap();

        assert_eq!(result.attempt, 1, "Should accept first attempt");
        assert!(result.score >= 0.5);
    }

    #[test]
    fn default_config() {
        let config = ReviewerConfig::default();
        assert_eq!(config.max_attempts, 3);
        assert!((config.accept_threshold - 0.85).abs() < f64::EPSILON);
    }
}
