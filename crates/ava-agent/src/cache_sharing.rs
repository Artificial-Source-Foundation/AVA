//! CacheSafeParams — enables prompt cache sharing between parent and sub-agents.
//!
//! After each successful LLM call the agent saves its cache-safe parameters
//! (system prompt, tool schema hash, model, thinking config hash). When spawning
//! sub-agents, the parent can pass these params so the child reuses the same
//! prompt prefix, avoiding an expensive cache miss on providers that support
//! prompt caching (e.g., Anthropic `cache_control`).

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};

use serde::{Deserialize, Serialize};

/// Parameters that determine prompt cache identity. Two agents with matching
/// `CacheSafeParams` can share the provider-side prompt cache.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct CacheSafeParams {
    /// The full system prompt text used for the LLM call.
    pub system_prompt: String,
    /// Hash of the serialized tool schemas (order-independent via sorted names).
    pub tool_schemas_hash: u64,
    /// Model identifier (e.g., "claude-sonnet-4-20250514").
    pub model: String,
    /// Hash of the thinking configuration (level + budget).
    pub thinking_config_hash: u64,
}

impl CacheSafeParams {
    /// Build cache-safe params from the current agent state.
    pub fn new(
        system_prompt: String,
        tool_schemas_hash: u64,
        model: String,
        thinking_config_hash: u64,
    ) -> Self {
        Self {
            system_prompt,
            tool_schemas_hash,
            model,
            thinking_config_hash,
        }
    }

    /// Check whether a sub-agent's configuration matches this parent's cache params.
    /// If they match, the sub-agent can reuse the parent's system prompt prefix
    /// for better cache hit rates.
    pub fn matches(&self, other: &CacheSafeParams) -> bool {
        self.model == other.model
            && self.tool_schemas_hash == other.tool_schemas_hash
            && self.thinking_config_hash == other.thinking_config_hash
    }
}

/// Compute a stable hash of tool schemas for cache identity.
///
/// Sorts tool names before hashing so insertion order doesn't matter.
pub fn hash_tool_schemas(tool_defs: &[ava_types::Tool]) -> u64 {
    let mut names: Vec<&str> = tool_defs.iter().map(|t| t.name.as_str()).collect();
    names.sort_unstable();
    let mut hasher = DefaultHasher::new();
    for name in &names {
        name.hash(&mut hasher);
    }
    // Also hash the total count to distinguish subsets
    names.len().hash(&mut hasher);
    hasher.finish()
}

/// Compute a stable hash of thinking configuration.
pub fn hash_thinking_config(level: &ava_types::ThinkingLevel, budget: Option<u32>) -> u64 {
    let mut hasher = DefaultHasher::new();
    format!("{level:?}").hash(&mut hasher);
    budget.hash(&mut hasher);
    hasher.finish()
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::ThinkingLevel;

    fn make_tool(name: &str) -> ava_types::Tool {
        ava_types::Tool {
            name: name.to_string(),
            description: format!("{name} tool"),
            parameters: serde_json::json!({}),
        }
    }

    #[test]
    fn params_saved_and_retrieved() {
        let params = CacheSafeParams::new(
            "You are a helpful assistant.".to_string(),
            12345,
            "claude-sonnet-4".to_string(),
            67890,
        );
        assert_eq!(params.system_prompt, "You are a helpful assistant.");
        assert_eq!(params.model, "claude-sonnet-4");
    }

    #[test]
    fn matching_params_detected() {
        let parent = CacheSafeParams::new("prompt".to_string(), 100, "model-a".to_string(), 200);
        let child = CacheSafeParams::new(
            "different prompt".to_string(),
            100,
            "model-a".to_string(),
            200,
        );
        // matches() checks model, tool hash, thinking hash — not system prompt text
        assert!(parent.matches(&child));
    }

    #[test]
    fn mismatched_model_not_matching() {
        let parent = CacheSafeParams::new("prompt".to_string(), 100, "model-a".to_string(), 200);
        let child = CacheSafeParams::new("prompt".to_string(), 100, "model-b".to_string(), 200);
        assert!(!parent.matches(&child));
    }

    #[test]
    fn mismatched_tool_hash_not_matching() {
        let parent = CacheSafeParams::new("prompt".to_string(), 100, "model-a".to_string(), 200);
        let child = CacheSafeParams::new("prompt".to_string(), 999, "model-a".to_string(), 200);
        assert!(!parent.matches(&child));
    }

    #[test]
    fn tool_schema_hash_is_order_independent() {
        let tools_a = vec![make_tool("read"), make_tool("write"), make_tool("bash")];
        let tools_b = vec![make_tool("bash"), make_tool("read"), make_tool("write")];
        assert_eq!(hash_tool_schemas(&tools_a), hash_tool_schemas(&tools_b));
    }

    #[test]
    fn tool_schema_hash_differs_for_different_sets() {
        let tools_a = vec![make_tool("read"), make_tool("write")];
        let tools_b = vec![make_tool("read"), make_tool("bash")];
        assert_ne!(hash_tool_schemas(&tools_a), hash_tool_schemas(&tools_b));
    }

    #[test]
    fn thinking_config_hash_differs_for_different_levels() {
        let h1 = hash_thinking_config(&ThinkingLevel::Off, None);
        let h2 = hash_thinking_config(&ThinkingLevel::Medium, None);
        assert_ne!(h1, h2);
    }

    #[test]
    fn thinking_config_hash_differs_for_different_budgets() {
        let h1 = hash_thinking_config(&ThinkingLevel::Medium, Some(1000));
        let h2 = hash_thinking_config(&ThinkingLevel::Medium, Some(5000));
        assert_ne!(h1, h2);
    }

    #[test]
    fn sub_agent_receives_parent_params() {
        let parent_params = CacheSafeParams::new(
            "system prompt".to_string(),
            hash_tool_schemas(&[make_tool("read"), make_tool("write")]),
            "claude-sonnet-4".to_string(),
            hash_thinking_config(&ThinkingLevel::Medium, Some(4000)),
        );

        // Simulate sub-agent building its own params with same config
        let child_params = CacheSafeParams::new(
            "child system prompt".to_string(), // different prompt text
            hash_tool_schemas(&[make_tool("read"), make_tool("write")]),
            "claude-sonnet-4".to_string(),
            hash_thinking_config(&ThinkingLevel::Medium, Some(4000)),
        );

        // Sub-agent can use parent's system_prompt since other params match
        assert!(parent_params.matches(&child_params));
    }

    #[test]
    fn mismatch_falls_back_to_own_prompt() {
        let parent_params = CacheSafeParams::new(
            "parent prompt".to_string(),
            hash_tool_schemas(&[make_tool("read"), make_tool("write")]),
            "claude-sonnet-4".to_string(),
            hash_thinking_config(&ThinkingLevel::Medium, Some(4000)),
        );

        // Child uses a different model
        let child_params = CacheSafeParams::new(
            "child prompt".to_string(),
            hash_tool_schemas(&[make_tool("read"), make_tool("write")]),
            "gpt-5.4".to_string(),
            hash_thinking_config(&ThinkingLevel::Medium, Some(4000)),
        );

        // Mismatch — child should use its own prompt
        assert!(!parent_params.matches(&child_params));
    }
}
