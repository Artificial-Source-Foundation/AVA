//! Cache break detection — detects when request parameters change in ways that
//! invalidate prompt caching, and reports the estimated token cost.
//!
//! Uses fast hashing to compare system prompt, tool schemas, model, thinking
//! config, and beta headers between turns. Only reports breaks estimated to
//! cost more than 2000 tokens.

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};

/// Minimum estimated token impact before a cache break is reported.
const MIN_TOKEN_IMPACT: usize = 2000;

/// Report of a detected cache break with changed fields and estimated impact.
#[derive(Debug, Clone)]
pub struct CacheBreakReport {
    /// Which fields changed (e.g., "system_prompt", "tool_schemas", "model").
    pub changed_fields: Vec<String>,
    /// Estimated tokens that will need to be re-processed due to the break.
    pub estimated_token_impact: usize,
}

/// Detects cache-invalidating changes between LLM requests by hashing key
/// parameters and comparing them across turns.
#[derive(Debug, Default)]
pub struct CacheBreakDetector {
    system_prompt_hash: Option<u64>,
    tool_schemas_hash: Option<u64>,
    model_hash: Option<u64>,
    thinking_config_hash: Option<u64>,
    beta_headers_hash: Option<u64>,
    /// Estimated total tokens for the cached prefix (system + tools).
    estimated_cached_tokens: usize,
}

impl CacheBreakDetector {
    pub fn new() -> Self {
        Self::default()
    }

    /// Update the detector with current request parameters and check if any
    /// cacheable fields changed. Returns a report if the estimated impact
    /// exceeds the minimum threshold.
    pub fn check_break(
        &mut self,
        system_prompt: &str,
        tool_schemas: &[String],
        model: &str,
        thinking_config: &str,
        beta_headers: &[String],
    ) -> Option<CacheBreakReport> {
        let new_system_hash = fast_hash(system_prompt);
        let new_tools_hash = fast_hash_slice(tool_schemas);
        let new_model_hash = fast_hash(model);
        let new_thinking_hash = fast_hash(thinking_config);
        let new_beta_hash = fast_hash_slice(beta_headers);

        // First call — initialize baseline, no report
        if self.system_prompt_hash.is_none() {
            self.system_prompt_hash = Some(new_system_hash);
            self.tool_schemas_hash = Some(new_tools_hash);
            self.model_hash = Some(new_model_hash);
            self.thinking_config_hash = Some(new_thinking_hash);
            self.beta_headers_hash = Some(new_beta_hash);
            // Rough estimate: 4 chars per token
            self.estimated_cached_tokens =
                (system_prompt.len() + tool_schemas.iter().map(|s| s.len()).sum::<usize>()) / 4;
            return None;
        }

        let mut changed = Vec::new();
        let mut token_impact = 0usize;

        if self.system_prompt_hash != Some(new_system_hash) {
            changed.push("system_prompt".to_string());
            token_impact += system_prompt.len() / 4;
        }
        if self.tool_schemas_hash != Some(new_tools_hash) {
            changed.push("tool_schemas".to_string());
            token_impact += tool_schemas.iter().map(|s| s.len()).sum::<usize>() / 4;
        }
        if self.model_hash != Some(new_model_hash) {
            changed.push("model".to_string());
            // Model change invalidates the entire cached prefix
            token_impact += self.estimated_cached_tokens;
        }
        if self.thinking_config_hash != Some(new_thinking_hash) {
            changed.push("thinking_config".to_string());
            // Thinking config change may invalidate the entire cached prefix
            token_impact += self.estimated_cached_tokens;
        }
        if self.beta_headers_hash != Some(new_beta_hash) {
            changed.push("beta_headers".to_string());
            // Beta header changes can invalidate the cached prefix
            token_impact += self.estimated_cached_tokens;
        }

        // Update stored hashes
        self.system_prompt_hash = Some(new_system_hash);
        self.tool_schemas_hash = Some(new_tools_hash);
        self.model_hash = Some(new_model_hash);
        self.thinking_config_hash = Some(new_thinking_hash);
        self.beta_headers_hash = Some(new_beta_hash);
        self.estimated_cached_tokens =
            (system_prompt.len() + tool_schemas.iter().map(|s| s.len()).sum::<usize>()) / 4;

        if changed.is_empty() || token_impact < MIN_TOKEN_IMPACT {
            return None;
        }

        tracing::info!(
            fields = ?changed,
            tokens = token_impact,
            "cache break detected"
        );

        Some(CacheBreakReport {
            changed_fields: changed,
            estimated_token_impact: token_impact,
        })
    }
}

/// Fast hash using the standard library DefaultHasher (SipHash).
fn fast_hash(value: &str) -> u64 {
    let mut hasher = DefaultHasher::new();
    value.hash(&mut hasher);
    hasher.finish()
}

/// Fast hash for a slice of strings — order-sensitive.
fn fast_hash_slice(values: &[String]) -> u64 {
    let mut hasher = DefaultHasher::new();
    for v in values {
        v.hash(&mut hasher);
    }
    hasher.finish()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn no_change_no_report() {
        let mut detector = CacheBreakDetector::new();
        let prompt = "You are AVA, an AI coding assistant.";
        let tools = vec!["read_tool_schema".to_string()];

        // First call initializes
        assert!(detector
            .check_break(prompt, &tools, "claude-sonnet-4.6", "high", &[])
            .is_none());

        // Same params again — no report
        assert!(detector
            .check_break(prompt, &tools, "claude-sonnet-4.6", "high", &[])
            .is_none());
    }

    #[test]
    fn system_prompt_change_detected() {
        let mut detector = CacheBreakDetector::new();
        // Use a large enough prompt to exceed the 2000-token threshold
        let prompt1 = "x".repeat(12000); // ~3000 tokens
        let tools = vec!["tool_schema".to_string()];

        detector.check_break(&prompt1, &tools, "claude-sonnet-4.6", "high", &[]);

        let prompt2 = "y".repeat(12000);
        let report = detector
            .check_break(&prompt2, &tools, "claude-sonnet-4.6", "high", &[])
            .expect("should detect system prompt change");

        assert!(report.changed_fields.contains(&"system_prompt".to_string()));
        assert!(report.estimated_token_impact >= MIN_TOKEN_IMPACT);
    }

    #[test]
    fn tool_added_detected() {
        let mut detector = CacheBreakDetector::new();
        let prompt = "x".repeat(12000);
        // Use large tool schemas so the change exceeds the 2000-token threshold
        let tools1 = vec!["x".repeat(10000)];

        detector.check_break(&prompt, &tools1, "claude-sonnet-4.6", "high", &[]);

        let tools2 = vec!["x".repeat(10000), "y".repeat(10000)];
        let report = detector
            .check_break(&prompt, &tools2, "claude-sonnet-4.6", "high", &[])
            .expect("should detect tool change");

        assert!(report.changed_fields.contains(&"tool_schemas".to_string()));
    }

    #[test]
    fn model_change_detected() {
        let mut detector = CacheBreakDetector::new();
        let prompt = "x".repeat(12000);
        let tools = vec!["tool".to_string()];

        detector.check_break(&prompt, &tools, "claude-sonnet-4.6", "high", &[]);

        let report = detector
            .check_break(&prompt, &tools, "gpt-5.4", "high", &[])
            .expect("should detect model change");

        assert!(report.changed_fields.contains(&"model".to_string()));
    }

    #[test]
    fn small_change_below_threshold_not_reported() {
        let mut detector = CacheBreakDetector::new();
        // Small prompt — changes will be below the 2000-token threshold
        let prompt = "small prompt";
        let tools: Vec<String> = vec![];

        detector.check_break(prompt, &tools, "model", "off", &[]);

        // Change something small
        let result = detector.check_break("different small prompt", &tools, "model", "off", &[]);
        // Should be None because estimated impact < 2000 tokens
        assert!(result.is_none());
    }
}
