# Sprint 28: Search & Context Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add BM25 full-text search (Tantivy), PageRank repo map, and multi-strategy context condenser to the Rust workspace — porting the TypeScript `packages/core/src/codebase/` and `packages/core/src/context/` modules to native Rust crates.

**Architecture:** Create two new crates: `ava-codebase` (Stories 2.4 + 2.5: Tantivy BM25 search index + PageRank dependency graph + repo map generation) and `ava-context` (Story 2.6: multi-strategy condenser with sliding window, tool truncation, and trait-based strategy chain). Both depend on `ava-types` for shared types. Wire through Tauri commands at the end.

**Tech Stack:** Rust, Tantivy 0.22, petgraph 0.6, serde, thiserror, tokio, tempfile (dev)

---

## Workspace Baseline

- **11 crates** in workspace, all compile, **99 tests** pass
- No `ava-codebase` or `ava-context` crate exists yet
- TypeScript reference: `packages/core/src/codebase/{ranking,graph,repomap,types,indexer}.ts` and `packages/core/src/context/{tracker,compactor,types,strategies/*}.ts`
- Existing Rust patterns to follow: `ava-memory` (rusqlite + tests), `ava-validator` (pipeline + trait), `ava-agent` (reflection + trait)

---

### Task 1: Scaffold `ava-codebase` and `ava-context` crates

**Files:**
- Modify: `Cargo.toml` (workspace members)
- Create: `crates/ava-codebase/Cargo.toml`
- Create: `crates/ava-codebase/src/lib.rs`
- Create: `crates/ava-context/Cargo.toml`
- Create: `crates/ava-context/src/lib.rs`

**Step 1: Verify crates don't exist yet**

Run: `cargo test -p ava-codebase --no-run 2>&1`
Expected: FAIL with "package(s) `ava-codebase` not found in workspace"

**Step 2: Create crate manifests**

`crates/ava-codebase/Cargo.toml`:
```toml
[package]
name = "ava-codebase"
version.workspace = true
edition.workspace = true
authors.workspace = true
license.workspace = true

[dependencies]
serde.workspace = true
serde_json.workspace = true
thiserror.workspace = true
tantivy = "0.22"
petgraph = "0.6"

[dev-dependencies]
tempfile = "3.0"
```

`crates/ava-context/Cargo.toml`:
```toml
[package]
name = "ava-context"
version.workspace = true
edition.workspace = true
authors.workspace = true
license.workspace = true

[dependencies]
serde.workspace = true
serde_json.workspace = true
thiserror.workspace = true

[dev-dependencies]
tokio = { workspace = true, features = ["rt", "macros"] }
```

`crates/ava-codebase/src/lib.rs`:
```rust
//! AVA Codebase — BM25 search index, dependency graph, PageRank, repo map.

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
```

`crates/ava-context/src/lib.rs`:
```rust
//! AVA Context — multi-strategy context condenser.

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
```

Add both to workspace `Cargo.toml` members array:
```toml
members = [
    # ... existing ...
    "crates/ava-codebase",
    "crates/ava-context",
]
```

**Step 3: Verify compilation**

Run: `cargo test -p ava-codebase -p ava-context`
Expected: 2 tests pass (one healthcheck each).

**Step 4: Commit**

```bash
git add Cargo.toml Cargo.lock crates/ava-codebase crates/ava-context
git commit -m "feat(rust): scaffold ava-codebase and ava-context crates for sprint 28"
```

---

### Task 2: Story 2.4 — BM25 search types and error module

**Files:**
- Create: `crates/ava-codebase/src/error.rs`
- Create: `crates/ava-codebase/src/types.rs`
- Modify: `crates/ava-codebase/src/lib.rs`

**Step 1: Write the types module**

`crates/ava-codebase/src/types.rs` — Port from TS `codebase/types.ts`:
```rust
//! Shared codebase types.

use serde::{Deserialize, Serialize};

/// Supported programming languages (mirrors TS Language type).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Language {
    Typescript, Javascript, Python, Rust, Go, Java,
    C, Cpp, Csharp, Ruby, Php, Swift, Kotlin, Scala,
    Markdown, Json, Yaml, Html, Css, Unknown,
}

/// A code symbol extracted from source.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CodeSymbol {
    pub name: String,
    pub kind: SymbolKind,
    pub line: u32,
    pub end_line: u32,
    pub exported: bool,
    pub signature: Option<String>,
    pub parent: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum SymbolKind {
    Function, Class, Interface, Type, Variable,
    Constant, Enum, Namespace, Method, Property,
}

/// One file in the search index.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FileEntry {
    pub path: String,
    pub relative_path: String,
    pub size: u64,
    pub mtime: u64,
    pub language: Language,
    pub tokens: u32,
    pub symbols: Vec<CodeSymbol>,
}

/// A single BM25 search hit.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SearchHit {
    pub relative_path: String,
    pub score: f32,
    pub line_matches: Vec<LineMatch>,
}

/// A matched line within a file.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LineMatch {
    pub line_number: u32,
    pub content: String,
}

/// Options for BM25 search.
#[derive(Debug, Clone)]
pub struct SearchOptions {
    pub max_results: usize,
    pub include_patterns: Vec<String>,
    pub exclude_patterns: Vec<String>,
}

impl Default for SearchOptions {
    fn default() -> Self {
        Self {
            max_results: 20,
            include_patterns: Vec::new(),
            exclude_patterns: Vec::new(),
        }
    }
}
```

`crates/ava-codebase/src/error.rs`:
```rust
//! Error types for ava-codebase.

use thiserror::Error;

pub type Result<T> = std::result::Result<T, CodebaseError>;

#[derive(Error, Debug)]
pub enum CodebaseError {
    #[error("Index error: {0}")]
    Index(String),
    #[error("Search error: {0}")]
    Search(String),
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Graph error: {0}")]
    Graph(String),
}
```

Update `lib.rs` to declare modules:
```rust
pub mod error;
pub mod types;

pub use error::{CodebaseError, Result};
```

**Step 2: Verify compilation**

Run: `cargo test -p ava-codebase`
Expected: PASS (1 healthcheck test).

**Step 3: Commit**

```bash
git add crates/ava-codebase/src/
git commit -m "feat(codebase): add shared types and error module"
```

---

### Task 3: Story 2.4 — Tantivy BM25 search index

**Files:**
- Create: `crates/ava-codebase/src/search.rs`
- Modify: `crates/ava-codebase/src/lib.rs`

**Step 1: Write failing tests**

Add tests at bottom of `search.rs` covering:
- `index_and_search_basic` — index 3 files, query "struct", expect hit in relevant file
- `search_empty_index` — search on empty index returns empty vec
- `search_no_match` — search for "xyznonexistent" returns nothing
- `index_updates_existing_file` — re-index same path replaces content
- `search_respects_max_results` — index 30 files, `max_results=5` returns ≤5

Run: `cargo test -p ava-codebase search -- --nocapture`
Expected: FAIL (module not found).

**Step 2: Implement `SearchIndex`**

```rust
//! BM25 full-text search over codebase files (Tantivy).

use tantivy::{
    collector::TopDocs,
    doc,
    query::QueryParser,
    schema::{Schema, STORED, TEXT, STRING},
    Index, IndexReader, IndexWriter, ReloadPolicy,
};
use std::path::Path;
use crate::error::{CodebaseError, Result};
use crate::types::{SearchHit, SearchOptions, LineMatch};

pub struct SearchIndex {
    index: Index,
    reader: IndexReader,
    writer_lock: std::sync::Mutex<IndexWriter>,
    schema: SearchSchema,
}

struct SearchSchema {
    schema: Schema,
    path_field: tantivy::schema::Field,
    content_field: tantivy::schema::Field,
}

impl SearchIndex {
    /// Create an in-memory index (for tests / ephemeral usage).
    pub fn in_memory() -> Result<Self> { /* ... */ }

    /// Create a persistent index at the given directory.
    pub fn open(dir: impl AsRef<Path>) -> Result<Self> { /* ... */ }

    /// Index a file's content. Replaces any existing entry for this path.
    pub fn add_file(&self, relative_path: &str, content: &str) -> Result<()> { /* ... */ }

    /// Remove a file from the index.
    pub fn remove_file(&self, relative_path: &str) -> Result<()> { /* ... */ }

    /// Commit pending writes.
    pub fn commit(&self) -> Result<()> { /* ... */ }

    /// Search for files matching the query string. Returns BM25-ranked hits.
    pub fn search(&self, query: &str, options: &SearchOptions) -> Result<Vec<SearchHit>> { /* ... */ }
}
```

Key implementation details:
- Schema: `path` (STRING + STORED), `content` (TEXT + STORED)
- `add_file` deletes existing by path term then adds doc
- `search` uses `QueryParser` on content field, `TopDocs` collector with `options.max_results`
- `LineMatch` populated by splitting stored content on `\n` and checking which lines contain query terms

**Step 3: Run tests**

Run: `cargo test -p ava-codebase search -- --nocapture`
Expected: 5 tests PASS.

**Step 4: Commit**

```bash
git add crates/ava-codebase/src/search.rs crates/ava-codebase/src/lib.rs
git commit -m "feat(codebase): add Tantivy BM25 search index (story 2.4)"
```

---

### Task 4: Story 2.5 — Dependency graph with petgraph

**Files:**
- Create: `crates/ava-codebase/src/graph.rs`
- Modify: `crates/ava-codebase/src/lib.rs`

**Step 1: Write failing tests**

Tests covering (in `graph.rs` `#[cfg(test)]`):
- `empty_graph` — new graph has 0 nodes, 0 edges
- `add_nodes_and_edges` — add 3 files with imports, verify edge count
- `find_roots_and_leaves` — files with no importers vs no imports
- `circular_dependency_detection` — A→B→C→A detected
- `transitive_dependencies` — A imports B, B imports C → A's transitive deps = {B, C}

Run: `cargo test -p ava-codebase graph -- --nocapture`
Expected: FAIL (module not found).

**Step 2: Implement `DependencyGraph`**

```rust
//! Dependency graph using petgraph.

use petgraph::graph::{DiGraph, NodeIndex};
use std::collections::HashMap;
use crate::error::Result;

pub struct DependencyGraph {
    graph: DiGraph<String, EdgeKind>,
    node_map: HashMap<String, NodeIndex>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EdgeKind {
    Regular,
    TypeOnly,
    Namespace,
}

impl DependencyGraph {
    pub fn new() -> Self { /* ... */ }

    /// Add a file node. Idempotent.
    pub fn add_file(&mut self, path: &str) -> NodeIndex { /* ... */ }

    /// Add a directed edge: `from` imports `to`.
    pub fn add_import(&mut self, from: &str, to: &str, kind: EdgeKind) { /* ... */ }

    /// Files with no incoming edges (entry points / dead code).
    pub fn roots(&self) -> Vec<String> { /* ... */ }

    /// Files with no outgoing edges (leaf files).
    pub fn leaves(&self) -> Vec<String> { /* ... */ }

    /// Detect all cycles. Returns vec of cycle paths.
    pub fn find_cycles(&self) -> Vec<Vec<String>> { /* ... (petgraph::algo::tarjan_scc) */ }

    /// All files transitively imported by `path`.
    pub fn transitive_deps(&self, path: &str) -> Vec<String> { /* ... (Bfs) */ }

    /// All files that transitively depend on `path`.
    pub fn transitive_dependents(&self, path: &str) -> Vec<String> { /* ... (reversed Bfs) */ }

    /// Number of nodes.
    pub fn node_count(&self) -> usize { /* ... */ }

    /// Number of edges.
    pub fn edge_count(&self) -> usize { /* ... */ }

    /// Outgoing neighbors of a node (what it imports).
    pub fn imports_of(&self, path: &str) -> Vec<String> { /* ... */ }

    /// Incoming neighbors of a node (what imports it).
    pub fn imported_by(&self, path: &str) -> Vec<String> { /* ... */ }
}
```

**Step 3: Run tests**

Run: `cargo test -p ava-codebase graph -- --nocapture`
Expected: 5 tests PASS.

**Step 4: Commit**

```bash
git add crates/ava-codebase/src/graph.rs crates/ava-codebase/src/lib.rs
git commit -m "feat(codebase): add petgraph dependency graph (story 2.5 foundation)"
```

---

### Task 5: Story 2.5 — PageRank algorithm

**Files:**
- Create: `crates/ava-codebase/src/ranking.rs`
- Modify: `crates/ava-codebase/src/lib.rs`

**Step 1: Write failing tests**

Tests:
- `pagerank_empty_graph` — returns empty HashMap
- `pagerank_single_node` — score = 1.0
- `pagerank_hub_gets_highest` — file imported by many others scores highest
- `pagerank_sum_approximately_one` — all scores sum ≈ 1.0
- `pagerank_convergence` — 2 iterations vs 20 iterations produces diff < tolerance
- `relevance_score_combines_signals` — composite of rank + keyword + recency

Run: `cargo test -p ava-codebase ranking -- --nocapture`
Expected: FAIL.

**Step 2: Implement `calculate_pagerank` + `RelevanceScorer`**

```rust
//! PageRank and composite file relevance scoring.

use std::collections::HashMap;
use crate::graph::DependencyGraph;

#[derive(Debug, Clone)]
pub struct PageRankOptions {
    pub damping: f64,        // default 0.85
    pub iterations: usize,   // default 20
    pub tolerance: f64,      // default 1e-6
}

impl Default for PageRankOptions {
    fn default() -> Self {
        Self { damping: 0.85, iterations: 20, tolerance: 1e-6 }
    }
}

/// Calculate PageRank scores for all nodes in the dependency graph.
/// Returns path → score map. Scores sum to ≈ 1.0.
pub fn calculate_pagerank(
    graph: &DependencyGraph,
    options: &PageRankOptions,
) -> HashMap<String, f64> { /* iterative power method, same as TS version */ }

/// Composite relevance scoring.
#[derive(Debug, Clone)]
pub struct ScoringWeights {
    pub pagerank: f64,   // default 0.3
    pub keyword: f64,    // default 0.5
    pub recency: f64,    // default 0.2
    pub max_age_ms: u64, // default 7 days
}

pub struct ScoredResult {
    pub score: f64,
    pub reasons: Vec<String>,
}

pub fn calculate_relevance(
    path: &str,
    symbols: &[String],
    mtime: u64,
    pagerank: f64,
    keywords: &[String],
    weights: &ScoringWeights,
) -> ScoredResult { /* same algorithm as TS calculateRelevanceScore */ }

/// Extract keywords from a task description.
/// Filters stop words, preserves tech terms, splits camelCase.
pub fn extract_keywords(task: &str) -> Vec<String> { /* port from TS */ }
```

**Step 3: Run tests**

Run: `cargo test -p ava-codebase ranking -- --nocapture`
Expected: 6 tests PASS.

**Step 4: Commit**

```bash
git add crates/ava-codebase/src/ranking.rs crates/ava-codebase/src/lib.rs
git commit -m "feat(codebase): add PageRank algorithm and relevance scoring (story 2.5)"
```

---

### Task 6: Story 2.5 — Repo map generation

**Files:**
- Create: `crates/ava-codebase/src/repomap.rs`
- Modify: `crates/ava-codebase/src/lib.rs`

**Step 1: Write failing tests**

Tests:
- `generate_repomap_basic` — 5 files, graph, options → RepoMap with summary string
- `repomap_respects_token_budget` — `max_tokens=100` produces shorter summary than `max_tokens=10000`
- `select_relevant_files` — task "fix login" selects file containing "login" symbol
- `repomap_groups_by_directory` — summary has `## src/` and `## tests/` headers

Run: `cargo test -p ava-codebase repomap -- --nocapture`
Expected: FAIL.

**Step 2: Implement**

```rust
//! Repo map generation — compact codebase summaries for LLM context.

use crate::graph::DependencyGraph;
use crate::ranking::{calculate_pagerank, calculate_relevance, extract_keywords, PageRankOptions, ScoringWeights};
use crate::types::{FileEntry, SearchHit};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone)]
pub struct RepoMapOptions {
    pub max_tokens: usize,         // default 8000
    pub include_symbols: bool,     // default true
    pub include_dependencies: bool, // default true
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RepoMap {
    pub summary: String,
    pub total_tokens: u32,
    pub file_count: usize,
    pub generated_at: u64,
    pub root_path: String,
}

pub fn generate_repomap(
    files: &[FileEntry],
    graph: &DependencyGraph,
    root_path: &str,
    options: &RepoMapOptions,
) -> RepoMap { /* port from TS generateRepoMap */ }

pub struct ScoredFile {
    pub file: FileEntry,
    pub score: f64,
    pub reason: String,
}

pub fn select_relevant_files(
    task: &str,
    files: &[FileEntry],
    graph: &DependencyGraph,
    max_tokens: usize,
) -> Vec<ScoredFile> { /* port from TS selectRelevantFiles */ }
```

**Step 3: Run tests**

Run: `cargo test -p ava-codebase repomap -- --nocapture`
Expected: 4 tests PASS.

**Step 4: Commit**

```bash
git add crates/ava-codebase/src/repomap.rs crates/ava-codebase/src/lib.rs
git commit -m "feat(codebase): add repo map generation with PageRank file selection (story 2.5)"
```

---

### Task 7: Story 2.6 — Context condenser types and error module

**Files:**
- Create: `crates/ava-context/src/error.rs`
- Create: `crates/ava-context/src/types.rs`
- Modify: `crates/ava-context/src/lib.rs`

**Step 1: Write types module**

`crates/ava-context/src/types.rs`:
```rust
//! Context condenser types.

use serde::{Deserialize, Serialize};

/// Message role.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Role {
    System,
    User,
    Assistant,
    Tool,
}

/// Message visibility.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Visibility {
    All,
    UserVisible,
    AgentVisible,
}

impl Default for Visibility {
    fn default() -> Self { Self::All }
}

/// A context message with metadata for condensation.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ContextMessage {
    pub id: String,
    pub session_id: String,
    pub role: Role,
    pub content: String,
    pub created_at: u64,
    pub token_count: Option<u32>,
    pub visibility: Visibility,
}

/// Result of a condensation operation.
#[derive(Debug, Clone)]
pub struct CondensationResult {
    pub messages: Vec<ContextMessage>,
    pub original_count: usize,
    pub condensed_count: usize,
    pub tokens_saved: u32,
    pub strategy_used: String,
}

/// Options for condensation.
#[derive(Debug, Clone)]
pub struct CondensationOptions {
    pub target_percent: u8,     // default 50
    pub preserve_system: bool,  // default true
    pub min_messages: usize,    // default 4
}

impl Default for CondensationOptions {
    fn default() -> Self {
        Self {
            target_percent: 50,
            preserve_system: true,
            min_messages: 4,
        }
    }
}
```

`crates/ava-context/src/error.rs`:
```rust
use thiserror::Error;

pub type Result<T> = std::result::Result<T, ContextError>;

#[derive(Error, Debug)]
pub enum ContextError {
    #[error("Strategy failed: {0}")]
    Strategy(String),
    #[error("Token limit exceeded: used {used}, limit {limit}")]
    TokenLimitExceeded { used: u32, limit: u32 },
    #[error("Invalid configuration: {0}")]
    Config(String),
}
```

Update `lib.rs`:
```rust
pub mod error;
pub mod types;

pub use error::{ContextError, Result};
```

**Step 2: Verify compilation**

Run: `cargo test -p ava-context`
Expected: PASS (1 healthcheck).

**Step 3: Commit**

```bash
git add crates/ava-context/src/
git commit -m "feat(context): add condenser types and error module (story 2.6)"
```

---

### Task 8: Story 2.6 — Token tracker

**Files:**
- Create: `crates/ava-context/src/tracker.rs`
- Modify: `crates/ava-context/src/lib.rs`

**Step 1: Write failing tests**

Tests:
- `tracker_new_state` — limit=1000, total=0, remaining=1000, percent=0.0
- `add_message_tracks_tokens` — add message, total > 0
- `remove_message_updates_total` — add then remove → total = 0
- `should_compact_at_threshold` — fill to 85% → `should_compact(80)` = true
- `would_fit_checks_buffer` — remaining=100, buffer=50, content=60 → false

Run: `cargo test -p ava-context tracker -- --nocapture`
Expected: FAIL.

**Step 2: Implement `ContextTracker`**

```rust
//! Token tracking for context window management.

use std::collections::HashMap;
use crate::types::ContextMessage;

pub struct TokenStats {
    pub total: u32,
    pub limit: u32,
    pub remaining: u32,
    pub percent_used: f64,
    pub message_count: usize,
}

pub struct ContextTracker {
    messages: HashMap<String, u32>,  // id → token count
    total: u32,
    limit: u32,
}

impl ContextTracker {
    pub fn new(limit: u32) -> Self { /* ... */ }
    pub fn add_message(&mut self, id: &str, token_count: u32) -> u32 { /* ... */ }
    pub fn remove_message(&mut self, id: &str) { /* ... */ }
    pub fn clear(&mut self) { /* ... */ }
    pub fn get_stats(&self) -> TokenStats { /* ... */ }
    pub fn should_compact(&self, threshold_percent: f64) -> bool { /* ... */ }
    pub fn would_fit(&self, tokens: u32, buffer: u32) -> bool { /* ... */ }
    pub fn set_limit(&mut self, limit: u32) { /* ... */ }
}

/// Estimate token count for text (chars / 4, matching TS convention).
pub fn estimate_tokens(text: &str) -> u32 {
    (text.len() as f64 / 4.0).ceil() as u32
}
```

**Step 3: Run tests**

Run: `cargo test -p ava-context tracker -- --nocapture`
Expected: 5 tests PASS.

**Step 4: Commit**

```bash
git add crates/ava-context/src/tracker.rs crates/ava-context/src/lib.rs
git commit -m "feat(context): add token tracker (story 2.6)"
```

---

### Task 9: Story 2.6 — Strategy trait + sliding window

**Files:**
- Create: `crates/ava-context/src/strategies/mod.rs`
- Create: `crates/ava-context/src/strategies/sliding_window.rs`
- Modify: `crates/ava-context/src/lib.rs`

**Step 1: Write failing tests in `sliding_window.rs`**

Tests:
- `empty_input_returns_empty` — compact([]) → []
- `under_budget_returns_all` — 5 messages within budget → all returned
- `over_budget_keeps_recent` — 20 messages, budget for 5 → last ~5 returned
- `preserves_system_message` — system msg always first in output
- `valid_turns_first_is_user` — with `ensure_valid_turns=true`, first non-system is user

Run: `cargo test -p ava-context sliding_window -- --nocapture`
Expected: FAIL.

**Step 2: Implement**

`crates/ava-context/src/strategies/mod.rs`:
```rust
//! Condensation strategy trait and built-in strategies.

pub mod sliding_window;
pub mod tool_truncation;

use crate::types::ContextMessage;
use crate::error::Result;

/// A strategy that reduces a message list to fit a token budget.
pub trait CondensationStrategy: Send + Sync {
    fn name(&self) -> &str;
    fn condense(&self, messages: &[ContextMessage], target_tokens: u32) -> Result<Vec<ContextMessage>>;
}
```

`crates/ava-context/src/strategies/sliding_window.rs`:
```rust
//! Sliding window: keep most recent messages within token budget.

use super::CondensationStrategy;
use crate::types::{ContextMessage, Role};
use crate::tracker::estimate_tokens;
use crate::error::Result;

pub struct SlidingWindow {
    pub min_messages: usize,
    pub ensure_valid_turns: bool,
}

impl Default for SlidingWindow { /* min_messages=2, ensure_valid_turns=true */ }

impl CondensationStrategy for SlidingWindow {
    fn name(&self) -> &str { "sliding-window" }

    fn condense(&self, messages: &[ContextMessage], target_tokens: u32) -> Result<Vec<ContextMessage>> {
        // 1. Separate system messages
        // 2. Calculate system overhead
        // 3. Iterate conversation from end, accumulate within budget
        // 4. Ensure min_messages
        // 5. If ensure_valid_turns, trim leading non-user messages
        // 6. Prepend system messages
    }
}
```

**Step 3: Run tests**

Run: `cargo test -p ava-context sliding_window -- --nocapture`
Expected: 5 tests PASS.

**Step 4: Commit**

```bash
git add crates/ava-context/src/strategies/ crates/ava-context/src/lib.rs
git commit -m "feat(context): add sliding window condensation strategy (story 2.6)"
```

---

### Task 10: Story 2.6 — Tool output truncation strategy

**Files:**
- Create: `crates/ava-context/src/strategies/tool_truncation.rs`
- Modify: `crates/ava-context/src/strategies/mod.rs`

**Step 1: Write failing tests**

Tests:
- `no_tool_responses_returns_unchanged` — all user messages → same output
- `short_tool_responses_unchanged` — assistant msg < budget → unchanged
- `long_tool_response_truncated` — 500-line assistant msg truncated to keep last 30 lines
- `recent_responses_preserved` — last 3 tool responses kept in full
- `truncation_marker_present` — truncated output contains "[... output truncated"

Run: `cargo test -p ava-context tool_truncation -- --nocapture`
Expected: FAIL.

**Step 2: Implement**

```rust
//! Tool output truncation: reduce large tool responses without LLM calls.

use super::CondensationStrategy;
use crate::types::{ContextMessage, Role};
use crate::tracker::estimate_tokens;
use crate::error::Result;

pub struct ToolTruncation {
    pub per_response_budget: u32,    // default 50_000
    pub truncate_keep_lines: usize,  // default 30
    pub preserve_recent_count: usize, // default 3
}

impl Default for ToolTruncation { /* defaults above */ }

impl CondensationStrategy for ToolTruncation { /* port from TS */ }

/// Truncate content to last N lines with marker.
pub fn truncate_content(content: &str, keep_lines: usize) -> String { /* port from TS */ }
```

**Step 3: Run tests**

Run: `cargo test -p ava-context tool_truncation -- --nocapture`
Expected: 5 tests PASS.

**Step 4: Commit**

```bash
git add crates/ava-context/src/strategies/tool_truncation.rs crates/ava-context/src/strategies/mod.rs
git commit -m "feat(context): add tool output truncation strategy (story 2.6)"
```

---

### Task 11: Story 2.6 — Multi-strategy condenser (orchestrator)

**Files:**
- Create: `crates/ava-context/src/condenser.rs`
- Modify: `crates/ava-context/src/lib.rs`

**Step 1: Write failing tests**

Tests:
- `no_condensation_needed` — messages within budget → strategy_used = "none"
- `tries_strategies_in_order` — first strategy fails → falls through to second
- `fallback_on_all_strategies_fail` — all fail → keeps last N messages
- `adds_and_removes_strategies` — `add_strategy`, `remove_strategy("sliding-window")`
- `needs_condensation_check` — tracker at 85% → `needs_condensation(80)` = true

Run: `cargo test -p ava-context condenser -- --nocapture`
Expected: FAIL.

**Step 2: Implement `Condenser`**

```rust
//! Multi-strategy context condenser.

use crate::error::Result;
use crate::strategies::CondensationStrategy;
use crate::tracker::ContextTracker;
use crate::types::{CondensationOptions, CondensationResult, ContextMessage, Role};

pub struct Condenser {
    strategies: Vec<Box<dyn CondensationStrategy>>,
    tracker: ContextTracker,
    default_target_percent: u8,
    fallback_min_messages: usize,
}

impl Condenser {
    pub fn new(tracker: ContextTracker) -> Self { /* default: empty strategies, 50%, 10 */ }

    pub fn with_strategies(mut self, strategies: Vec<Box<dyn CondensationStrategy>>) -> Self { /* ... */ }

    pub fn condense(
        &self,
        messages: &[ContextMessage],
        options: &CondensationOptions,
    ) -> CondensationResult {
        // 1. Check if condensation needed
        // 2. Try each strategy in order
        // 3. Validate result (non-empty, actually saved tokens)
        // 4. Fallback: keep system + last N
    }

    pub fn needs_condensation(&self, threshold: f64) -> bool { /* ... */ }
    pub fn add_strategy(&mut self, strategy: Box<dyn CondensationStrategy>) { /* ... */ }
    pub fn remove_strategy(&mut self, name: &str) -> bool { /* ... */ }
    pub fn strategy_names(&self) -> Vec<String> { /* ... */ }
}

/// Factory: condenser with sliding window only.
pub fn create_condenser(limit: u32) -> Condenser { /* ... */ }

/// Factory: condenser with tool truncation + sliding window.
pub fn create_full_condenser(limit: u32) -> Condenser { /* ... */ }
```

**Step 3: Run tests**

Run: `cargo test -p ava-context condenser -- --nocapture`
Expected: 5 tests PASS.

**Step 4: Commit**

```bash
git add crates/ava-context/src/condenser.rs crates/ava-context/src/lib.rs
git commit -m "feat(context): add multi-strategy condenser orchestrator (story 2.6)"
```

---

### Task 12: Wire public API exports

**Files:**
- Modify: `crates/ava-codebase/src/lib.rs`
- Modify: `crates/ava-context/src/lib.rs`

**Step 1: Update barrel exports**

`crates/ava-codebase/src/lib.rs`:
```rust
//! AVA Codebase — BM25 search, dependency graph, PageRank, repo map.

pub mod error;
pub mod graph;
pub mod ranking;
pub mod repomap;
pub mod search;
pub mod types;

pub use error::{CodebaseError, Result};
pub use graph::DependencyGraph;
pub use ranking::{calculate_pagerank, calculate_relevance, extract_keywords};
pub use repomap::{generate_repomap, select_relevant_files, RepoMap};
pub use search::SearchIndex;
pub use types::{CodeSymbol, FileEntry, SearchHit, SearchOptions};
```

`crates/ava-context/src/lib.rs`:
```rust
//! AVA Context — multi-strategy context condenser.

pub mod condenser;
pub mod error;
pub mod strategies;
pub mod tracker;
pub mod types;

pub use condenser::{create_condenser, create_full_condenser, Condenser};
pub use error::{ContextError, Result};
pub use strategies::CondensationStrategy;
pub use tracker::{estimate_tokens, ContextTracker, TokenStats};
pub use types::{
    CondensationOptions, CondensationResult, ContextMessage, Role, Visibility,
};
```

**Step 2: Verify full build**

Run: `cargo test --workspace`
Expected: All existing 99 tests + new ~40 tests PASS.

**Step 3: Commit**

```bash
git add crates/ava-codebase/src/lib.rs crates/ava-context/src/lib.rs
git commit -m "feat(rust): finalize public API exports for ava-codebase and ava-context"
```

---

### Task 13: Sprint 28 verification and quality gate

**Files:** (no new files)

**Step 1: Run full test suite**

Run: `cargo test --workspace`
Expected: ALL PASS (~139 tests).

**Step 2: Run clippy and format check**

Run: `cargo fmt --all -- --check && cargo clippy --workspace --all-targets -- -D warnings`
Expected: PASS (0 warnings, 0 errors).

**Step 3: Verify crate dependency sizes**

Run: `cargo tree -p ava-codebase --depth 1 && cargo tree -p ava-context --depth 1`
Expected: Only declared dependencies appear.

**Step 4: Commit any final fixes**

```bash
git add -A
git commit -m "chore(rust): sprint 28 lint/fmt cleanup"
```

---

## Dependency Summary

| Crate | New Dependencies | Why |
|-------|-----------------|-----|
| `ava-codebase` | `tantivy 0.22` | BM25 full-text search engine |
| `ava-codebase` | `petgraph 0.6` | Directed graph data structure |
| `ava-codebase` | `tempfile 3.0` (dev) | Temp dirs for index tests |
| `ava-context` | (none new) | Only `serde`, `thiserror` from workspace |
| `ava-context` | `tokio` (dev) | Async test runtime |

## File Layout (Final)

```
crates/ava-codebase/
├── Cargo.toml
└── src/
    ├── lib.rs          # barrel exports
    ├── error.rs        # CodebaseError
    ├── types.rs        # Language, FileEntry, CodeSymbol, SearchHit, SearchOptions
    ├── search.rs       # SearchIndex (Tantivy BM25)
    ├── graph.rs        # DependencyGraph (petgraph)
    ├── ranking.rs      # calculate_pagerank, calculate_relevance, extract_keywords
    └── repomap.rs      # generate_repomap, select_relevant_files, RepoMap

crates/ava-context/
├── Cargo.toml
└── src/
    ├── lib.rs          # barrel exports
    ├── error.rs        # ContextError
    ├── types.rs        # ContextMessage, Role, Visibility, CondensationResult, CondensationOptions
    ├── tracker.rs      # ContextTracker, estimate_tokens, TokenStats
    ├── condenser.rs    # Condenser orchestrator, create_condenser, create_full_condenser
    └── strategies/
        ├── mod.rs              # CondensationStrategy trait
        ├── sliding_window.rs   # SlidingWindow
        └── tool_truncation.rs  # ToolTruncation, truncate_content
```

## Test Matrix

| Module | Test Count | What's Covered |
|--------|-----------|----------------|
| search | 5 | index, query, update, empty, max_results |
| graph | 5 | edges, roots, leaves, cycles, transitive |
| ranking | 6 | pagerank empty/single/hub/sum, convergence, relevance |
| repomap | 4 | basic, budget, selection, grouping |
| tracker | 5 | new, add, remove, compact threshold, would_fit |
| sliding_window | 5 | empty, under budget, over budget, system, turns |
| tool_truncation | 5 | no-op, short, long, recent, marker |
| condenser | 5 | no-op, order, fallback, add/remove, check |
| **Total new** | **~40** | |

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Tantivy compile time | +30-60s first build | Only in `ava-codebase`, behind feature flag if needed |
| Tantivy binary size | +2-5MB | Acceptable for desktop app; use `tantivy` without optional features |
| petgraph API churn | Low | Pin to 0.6.x |
| Token estimation drift | Estimates differ from GPT tokenizer | Use same `chars/4` as TS for consistency; mark as approximate |
| Strategy trait sync vs async | CondensationStrategy is sync | Sufficient for sliding window + truncation; add async variant later if LLM summarization needed |
