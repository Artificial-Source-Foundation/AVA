//! Memory enrichment for agent goals.
//!
//! Extracts relevant memories and learned project patterns from the goal text
//! to enrich the agent's context. Also detects preference-like statements in
//! user goals and persists them as learned patterns.

use ava_memory::MemorySystem;
use ava_types::MEMORY_BLOCK_MAX_CHARS;
use tracing::warn;

/// Enrich a goal string with relevant memories from the memory system.
///
/// Searches for keywords in the goal, retrieves matching memories, and appends
/// them as context below the original goal text. Returns the original goal
/// unchanged if no relevant memories are found.
pub async fn enrich_goal_with_memories(memory: &MemorySystem, goal: &str) -> String {
    let keywords = extract_goal_keywords(goal);
    if keywords.is_empty() {
        return goal.to_string();
    }
    let query = keywords.join(" ");
    let Ok(memories) = memory.search(&query) else {
        return goal.to_string();
    };
    let entries: Vec<String> = memories
        .into_iter()
        .take(5)
        .map(|m| format!("- [{}]: {}", m.key, m.value))
        .collect();

    let learned = memory
        .search_confirmed_learned(&query, 5)
        .unwrap_or_default();
    let learned_entries: Vec<String> = learned
        .into_iter()
        .map(|m| format!("- [{}]: {}", m.key, m.value))
        .collect();

    if entries.is_empty() && learned_entries.is_empty() {
        return goal.to_string();
    }

    let mut blocks = Vec::new();
    if !entries.is_empty() {
        blocks.push(format!("Relevant memories:\n{}", entries.join("\n")));
    }
    if !learned_entries.is_empty() {
        blocks.push(format!(
            "Confirmed learned project memories:\n{}",
            learned_entries.join("\n")
        ));
    }

    let memory_block = blocks.join("\n\n");
    let memory_block = if memory_block.len() > MEMORY_BLOCK_MAX_CHARS {
        let truncated: String = memory_block.chars().take(MEMORY_BLOCK_MAX_CHARS).collect();
        format!("{truncated}...")
    } else {
        memory_block
    };
    format!("{goal}\n\n{memory_block}")
}

/// Scan a goal for preference-like statements and persist them as learned patterns.
pub fn learn_project_patterns_from_goal(memory: &MemorySystem, goal: &str) {
    let observations = extract_project_pattern_observations(goal);
    for observation in observations {
        if let Err(err) = memory.observe_learned_pattern(
            &observation.key,
            &observation.value,
            &observation.source_excerpt,
            observation.confidence,
        ) {
            warn!(error = %err, "failed to persist learned project memory");
        }
    }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

const STOPWORDS: &[&str] = &[
    "a", "an", "the", "is", "are", "was", "were", "be", "been", "being", "have", "has", "had",
    "do", "does", "did", "will", "would", "could", "should", "may", "might", "can", "shall", "to",
    "of", "in", "for", "on", "with", "at", "by", "from", "as", "into", "about", "it", "its",
    "this", "that", "and", "or", "but", "not", "no", "so", "if", "then", "than", "too", "very",
    "just", "how", "what", "which", "who", "when", "where", "why", "all", "each", "me", "my", "i",
    "you", "your", "we", "our", "he", "she", "they", "them",
];

fn extract_goal_keywords(goal: &str) -> Vec<String> {
    goal.split(|c: char| !c.is_alphanumeric() && c != '_' && c != '-')
        .map(|s| s.trim().to_lowercase())
        .filter(|s| s.len() > 2 && !STOPWORDS.contains(&s.as_str()))
        .collect()
}

#[derive(Debug, Clone)]
struct LearnedPatternObservation {
    key: String,
    value: String,
    source_excerpt: String,
    confidence: f64,
}

fn extract_project_pattern_observations(goal: &str) -> Vec<LearnedPatternObservation> {
    let mut out = Vec::new();

    for sentence in split_into_sentences(goal) {
        let lower = sentence.to_lowercase();
        let (key, confidence) = if lower.starts_with("always use ") {
            ("preference.implementation", 0.85)
        } else if lower.starts_with("prefer ") {
            ("preference.general", 0.8)
        } else if lower.contains(" by default") && lower.starts_with("use ") {
            ("preference.default", 0.85)
        } else if lower.starts_with("we use ") {
            ("preference.team", 0.7)
        } else {
            continue;
        };

        if sentence.len() < 12 || sentence.len() > 180 {
            continue;
        }

        out.push(LearnedPatternObservation {
            key: key.to_string(),
            value: normalize_whitespace(&sentence),
            source_excerpt: normalize_whitespace(&sentence),
            confidence,
        });
    }

    out
}

fn split_into_sentences(text: &str) -> Vec<String> {
    text.split(['\n', '.', '!', '?'])
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .map(|s| s.to_string())
        .collect()
}

fn normalize_whitespace(text: &str) -> String {
    text.split_whitespace().collect::<Vec<_>>().join(" ")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pattern_detector_extracts_conservative_preferences() {
        let goal =
            "Prefer integration tests for boundaries.\nAlways use cargo clippy before commit.";
        let patterns = extract_project_pattern_observations(goal);
        assert_eq!(patterns.len(), 2);
        assert_eq!(patterns[0].key, "preference.general");
        assert_eq!(patterns[1].key, "preference.implementation");
    }

    #[test]
    fn pattern_detector_ignores_non_preference_sentences() {
        let goal = "Fix the bug in parser. Add docs.";
        let patterns = extract_project_pattern_observations(goal);
        assert!(patterns.is_empty());
    }

    #[test]
    fn memory_pattern_detector_finds_default_preferences() {
        let goal = "Use ripgrep by default for search.";
        let patterns = extract_project_pattern_observations(goal);
        assert_eq!(patterns.len(), 1);
        assert_eq!(patterns[0].key, "preference.default");
    }
}
