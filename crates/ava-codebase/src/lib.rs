//! AVA Codebase - BM25 search index, dependency graph, PageRank, repo map.

pub mod error;
pub mod graph;
pub mod pagerank;
pub mod repomap;
pub mod search;
pub mod types;

pub use error::{CodebaseError, Result};
pub use graph::DependencyGraph;
pub use pagerank::{calculate_pagerank, calculate_relevance, extract_keywords};
pub use repomap::{generate_repomap, score_map, select_relevant_files, RankedFile, RepoFile};
pub use search::SearchIndex;
pub use types::{SearchDocument, SearchHit, SearchQuery};

pub fn healthcheck() -> bool {
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn healthcheck_returns_true() {
        assert!(healthcheck());
    }
}
