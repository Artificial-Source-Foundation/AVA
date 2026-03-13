//! AVA Codebase - BM25 search index, dependency graph, PageRank, repo map.

pub mod error;
pub mod graph;
pub mod indexer;
pub mod pagerank;
pub mod repomap;
pub mod search;
pub mod types;

use std::collections::HashMap;

pub use error::{CodebaseError, Result};
pub use graph::DependencyGraph;
pub use pagerank::{calculate_pagerank, calculate_relevance, extract_keywords};
pub use repomap::{generate_repomap, score_map, select_relevant_files, RankedFile, RepoFile};
pub use search::SearchIndex;
pub use types::{SearchDocument, SearchHit, SearchQuery};

/// Composite index holding the search index, dependency graph, and PageRank scores.
pub struct CodebaseIndex {
    pub search: SearchIndex,
    pub graph: DependencyGraph,
    pub pagerank: HashMap<String, f64>,
}
