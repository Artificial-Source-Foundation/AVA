//! Build race (BG2-34, inspired by Plandex).
//!
//! Run parallel competing build/edit strategies and use the first
//! valid result. Losers are cancelled.

use std::future::Future;
use std::pin::Pin;

/// Result of a single build strategy attempt.
#[derive(Debug, Clone)]
pub struct BuildResult {
    pub strategy_name: String,
    pub content: String,
    pub success: bool,
    pub error: Option<String>,
    pub duration_ms: u64,
}

/// A build strategy that produces a result.
pub type BuildFn = Box<dyn FnOnce() -> Pin<Box<dyn Future<Output = BuildResult> + Send>> + Send>;

/// Race multiple build strategies, returning the first successful result.
///
/// All strategies are spawned concurrently. The first one to complete
/// successfully wins; remaining tasks are dropped (cancelled via tokio).
pub async fn race_builds(strategies: Vec<(String, BuildFn)>) -> Option<BuildResult> {
    if strategies.is_empty() {
        return None;
    }

    let (tx, mut rx) = tokio::sync::mpsc::channel::<BuildResult>(strategies.len());

    let mut handles = Vec::new();
    for (name, build_fn) in strategies {
        let tx = tx.clone();
        let handle = tokio::spawn(async move {
            let start = std::time::Instant::now();
            let mut result = build_fn().await;
            result.duration_ms = start.elapsed().as_millis() as u64;
            result.strategy_name = name;
            let _ = tx.send(result).await;
        });
        handles.push(handle);
    }

    // Drop sender so rx completes when all tasks finish
    drop(tx);

    let mut best: Option<BuildResult> = None;

    while let Some(result) = rx.recv().await {
        if result.success {
            // Cancel remaining tasks
            for handle in &handles {
                handle.abort();
            }
            return Some(result);
        }
        // Track best failed attempt (shortest error)
        if best.is_none() {
            best = Some(result);
        }
    }

    // No successful result — return best failed attempt
    best
}

/// Simplified race between two strategies: a fast approach and a thorough approach.
pub async fn race_fast_vs_thorough<F1, F2>(
    fast_name: &str,
    fast: F1,
    thorough_name: &str,
    thorough: F2,
) -> Option<BuildResult>
where
    F1: Future<Output = BuildResult> + Send + 'static,
    F2: Future<Output = BuildResult> + Send + 'static,
{
    let fast_name = fast_name.to_string();
    let thorough_name = thorough_name.to_string();

    let strategies: Vec<(String, BuildFn)> = vec![
        (fast_name, Box::new(move || Box::pin(fast))),
        (thorough_name, Box::new(move || Box::pin(thorough))),
    ];

    race_builds(strategies).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn first_success_wins() {
        let strategies: Vec<(String, BuildFn)> = vec![
            (
                "slow".to_string(),
                Box::new(|| {
                    Box::pin(async {
                        tokio::time::sleep(std::time::Duration::from_millis(200)).await;
                        BuildResult {
                            strategy_name: String::new(),
                            content: "slow result".to_string(),
                            success: true,
                            error: None,
                            duration_ms: 0,
                        }
                    })
                }),
            ),
            (
                "fast".to_string(),
                Box::new(|| {
                    Box::pin(async {
                        BuildResult {
                            strategy_name: String::new(),
                            content: "fast result".to_string(),
                            success: true,
                            error: None,
                            duration_ms: 0,
                        }
                    })
                }),
            ),
        ];

        let result = race_builds(strategies).await.unwrap();
        assert_eq!(result.content, "fast result");
        assert!(result.success);
    }

    #[tokio::test]
    async fn failed_attempts_return_best() {
        let strategies: Vec<(String, BuildFn)> = vec![(
            "failing".to_string(),
            Box::new(|| {
                Box::pin(async {
                    BuildResult {
                        strategy_name: String::new(),
                        content: String::new(),
                        success: false,
                        error: Some("compilation error".to_string()),
                        duration_ms: 0,
                    }
                })
            }),
        )];

        let result = race_builds(strategies).await.unwrap();
        assert!(!result.success);
        assert!(result.error.is_some());
    }

    #[tokio::test]
    async fn empty_strategies_returns_none() {
        let result = race_builds(vec![]).await;
        assert!(result.is_none());
    }

    #[tokio::test]
    async fn race_fast_vs_thorough_works() {
        let result = race_fast_vs_thorough(
            "fast-apply",
            async {
                BuildResult {
                    strategy_name: String::new(),
                    content: "quick fix".to_string(),
                    success: true,
                    error: None,
                    duration_ms: 0,
                }
            },
            "whole-file",
            async {
                tokio::time::sleep(std::time::Duration::from_millis(100)).await;
                BuildResult {
                    strategy_name: String::new(),
                    content: "full rewrite".to_string(),
                    success: true,
                    error: None,
                    duration_ms: 0,
                }
            },
        )
        .await
        .unwrap();

        assert_eq!(result.content, "quick fix");
    }
}
