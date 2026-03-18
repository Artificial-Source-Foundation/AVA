# ava-codebase

> BM25 search index, dependency graph, PageRank scoring, and repository mapping.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `CodebaseIndex` | Composite index holding search, graph, and pagerank data |
| `CodebaseError` | Error enum: Tantivy, Io, InvalidQuery |
| `DependencyGraph` | Directed graph of file dependencies using petgraph |
| `SearchIndex` | Tantivy-based BM25 search index |
| `SearchDocument` | Document with path, content, optional language |
| `SearchHit` | Result with path, score, snippet |
| `SearchQuery` | Query string with max_results limit |
| `SemanticIndex` | TF-based semantic search (feature-gated) |
| `RepoFile` | File representation with path, content, dependencies |
| `RankedFile` | File with PageRank relevance score |
| `ImpactSummary` | Change impact analysis: changed, direct, transitive, test files |
| `index_project()` | Async index a single project directory |
| `index_workspace()` | Async index multiple roots with repo-qualified paths |
| `generate_repomap()` | Build ranked file list from repo files |
| `select_relevant_files()` | Take top N files from ranked list |
| `score_map()` | Convert RankedFile list to HashMap |
| `analyze_change_impact()` | BFS impact analysis from changed files |
| `calculate_pagerank()` | PageRank with damping factor and iterations |
| `calculate_relevance()` | Score based on PageRank + keyword matches |
| `extract_keywords()` | Tokenize query into lowercase keywords |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Exports all modules, defines `CodebaseIndex` with hybrid search |
| `error.rs` | `CodebaseError` enum with `thiserror` |
| `types.rs` | `SearchDocument`, `SearchHit`, `SearchQuery` structs |
| `graph.rs` | `DependencyGraph` with DiGraph, node/edge operations |
| `search.rs` | `SearchIndex` using Tantivy in-memory index |
| `semantic.rs` | `SemanticIndex` with cosine similarity TF scoring |
| `indexer.rs` | `index_project/workspace` with import parsing for Rust/JS/Python |
| `repomap.rs` | Repo map generation with PageRank ranking |
| `pagerank.rs` | PageRank algorithm and relevance scoring |
| `impact.rs` | Change impact analysis with BFS traversal |

## Dependencies

Uses: None (external only: tantivy, petgraph, tokio, regex, tracing, thiserror)

Used by:
- `ava-agent` - For codebase search and context
- `ava-tools` - For codebase indexing
- `ava-tui` - For search functionality

## Key Patterns

- **Error handling**: `thiserror`-based `CodebaseError` with `Result<T>` type alias
- **Feature gates**: `semantic` feature for semantic search
- **Async**: All indexing operations are async using tokio
- **Regex-based parsing**: Static regexes with `LazyLock` for import extraction
- **Hybrid search**: Combines BM25 lexical + cosine similarity semantic (when enabled)
- **Workspace support**: Repo-qualified paths (`repo_name:path`) for multi-repo indexing
- **PageRank integration**: Graph centrality scores boost search relevance
