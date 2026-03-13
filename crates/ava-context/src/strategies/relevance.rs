use std::collections::HashMap;

use ava_types::{Message, Role};
use regex::Regex;

use crate::error::Result;
use crate::strategies::CondensationStrategy;
use crate::token_tracker::estimate_tokens_for_message;

/// Relevance-aware condensation strategy that scores messages by:
/// 1. PageRank score of files mentioned in the message
/// 2. Recency bonus (later messages score higher)
///
/// Always preserves the system prompt and last N messages.
pub struct RelevanceStrategy {
    pagerank: HashMap<String, f64>,
    preserve_recent: usize,
}

impl RelevanceStrategy {
    pub fn new(pagerank: HashMap<String, f64>, preserve_recent: usize) -> Self {
        Self {
            pagerank,
            preserve_recent,
        }
    }

    fn extract_paths(content: &str) -> Vec<String> {
        let re = Regex::new(r"[\w./\-]+\.\w{1,6}").unwrap();
        re.find_iter(content)
            .filter(|m| m.as_str().contains('/') || m.as_str().contains('.'))
            .map(|m| m.as_str().to_string())
            .collect()
    }

    fn score_message(&self, message: &Message, index: usize, total: usize) -> f64 {
        let paths = Self::extract_paths(&message.content);
        let pagerank_score = paths
            .iter()
            .filter_map(|p| self.pagerank.get(p))
            .copied()
            .fold(0.0_f64, f64::max);

        let recency_bonus = if total > 0 {
            0.1 * (index as f64 / total as f64)
        } else {
            0.0
        };

        pagerank_score + recency_bonus
    }
}

impl CondensationStrategy for RelevanceStrategy {
    fn name(&self) -> &'static str {
        "relevance"
    }

    fn condense(&self, messages: &[Message], max_tokens: usize) -> Result<Vec<Message>> {
        if messages.is_empty() {
            return Ok(Vec::new());
        }

        let total = messages.len();

        // Separate system prompt, protected tail, and scoreable middle
        let mut system_messages = Vec::new();
        let mut middle = Vec::new();
        let tail_start = total.saturating_sub(self.preserve_recent);

        for (i, msg) in messages.iter().enumerate() {
            if msg.role == Role::System {
                system_messages.push(msg.clone());
            } else if i >= tail_start {
                // Will be preserved unconditionally
            } else {
                middle.push((i, msg));
            }
        }

        let tail: Vec<Message> = messages[tail_start..].to_vec();

        // Budget remaining after system + tail
        let system_tokens: usize = system_messages
            .iter()
            .map(estimate_tokens_for_message)
            .sum();
        let tail_tokens: usize = tail.iter().map(estimate_tokens_for_message).sum();
        let remaining_budget = max_tokens.saturating_sub(system_tokens + tail_tokens);

        // Score and sort middle messages
        let mut scored: Vec<(f64, usize, &Message)> = middle
            .iter()
            .map(|&(i, msg)| (self.score_message(msg, i, total), i, msg))
            .collect();
        scored.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap_or(std::cmp::Ordering::Equal));

        // Greedily select highest-scored messages within budget
        let mut selected_indices = Vec::new();
        let mut used_tokens = 0;
        for (_, idx, msg) in &scored {
            let tokens = estimate_tokens_for_message(msg);
            if used_tokens + tokens <= remaining_budget {
                selected_indices.push(*idx);
                used_tokens += tokens;
            }
        }

        // Rebuild in original order
        selected_indices.sort();
        let mut result = system_messages;
        for idx in selected_indices {
            result.push(messages[idx].clone());
        }
        result.extend(tail);

        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role};

    use super::*;

    #[test]
    fn preserves_system_prompt_and_recent() {
        let mut pagerank = HashMap::new();
        pagerank.insert("src/important.rs".to_string(), 0.9);

        let strategy = RelevanceStrategy::new(pagerank, 2);

        let messages = vec![
            Message::new(Role::System, "You are an assistant"),
            Message::new(Role::User, "Tell me about src/unimportant.rs"),
            Message::new(Role::Assistant, "That file does nothing"),
            Message::new(Role::User, "Now about src/important.rs"),
            Message::new(Role::Assistant, "That is the main file"),
        ];

        let result = strategy.condense(&messages, 100).unwrap();

        // System prompt always kept
        assert_eq!(result[0].role, Role::System);
        // Last 2 messages always kept
        assert!(result
            .iter()
            .any(|m| m.content.contains("src/important.rs")));
        assert!(result.iter().any(|m| m.content.contains("main file")));
    }

    #[test]
    fn drops_low_relevance_under_tight_budget() {
        let mut pagerank = HashMap::new();
        pagerank.insert("src/core.rs".to_string(), 0.9);

        let strategy = RelevanceStrategy::new(pagerank, 1);

        let messages = vec![
            Message::new(Role::System, "system"),
            Message::new(Role::User, "irrelevant chatter about nothing"),
            Message::new(Role::User, "more irrelevant chatter about nothing"),
            Message::new(Role::User, "discuss src/core.rs implementation"),
            Message::new(Role::User, "final message"),
        ];

        // Very tight budget — should drop irrelevant messages first
        let result = strategy.condense(&messages, 30).unwrap();
        assert!(result.len() < messages.len());
        // System + final message should be present
        assert_eq!(result[0].role, Role::System);
        assert_eq!(result.last().unwrap().content, "final message");
    }

    #[test]
    fn extract_paths_finds_file_references() {
        let paths = RelevanceStrategy::extract_paths(
            "Check src/main.rs and also crates/ava-tools/src/lib.rs for details",
        );
        assert!(paths.iter().any(|p| p.contains("main.rs")));
        assert!(paths.iter().any(|p| p.contains("lib.rs")));
    }
}
