use std::collections::HashMap;

use crate::graph::DependencyGraph;
use crate::pagerank::{calculate_pagerank, calculate_relevance, extract_keywords};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RepoFile {
    pub path: String,
    pub content: String,
    pub dependencies: Vec<String>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct RankedFile {
    pub path: String,
    pub score: f64,
}

pub fn generate_repomap(files: &[RepoFile], query: &str) -> Vec<RankedFile> {
    let mut graph = DependencyGraph::new();
    for file in files {
        graph.add_file(file.path.clone());
        for dep in &file.dependencies {
            graph.add_dependency(file.path.clone(), dep.clone());
        }
    }

    let pagerank = calculate_pagerank(&graph, 0.85, 20);
    let keywords = extract_keywords(query);

    let mut ranked = files
        .iter()
        .map(|file| RankedFile {
            path: file.path.clone(),
            score: calculate_relevance(&file.path, &file.content, &keywords, &pagerank),
        })
        .collect::<Vec<_>>();

    ranked.sort_by(|a, b| b.score.total_cmp(&a.score));
    ranked
}

pub fn select_relevant_files(ranked: &[RankedFile], limit: usize) -> Vec<RankedFile> {
    ranked.iter().take(limit).cloned().collect()
}

pub fn score_map(ranked: &[RankedFile]) -> HashMap<String, f64> {
    ranked
        .iter()
        .map(|r| (r.path.clone(), r.score))
        .collect::<HashMap<_, _>>()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_files() -> Vec<RepoFile> {
        vec![
            RepoFile {
                path: "src/parser.rs".to_string(),
                content: "pub fn parse() {}".to_string(),
                dependencies: vec!["src/token.rs".to_string()],
            },
            RepoFile {
                path: "src/token.rs".to_string(),
                content: "pub struct Token".to_string(),
                dependencies: vec![],
            },
            RepoFile {
                path: "src/main.rs".to_string(),
                content: "fn main() { parse(); }".to_string(),
                dependencies: vec!["src/parser.rs".to_string()],
            },
        ]
    }

    #[test]
    fn repomap_returns_ranked_files() {
        let ranked = generate_repomap(&sample_files(), "parse token");
        assert_eq!(ranked.len(), 3);
        assert!(ranked[0].score >= ranked[1].score);
    }

    #[test]
    fn select_relevant_files_limits_results() {
        let ranked = generate_repomap(&sample_files(), "parse");
        let top = select_relevant_files(&ranked, 2);
        assert_eq!(top.len(), 2);
    }

    #[test]
    fn score_map_contains_all_selected_files() {
        let ranked = generate_repomap(&sample_files(), "token");
        let map = score_map(&ranked);
        assert_eq!(map.len(), 3);
        assert!(map.contains_key("src/token.rs"));
    }
}
