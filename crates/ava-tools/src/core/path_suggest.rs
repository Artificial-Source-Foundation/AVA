use std::cmp::Ordering;
use std::path::{Path, PathBuf};

use ava_types::AvaError;

const MAX_SUGGESTIONS: usize = 3;
const MIN_SUGGESTION_SCORE: f64 = 0.55;

pub async fn missing_file_error(original_path: &str, resolved_path: &Path) -> AvaError {
    let suggestions = suggest_sibling_paths(resolved_path).await;
    let mut message = format!("file not found: {original_path}");

    if !suggestions.is_empty() {
        message.push_str("\n\nDid you mean one of these?\n");
        for suggestion in suggestions {
            message.push_str(&format!("- {}\n", suggestion.display()));
        }
        message.truncate(message.trim_end().len());
    }

    AvaError::NotFound(message)
}

async fn suggest_sibling_paths(resolved_path: &Path) -> Vec<PathBuf> {
    let Some(parent) = resolved_path.parent() else {
        return Vec::new();
    };
    let Some(needle) = resolved_path.file_name().and_then(|value| value.to_str()) else {
        return Vec::new();
    };

    let Ok(mut entries) = tokio::fs::read_dir(parent).await else {
        return Vec::new();
    };

    let needle_lower = needle.to_ascii_lowercase();
    let mut scored = Vec::new();

    while let Ok(Some(entry)) = entries.next_entry().await {
        let file_name = entry.file_name();
        let Some(candidate) = file_name.to_str() else {
            continue;
        };
        let score = suggestion_score(&needle_lower, candidate);
        if score < MIN_SUGGESTION_SCORE {
            continue;
        }
        scored.push((score, candidate.to_string(), entry.path()));
    }

    scored.sort_by(|left, right| {
        right
            .0
            .partial_cmp(&left.0)
            .unwrap_or(Ordering::Equal)
            .then_with(|| left.1.cmp(&right.1))
    });
    scored.truncate(MAX_SUGGESTIONS);

    scored.into_iter().map(|(_, _, path)| path).collect()
}

fn suggestion_score(needle_lower: &str, candidate: &str) -> f64 {
    let candidate_lower = candidate.to_ascii_lowercase();
    let ratio = similar::TextDiff::from_chars(needle_lower, &candidate_lower).ratio() as f64;
    let contains_bonus = if needle_lower.len() >= 4
        && (candidate_lower.contains(needle_lower) || needle_lower.contains(&candidate_lower))
    {
        0.15
    } else {
        0.0
    };
    let prefix_bonus = common_prefix_len(needle_lower, &candidate_lower) as f64
        / needle_lower.len().max(candidate_lower.len()).max(1) as f64
        * 0.1;

    ratio + contains_bonus + prefix_bonus
}

fn common_prefix_len(left: &str, right: &str) -> usize {
    left.chars()
        .zip(right.chars())
        .take_while(|(a, b)| a == b)
        .count()
}

#[cfg(test)]
mod tests {
    use tempfile::tempdir;

    use super::*;

    #[tokio::test]
    async fn missing_file_error_includes_similar_siblings() {
        let dir = tempdir().unwrap();
        let target = dir.path().join("config.tom");
        tokio::fs::write(dir.path().join("config.toml"), "x")
            .await
            .unwrap();
        tokio::fs::write(dir.path().join("config.local.toml"), "x")
            .await
            .unwrap();

        let error = missing_file_error("config.tom", &target).await;
        let message = error.to_string();
        assert!(message.contains("Did you mean"));
        assert!(message.contains("config.toml"));
    }

    #[test]
    fn suggestion_score_prefers_closer_names() {
        let exactish = suggestion_score("main.r", "main.rs");
        let distant = suggestion_score("main.r", "config.toml");
        assert!(exactish > distant);
    }
}
