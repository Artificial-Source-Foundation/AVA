//! AVA Codebase - BM25 search index, dependency graph, PageRank, repo map.

pub mod error;
pub mod graph;
pub mod impact;
pub mod indexer;
pub mod pagerank;
pub mod repomap;
pub mod search;
#[cfg(feature = "semantic")]
pub mod semantic;
pub mod symbol_graph;
pub mod symbols;
pub mod types;

use std::collections::HashMap;

pub use error::{CodebaseError, Result};
pub use graph::DependencyGraph;
pub use impact::{analyze_change_impact, ImpactSummary};
pub use indexer::{index_project, index_workspace};
pub use pagerank::{
    calculate_pagerank, calculate_relevance, calculate_symbol_pagerank, extract_keywords,
    find_seed_nodes, personalized_pagerank,
};
pub use repomap::{
    generate_repomap, generate_symbol_repomap, generate_symbol_repomap_entries, score_map,
    select_relevant_files, RankedFile, RepoFile, RepoMapEntry, SymbolSummary,
};
pub use search::SearchIndex;
#[cfg(feature = "semantic")]
pub use semantic::SemanticIndex;
pub use symbol_graph::SymbolGraph;
pub use symbols::{extract_symbols, Symbol, SymbolKind, SymbolRef};
pub use types::{SearchDocument, SearchHit, SearchQuery};

/// Composite index holding the search index, dependency graph, and PageRank scores.
pub struct CodebaseIndex {
    pub search: SearchIndex,
    pub graph: DependencyGraph,
    pub pagerank: HashMap<String, f64>,
    /// Symbol-level dependency graph (functions, structs, traits as nodes).
    pub symbol_graph: Option<SymbolGraph>,
    /// Symbol-level PageRank scores keyed by FQN (`file::symbol`).
    pub symbol_pagerank: HashMap<String, f64>,
    #[cfg(feature = "semantic")]
    pub semantic: Option<SemanticIndex>,
}

impl CodebaseIndex {
    pub fn impact_summary(&self, changed_files: &[String]) -> ImpactSummary {
        analyze_change_impact(&self.graph, changed_files, 3)
    }

    /// Generate a repo map ranked by hybrid BM25 + symbol PageRank.
    ///
    /// If a symbol graph exists, uses personalized PageRank for structural ranking.
    /// Falls back to file-level PageRank if no symbol graph is available.
    pub fn symbol_file_scores(&self, _query: &str) -> HashMap<String, f64> {
        if let Some(sg) = &self.symbol_graph {
            if !self.symbol_pagerank.is_empty() {
                return sg.aggregate_file_scores(&self.symbol_pagerank);
            }
        }
        // Fallback to file-level pagerank
        self.pagerank.clone()
    }

    pub fn hybrid_search(&self, query: &SearchQuery) -> Result<Vec<SearchHit>> {
        let lexical = self.search.search(query)?;

        #[cfg(feature = "semantic")]
        {
            let mut merged = lexical;
            if let Some(semantic) = &self.semantic {
                let semantic_hits = semantic.search(&query.query, query.max_results);
                for semantic_hit in semantic_hits {
                    if let Some(existing) =
                        merged.iter_mut().find(|hit| hit.path == semantic_hit.path)
                    {
                        existing.score += semantic_hit.score * 0.35;
                    } else if merged.len() < query.max_results {
                        merged.push(semantic_hit);
                    }
                }
                merged.sort_by(|a, b| {
                    b.score
                        .partial_cmp(&a.score)
                        .unwrap_or(std::cmp::Ordering::Equal)
                        .then_with(|| a.path.cmp(&b.path))
                });
                merged.truncate(query.max_results);
            }
            Ok(merged)
        }

        #[cfg(not(feature = "semantic"))]
        {
            Ok(lexical)
        }
    }
}
