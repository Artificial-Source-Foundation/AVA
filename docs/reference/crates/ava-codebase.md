# ava-codebase

Code indexing and search using tantivy (BM25) and petgraph (PageRank). Provides project-wide code search, dependency graph construction, and relevance scoring.

## How It Works

`CodebaseIndex` composes three subsystems:

```rust
pub struct CodebaseIndex {
    pub search: SearchIndex,
    pub graph: DependencyGraph,
    pub pagerank: HashMap<String, f64>,
}
```

### Indexing (`src/indexer.rs`)

`index_project(root)` async-walks the project directory, skipping `SKIP_DIRS` (e.g., `target`, `node_modules`, `.git`, `vendor`) and filtering by `SOURCE_EXTENSIONS` (`.rs`, `.ts`, `.js`, `.py`, `.go`, `.java`, etc.). For each source file it:

1. Reads content
2. Adds to the search index
3. Parses imports via `parse_imports()` (supports Rust `use`/`mod`, JS/TS `import`/`require`, Python `import`/`from`)
4. Adds dependency edges to the graph

### Search (`src/search.rs`)

`SearchIndex` uses tantivy with an in-RAM directory. Schema: `path` (STRING + STORED) and `content` (TEXT + STORED). Provides `add_document()` and `search()` returning `Vec<SearchHit>`.

### Dependency Graph (`src/graph.rs`)

`DependencyGraph` wraps petgraph `DiGraph`. Each file is a node; imports create directed edges. Supports `add_file()`, `add_dependency(from, to)`, `outgoing(file)`, `incoming(file)`.

### PageRank (`src/pagerank.rs`)

`calculate_pagerank(graph, damping)` runs iterative PageRank over the dependency graph. `calculate_relevance(query, search_index, pagerank)` combines BM25 search scores with PageRank values. `extract_keywords(text)` splits text into search terms.

### Repo Map (`src/repomap.rs`)

`generate_repomap(index)` produces a summary of the project structure. `select_relevant_files(query, index, limit)` combines search and PageRank to find the most relevant files for a query. `score_map(hits, pagerank)` blends search relevance with graph centrality.

### Types (`src/types.rs`)

```rust
pub struct SearchDocument { pub path: String, pub content: String }
pub struct SearchHit { pub path: String, pub score: f32, pub snippet: Option<String> }
pub struct SearchQuery { pub text: String, pub max_results: usize }
```

## Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/lib.rs` | -- | CodebaseIndex composite |
| `src/indexer.rs` | -- | Async project walker, import parsing |
| `src/search.rs` | -- | Tantivy BM25 search |
| `src/graph.rs` | -- | Petgraph dependency graph |
| `src/pagerank.rs` | -- | PageRank + relevance scoring |
| `src/repomap.rs` | -- | Repo map generation, file selection |
| `src/types.rs` | -- | SearchDocument, SearchHit, SearchQuery |
