use crate::types::SearchHit;
use std::collections::HashMap;

#[derive(Debug, Clone)]
pub struct SemanticIndex {
    docs: Vec<SemanticDoc>,
}

#[derive(Debug, Clone)]
struct SemanticDoc {
    path: String,
    tf: HashMap<String, f32>,
    snippet: String,
}

impl Default for SemanticIndex {
    fn default() -> Self {
        Self::new()
    }
}

impl SemanticIndex {
    pub fn new() -> Self {
        Self { docs: Vec::new() }
    }

    pub fn add_document(&mut self, path: impl Into<String>, content: &str) {
        let path = path.into();
        let tf = token_tf(content);
        let snippet = make_semantic_snippet(content);
        self.docs.push(SemanticDoc { path, tf, snippet });
    }

    pub fn search(&self, query: &str, limit: usize) -> Vec<SearchHit> {
        let q = token_tf(query);
        if q.is_empty() {
            return Vec::new();
        }

        let mut hits: Vec<SearchHit> = self
            .docs
            .iter()
            .filter_map(|doc| {
                let score = cosine_similarity(&q, &doc.tf);
                if score <= 0.0 {
                    return None;
                }
                Some(SearchHit {
                    path: doc.path.clone(),
                    score,
                    snippet: doc.snippet.clone(),
                })
            })
            .collect();

        hits.sort_by(|a, b| {
            b.score
                .partial_cmp(&a.score)
                .unwrap_or(std::cmp::Ordering::Equal)
                .then_with(|| a.path.cmp(&b.path))
        });
        hits.truncate(limit);
        hits
    }
}

fn token_tf(text: &str) -> HashMap<String, f32> {
    let mut counts: HashMap<String, f32> = HashMap::new();
    for token in text
        .split(|ch: char| !ch.is_alphanumeric() && ch != '_' && ch != '-')
        .map(|part| part.trim().to_lowercase())
        .filter(|token| token.len() > 2)
    {
        *counts.entry(token).or_insert(0.0) += 1.0;
    }

    let norm = counts.values().map(|v| v * v).sum::<f32>().sqrt();
    if norm > 0.0 {
        for value in counts.values_mut() {
            *value /= norm;
        }
    }
    counts
}

fn cosine_similarity(a: &HashMap<String, f32>, b: &HashMap<String, f32>) -> f32 {
    a.iter()
        .map(|(token, weight)| weight * b.get(token).copied().unwrap_or(0.0))
        .sum()
}

fn make_semantic_snippet(content: &str) -> String {
    const MAX: usize = 120;
    let out: String = content.chars().take(MAX).collect();
    if content.chars().count() > MAX {
        format!("{out}...")
    } else {
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn semantic_search_returns_ranked_hits() {
        let mut idx = SemanticIndex::new();
        idx.add_document("a.rs", "parse tokens and build ast");
        idx.add_document("b.rs", "render html and css styling");

        let hits = idx.search("parse tokens", 5);
        assert!(!hits.is_empty());
        assert_eq!(hits[0].path, "a.rs");
    }

    #[test]
    fn semantic_search_empty_query_returns_no_hits() {
        let mut idx = SemanticIndex::new();
        idx.add_document("a.rs", "some content");

        let hits = idx.search("  ", 5);
        assert!(hits.is_empty());
    }
}
