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
- [ ] BM25 ranking works
- [ ] Real-time indexing
- [ ] Better results than regex

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
- [ ] Dependency graph built
- [ ] PageRank calculated
- [ ] Top-5 files are relevant

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
- [ ] 9 condensers implemented
- [ ] Auto-selection works
- [ ] Context efficiency +40%

---

## Sprint Goal

**Success Criteria:**
- [ ] BM25 search working
- [ ] Repo map ranking files
- [ ] 9 condensers available

**Next:** Sprint 29 - LSP & Sandboxing
