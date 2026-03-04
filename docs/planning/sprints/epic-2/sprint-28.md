# Sprint 28: Search & Context

**Epic:** Essential Tools (Epic 2)  
**Duration:** 2 weeks  
**Goal:** BM25 search, PageRank repo map, condensers

## Stories

### Story 2.4: BM25 Search
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
BM25 search in `crates/ava-tools/src/search/bm25.rs`

```rust
pub struct BM25Index {
    index: tantivy::Index,
}

impl BM25Index {
    pub fn create(path: &Path) -> Result<Self> {
        // Create index directory
        // Set up schema: path, content, modified
    }
    
    pub fn add_document(&mut self, path: &Path, content: &str) {
        // Add to index
        // Real-time updates
    }
    
    pub fn search(&self, query: &str) -> Vec<SearchResult> {
        // BM25 ranking
        // Return top results with scores
    }
}
```

**Acceptance Criteria:**
- [x] BM25 ranking works
- [x] Real-time indexing
- [x] Better results than regex

---

### Story 2.5: PageRank Repo Map
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
Repo map in `crates/ava-codebase/src/repomap.rs`

```rust
pub struct RepoMap {
    graph: Graph<Node, Edge>,
}

#[derive(Debug, Clone)]
pub struct Node {
    pub path: PathBuf,
    pub node_type: NodeType, // Definition, Declaration, Reference
    pub weight: f64,
}

impl RepoMap {
    pub fn build(root: &Path) -> Result<Self> {
        // Parse all files
        // Build dependency graph
        // Calculate PageRank
    }
    
    pub fn rank_files(&self, query: &str) -> Vec<RankedFile> {
        // Weight definitions (3.0) > declarations (2.0) > identifiers (0.5)
        // Return top-N relevant files
    }
}
```

**Competitor Reference:** Aider repo map

**Acceptance Criteria:**
- [x] Dependency graph built
- [x] PageRank calculated
- [x] Top-5 files are relevant

---

### Story 2.6: Multi-Strategy Condenser
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
Condensers in `crates/ava-context/src/condensers/`

```rust
pub trait Condenser {
    fn condense(&self, context: &Context) -> Context;
}

pub struct RecentCondenser;        // Keep last N messages
pub struct AmortizedForgetting;    // Gradual removal
pub struct ObservationMasking;     // Keep actions, mask observations
pub struct LLMSummarization;       // Cheap model summarizes
pub struct StructuredSummary;      // Structured compression
pub struct HybridCondenser;        // Combine multiple
pub struct BrowserTurnCondenser;   // Browser-specific
pub struct IdentityCondenser;      // Minimal context
pub struct NoOpCondenser;          // Don't compress

pub struct CondenserSelector {
    condensers: Vec<Box<dyn Condenser>>,
}

impl CondenserSelector {
    pub fn select(&self, token_count: usize, limit: usize) -> &dyn Condenser {
        // Select based on pressure
    }
}
```

**Competitor Reference:** OpenHands 9 condensers

**Acceptance Criteria:**
- [x] Condenser framework implemented
- [x] Auto-selection works
- [x] Context efficiency improved

---

## Sprint Goal

**Success Criteria:**
- [x] BM25 search working
- [x] Repo map ranking files
- [x] Condenser strategies available

## Implementation Status (2026-03-04)

- Added `ava-codebase` crate with BM25 search (`search.rs`) and tests
- Added dependency graph + PageRank + repo map ranking pipeline in `ava-codebase`
- Added `ava-context` crate with token tracking, strategies, and condenser orchestration
- Verified with:
  - `cargo test -p ava-codebase -p ava-context`
  - `cargo clippy -p ava-codebase -p ava-context -- -D warnings`

**Next:** Sprint 29 - LSP & Sandboxing
