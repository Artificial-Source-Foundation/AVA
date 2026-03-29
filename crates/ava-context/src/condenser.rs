use std::sync::Arc;

use ava_types::Message;

use crate::error::{ContextError, Result};
use crate::strategies::{
    AmortizedForgettingStrategy, AsyncCondensationStrategy, CondensationStrategy,
    ObservationMaskingStrategy, RelevanceStrategy, SlidingWindowStrategy, SummarizationStrategy,
    Summarizer, ToolTruncationStrategy,
};
use crate::token_tracker::TokenTracker;
use crate::types::{CondensationResult, CondenserConfig};

pub struct Condenser {
    config: CondenserConfig,
    tracker: TokenTracker,
    strategies: Vec<Box<dyn CondensationStrategy>>,
}

impl Condenser {
    pub fn new(config: CondenserConfig, strategies: Vec<Box<dyn CondensationStrategy>>) -> Self {
        Self {
            tracker: TokenTracker::new(config.max_tokens),
            config,
            strategies,
        }
    }

    pub fn condense(&mut self, messages: &[Message]) -> Result<CondensationResult> {
        self.tracker.reset();
        self.tracker.add_messages(messages);
        let before_tokens = self.tracker.current_tokens;

        if !self.tracker.is_over_limit() {
            return Ok(CondensationResult {
                messages: messages.to_vec(),
                estimated_tokens: self.tracker.current_tokens,
                strategy: "none".to_string(),
                compacted_messages: Vec::new(),
            });
        }

        let mut current = messages.to_vec();

        for strategy in &self.strategies {
            current = strategy.condense(&current, self.config.target_tokens)?;
            self.tracker.reset();
            self.tracker.add_messages(&current);

            if !self.tracker.is_over_limit() {
                tracing::info!(
                    "Context condensed: {} -> {} tokens (strategy: {})",
                    before_tokens,
                    self.tracker.current_tokens,
                    strategy.name()
                );
                let compacted = mark_compacted_messages(messages, &current);
                return Ok(CondensationResult {
                    messages: current,
                    estimated_tokens: self.tracker.current_tokens,
                    strategy: strategy.name().to_string(),
                    compacted_messages: compacted,
                });
            }
        }

        Err(ContextError::TokenBudgetExceeded(
            self.tracker.current_tokens,
            self.config.max_tokens,
        ))
    }

    pub fn force_condense(&mut self, messages: &[Message]) -> Result<CondensationResult> {
        self.tracker.reset();
        self.tracker.add_messages(messages);
        let before_tokens = self.tracker.current_tokens;
        let mut current = messages.to_vec();

        for strategy in &self.strategies {
            current = strategy.condense(&current, self.config.target_tokens)?;
            self.tracker.reset();
            self.tracker.add_messages(&current);

            if self.tracker.current_tokens <= self.config.target_tokens {
                let compacted = mark_compacted_messages(messages, &current);
                return Ok(CondensationResult {
                    messages: current,
                    estimated_tokens: self.tracker.current_tokens,
                    strategy: strategy.name().to_string(),
                    compacted_messages: compacted,
                });
            }
        }

        let compacted = mark_compacted_messages(messages, &current);
        tracing::info!(
            before_tokens,
            after_tokens = self.tracker.current_tokens,
            "forced condensation completed without reaching target"
        );
        Ok(CondensationResult {
            messages: current,
            estimated_tokens: self.tracker.current_tokens,
            strategy: "forced".to_string(),
            compacted_messages: compacted,
        })
    }
}

/// Hybrid condenser with a 3-stage pipeline:
/// 1. Sync strategies (tool truncation)
/// 2. Async strategies (LLM summarization)
/// 3. Final sync fallback (sliding window)
pub struct HybridCondenser {
    config: CondenserConfig,
    tracker: TokenTracker,
    sync_strategies: Vec<Box<dyn CondensationStrategy>>,
    async_strategies: Vec<Box<dyn AsyncCondensationStrategy>>,
    fallback_strategies: Vec<Box<dyn CondensationStrategy>>,
}

impl HybridCondenser {
    pub fn new(
        config: CondenserConfig,
        sync_strategies: Vec<Box<dyn CondensationStrategy>>,
        async_strategies: Vec<Box<dyn AsyncCondensationStrategy>>,
        fallback_strategies: Vec<Box<dyn CondensationStrategy>>,
    ) -> Self {
        Self {
            tracker: TokenTracker::new(config.max_tokens),
            config,
            sync_strategies,
            async_strategies,
            fallback_strategies,
        }
    }

    /// Forward a previous summary to any summarization strategies for iterative compaction.
    pub fn set_previous_summary(&mut self, summary: Option<String>) {
        for strategy in &mut self.async_strategies {
            strategy.set_previous_summary(summary.clone());
        }
    }

    pub async fn condense(&mut self, messages: &[Message]) -> Result<CondensationResult> {
        self.tracker.reset();
        self.tracker.add_messages(messages);
        let before_tokens = self.tracker.current_tokens;

        if !self.tracker.is_over_limit() {
            return Ok(CondensationResult {
                messages: messages.to_vec(),
                estimated_tokens: self.tracker.current_tokens,
                strategy: "none".to_string(),
                compacted_messages: Vec::new(),
            });
        }

        let mut current = messages.to_vec();

        // Stage 1: sync strategies (e.g. tool truncation)
        for strategy in &self.sync_strategies {
            current = strategy.condense(&current, self.config.target_tokens)?;
            self.tracker.reset();
            self.tracker.add_messages(&current);

            if !self.tracker.is_over_limit() {
                let compacted = mark_compacted_messages(messages, &current);
                return Ok(CondensationResult {
                    messages: current,
                    estimated_tokens: self.tracker.current_tokens,
                    strategy: strategy.name().to_string(),
                    compacted_messages: compacted,
                });
            }
        }

        // Stage 2: async strategies (e.g. LLM summarization)
        for strategy in &self.async_strategies {
            current = strategy
                .condense(&current, self.config.target_tokens)
                .await?;
            self.tracker.reset();
            self.tracker.add_messages(&current);

            if !self.tracker.is_over_limit() {
                let compacted = mark_compacted_messages(messages, &current);
                return Ok(CondensationResult {
                    messages: current,
                    estimated_tokens: self.tracker.current_tokens,
                    strategy: strategy.name().to_string(),
                    compacted_messages: compacted,
                });
            }
        }

        // Stage 3: fallback sync strategies (e.g. sliding window)
        for strategy in &self.fallback_strategies {
            current = strategy.condense(&current, self.config.target_tokens)?;
            self.tracker.reset();
            self.tracker.add_messages(&current);

            if !self.tracker.is_over_limit() {
                tracing::info!(
                    "Context condensed: {} -> {} tokens (strategy: {})",
                    before_tokens,
                    self.tracker.current_tokens,
                    strategy.name()
                );
                let compacted = mark_compacted_messages(messages, &current);
                return Ok(CondensationResult {
                    messages: current,
                    estimated_tokens: self.tracker.current_tokens,
                    strategy: strategy.name().to_string(),
                    compacted_messages: compacted,
                });
            }
        }

        Err(ContextError::TokenBudgetExceeded(
            self.tracker.current_tokens,
            self.config.max_tokens,
        ))
    }

    pub async fn force_condense(&mut self, messages: &[Message]) -> Result<CondensationResult> {
        self.tracker.reset();
        self.tracker.add_messages(messages);
        let before_tokens = self.tracker.current_tokens;
        let mut current = messages.to_vec();

        for strategy in &self.sync_strategies {
            current = strategy.condense(&current, self.config.target_tokens)?;
            self.tracker.reset();
            self.tracker.add_messages(&current);

            if self.tracker.current_tokens <= self.config.target_tokens {
                let compacted = mark_compacted_messages(messages, &current);
                return Ok(CondensationResult {
                    messages: current,
                    estimated_tokens: self.tracker.current_tokens,
                    strategy: strategy.name().to_string(),
                    compacted_messages: compacted,
                });
            }
        }

        for strategy in &self.async_strategies {
            current = strategy
                .condense(&current, self.config.target_tokens)
                .await?;
            self.tracker.reset();
            self.tracker.add_messages(&current);

            if self.tracker.current_tokens <= self.config.target_tokens {
                let compacted = mark_compacted_messages(messages, &current);
                return Ok(CondensationResult {
                    messages: current,
                    estimated_tokens: self.tracker.current_tokens,
                    strategy: strategy.name().to_string(),
                    compacted_messages: compacted,
                });
            }
        }

        for strategy in &self.fallback_strategies {
            current = strategy.condense(&current, self.config.target_tokens)?;
            self.tracker.reset();
            self.tracker.add_messages(&current);

            if self.tracker.current_tokens <= self.config.target_tokens {
                let compacted = mark_compacted_messages(messages, &current);
                return Ok(CondensationResult {
                    messages: current,
                    estimated_tokens: self.tracker.current_tokens,
                    strategy: strategy.name().to_string(),
                    compacted_messages: compacted,
                });
            }
        }

        let compacted = mark_compacted_messages(messages, &current);
        tracing::info!(
            before_tokens,
            after_tokens = self.tracker.current_tokens,
            "forced hybrid condensation completed without reaching target"
        );
        Ok(CondensationResult {
            messages: current,
            estimated_tokens: self.tracker.current_tokens,
            strategy: "forced".to_string(),
            compacted_messages: compacted,
        })
    }
}

/// Identify messages from `original` that are not present in `condensed` (by ID)
/// and return them with `agent_visible = false` and `original_content` preserved.
/// These are the messages that were dropped during compaction.
pub fn mark_compacted_messages_pub(original: &[Message], condensed: &[Message]) -> Vec<Message> {
    mark_compacted_messages(original, condensed)
}

fn mark_compacted_messages(original: &[Message], condensed: &[Message]) -> Vec<Message> {
    use std::collections::HashSet;
    let retained_ids: HashSet<uuid::Uuid> = condensed.iter().map(|m| m.id).collect();
    original
        .iter()
        .filter(|m| !retained_ids.contains(&m.id))
        .map(|m| {
            let mut compacted = m.clone();
            compacted.mark_compacted();
            compacted
        })
        .collect()
}

pub fn create_condenser(max_tokens: usize) -> Condenser {
    let config = CondenserConfig {
        max_tokens,
        target_tokens: max_tokens.saturating_mul(3) / 4,
        max_tool_content_chars: 2000,
        ..Default::default()
    };
    Condenser::new(
        config,
        vec![
            Box::new(ToolTruncationStrategy::default()),
            Box::new(SlidingWindowStrategy),
        ],
    )
}

/// Create a hybrid condenser with the 3-stage pipeline.
/// If `summarizer` is `None` and `enable_summarization` is true, uses heuristic fallback.
/// If `relevance_scores` is provided, inserts a relevance strategy between tool truncation
/// and summarization.
pub fn create_hybrid_condenser(
    config: CondenserConfig,
    summarizer: Option<Arc<dyn Summarizer>>,
) -> HybridCondenser {
    create_hybrid_condenser_with_relevance(config, summarizer, None)
}

/// Create a hybrid condenser with optional relevance-aware scoring.
pub fn create_hybrid_condenser_with_relevance(
    config: CondenserConfig,
    summarizer: Option<Arc<dyn Summarizer>>,
    relevance_scores: Option<std::collections::HashMap<String, f64>>,
) -> HybridCondenser {
    // Stage 1 sync pipeline: observation masking → tool truncation → relevance
    let mut sync_strategies: Vec<Box<dyn CondensationStrategy>> = vec![
        Box::new(ObservationMaskingStrategy::new(
            config.preserve_recent_messages,
        )),
        Box::new(ToolTruncationStrategy::new(config.max_tool_content_chars)),
    ];

    if let Some(scores) = relevance_scores {
        sync_strategies.push(Box::new(RelevanceStrategy::new(
            scores,
            config.preserve_recent_messages,
        )));
    }

    // Stage 2 async: LLM summarization
    let async_strategies: Vec<Box<dyn AsyncCondensationStrategy>> = if config.enable_summarization {
        vec![Box::new(SummarizationStrategy::new(
            summarizer,
            config.summarization_batch_size,
            config.preserve_recent_messages,
            config.preserve_recent_turns,
            config.focus.clone(),
        ))]
    } else {
        vec![]
    };

    // Stage 3 fallback: amortized forgetting → sliding window
    let fallback_strategies: Vec<Box<dyn CondensationStrategy>> = vec![
        Box::new(AmortizedForgettingStrategy::default()),
        Box::new(SlidingWindowStrategy),
    ];

    HybridCondenser::new(
        config,
        sync_strategies,
        async_strategies,
        fallback_strategies,
    )
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role, ToolResult};

    use super::*;

    #[test]
    fn no_condensation_when_under_limit() {
        let mut condenser = create_condenser(10_000);
        let messages = vec![Message::new(Role::User, "hello")];
        let result = condenser.condense(&messages).unwrap();
        assert_eq!(result.strategy, "none");
        assert_eq!(result.messages.len(), 1);
    }

    #[test]
    fn applies_strategies_when_over_limit() {
        let mut condenser = create_condenser(20);
        // Use multi-word content so word-based token estimator counts enough tokens
        let words = (0..100)
            .map(|i| format!("word{i}"))
            .collect::<Vec<_>>()
            .join(" ");
        let tool_words = (0..200)
            .map(|i| format!("val{i}"))
            .collect::<Vec<_>>()
            .join(" ");
        let mut messages = vec![Message::new(Role::User, words)];
        messages[0].tool_results.push(ToolResult {
            call_id: "1".to_string(),
            content: tool_words,
            is_error: false,
        });

        let result = condenser.condense(&messages).unwrap();
        assert_ne!(result.strategy, "none");
    }

    #[tokio::test]
    async fn hybrid_no_condensation_when_under_limit() {
        let config = CondenserConfig {
            max_tokens: 10_000,
            target_tokens: 7_500,
            enable_summarization: false,
            ..Default::default()
        };
        let mut condenser = create_hybrid_condenser(config, None);
        let messages = vec![Message::new(Role::User, "hello")];
        let result = condenser.condense(&messages).await.unwrap();
        assert_eq!(result.strategy, "none");
    }

    #[tokio::test]
    async fn hybrid_pipeline_tool_trunc_then_summarize_then_sliding() {
        let config = CondenserConfig {
            max_tokens: 30,
            target_tokens: 20,
            max_tool_content_chars: 50,
            enable_summarization: true,
            summarization_batch_size: 3,
            preserve_recent_messages: 2,
            compaction_threshold_pct: 0.8,
            ..Default::default()
        };
        let mut condenser = create_hybrid_condenser(config, None);

        let mut messages = Vec::new();
        messages.push(Message::new(Role::System, "system prompt"));
        for i in 0..8 {
            messages.push(Message::new(
                Role::User,
                format!("message {i} with some content"),
            ));
        }

        let result = condenser.condense(&messages).await.unwrap();
        assert!(result.messages.len() < messages.len());
        assert_ne!(result.strategy, "none");
    }

    #[tokio::test]
    async fn hybrid_with_summarization_disabled_skips_to_sliding_window() {
        let config = CondenserConfig {
            max_tokens: 30,
            target_tokens: 20,
            enable_summarization: false,
            ..Default::default()
        };
        let mut condenser = create_hybrid_condenser(config, None);

        let mut messages = Vec::new();
        for _ in 0..8 {
            messages.push(Message::new(Role::User, "x".repeat(50)));
        }

        let result = condenser.condense(&messages).await.unwrap();
        assert_eq!(result.strategy, "sliding_window");
    }
}
