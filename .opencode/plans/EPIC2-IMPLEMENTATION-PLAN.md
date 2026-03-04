# Epic 2: Essential Tools - Implementation Plan

**Goal:** Complete all 3 sprints (27-29) of the Tools phase with full code review after each sprint.

**Timeline:** 6 weeks total (2 weeks per sprint)

---

## Sprint 27: Edit Tool Excellence

### Story 2.1: 9 Edit Strategies (8 hrs AI + 8 hrs human)

**New crate:** `crates/ava-tools/src/edit/`

**What to build:**

```rust
// src/edit/mod.rs
pub trait EditStrategy: Send + Sync {
    /// Apply edit to content, returning modified content
    fn apply(&self, content: &str, old_string: &str, new_string: &str) -> Result<String>;
    
    /// Strategy name for error messages
    fn name(&self) -> &'static str;
}

// Strategies to implement:
pub mod exact_match;      // Exact string match
pub mod flexible_match;   // Ignore whitespace differences
pub mod block_anchor;     // Match by surrounding context
pub mod regex_match;      // Regex pattern matching
pub mod fuzzy_match;      // Fuzzy string matching
pub mod line_number;      // Line number based
pub mod range_replace;    // Character range replacement
pub mod multi_cursor;     // Multiple occurrences
pub mod semantic_merge;   // Merge without conflicts
```

**Files to create:**
- `crates/ava-tools/src/edit/mod.rs` (~150 lines)
- `crates/ava-tools/src/edit/exact_match.rs` (~80 lines)
- `crates/ava-tools/src/edit/flexible_match.rs` (~100 lines)
- `crates/ava-tools/src/edit/block_anchor.rs` (~120 lines)
- `crates/ava-tools/src/edit/regex_match.rs` (~90 lines)
- `crates/ava-tools/src/edit/fuzzy_match.rs` (~150 lines)
- `crates/ava-tools/src/edit/line_number.rs` (~70 lines)
- `crates/ava-tools/src/edit/range_replace.rs` (~60 lines)
- `crates/ava-tools/src/edit/multi_cursor.rs` (~110 lines)
- `crates/ava-tools/src/edit/semantic_merge.rs` (~130 lines)

**Acceptance Criteria:**
- [ ] All 9 strategies implement EditStrategy trait
- [ ] Tests for each strategy pass
- [ ] No clippy warnings
- [ ] All files under 300 lines

---

### Story 2.2: Streaming Fuzzy Matcher (8 hrs AI + 8 hrs human)

**Extend:** `crates/ava-tools/src/edit/fuzzy_match.rs`

**What to build:**

```rust
pub struct StreamingMatcher {
    substitution_cost: usize,  // Default: 2
    indel_cost: usize,         // Default: 1
    max_distance: usize,       // Default: 10
}

impl StreamingMatcher {
    pub fn new() -> Self;
    
    /// Match pattern against stream of tokens
    pub fn match_stream(
        &self,
        pattern: &str,
        tokens: impl Stream<Item = String>,
    ) -> impl Stream<Item = Match>;
    
    /// Calculate edit distance with asymmetric costs
    pub fn edit_distance(&self, pattern: &str, target: &str) -> usize;
    
    /// Find best match in content
    pub fn find_best_match(&self, pattern: &str, content: &str) -> Option<Match>;
}

pub struct Match {
    pub start: usize,
    pub end: usize,
    pub distance: usize,
    pub text: String,
}
```

**Acceptance Criteria:**
- [ ] Streaming match works on large files
- [ ] Latency under 0.5s for typical edits
- [ ] Asymmetric costs (sub=2, indel=1) implemented
- [ ] Tests pass

---

### Story 2.3: Error Recovery Pipeline (6 hrs AI + 6 hrs human)

**New file:** `crates/ava-tools/src/edit/recovery.rs`

**What to build:**

```rust
pub struct RecoveryPipeline {
    strategies: Vec<Box<dyn EditStrategy>>,
    max_attempts: usize,  // Default: 4
}

impl RecoveryPipeline {
    pub fn new() -> Self {
        Self {
            strategies: vec![
                Box::new(ExactMatch),
                Box::new(FlexibleMatch),
                Box::new(RegexMatch),
                Box::new(FuzzyMatch),
            ],
            max_attempts: 4,
        }
    }
    
    /// Try edit with recovery strategies
    pub async fn apply_with_recovery(
        &self,
        content: &str,
        old_string: &str,
        new_string: &str,
        llm_fallback: Option<&dyn LlmCorrector>,
    ) -> Result<String, RecoveryError>;
    
    /// Get next strategy to try
    pub fn next_strategy(&self, attempt: usize) -> Option<&dyn EditStrategy>;
}

pub struct RecoveryError {
    pub attempts: Vec<AttemptRecord>,
    pub final_error: String,
}

pub struct AttemptRecord {
    pub strategy: String,
    pub error: String,
    pub duration: Duration,
}
```

**Acceptance Criteria:**
- [ ] 4-tier recovery pipeline works
- [ ] LLM self-correction as final fallback
- [ ] 85% recovery rate in tests
- [ ] Detailed error reporting

---

## Sprint 28: Search & Context

### Story 2.4: BM25 Search with Tantivy (6 hrs AI + 6 hrs human)

**New crate:** `crates/ava-codebase/`

**What to build:**

```rust
// crates/ava-codebase/src/search/mod.rs
use tantivy::{Index, IndexWriter, Searcher};

pub struct CodeSearchIndex {
    index: Index,
    writer: IndexWriter,
}

impl CodeSearchIndex {
    pub async fn new(index_path: &Path) -> Result<Self>;
    
    /// Index a file
    pub async fn index_file(&mut self, path: &Path, content: &str) -> Result<()>;
    
    /// Index entire repository
    pub async fn index_repo(&mut self, root: &Path) -> Result<()>;
    
    /// Search using BM25
    pub async fn search(&self, query: &str, limit: usize) -> Result<Vec<SearchResult>>;
    
    /// Real-time index update
    pub async fn update_file(&mut self, path: &Path, content: &str) -> Result<()>;
}

pub struct SearchResult {
    pub path: PathBuf,
    pub score: f32,
    pub highlights: Vec<Highlight>,
}
```

**Dependencies to add:**
```toml
tantivy = "0.22"
```

**Acceptance Criteria:**
- [ ] BM25 search returns relevant results
- [ ] Real-time indexing works
- [ ] Tests pass

---

### Story 2.5: PageRank Repo Map (6 hrs AI + 6 hrs human)

**Extend:** `crates/ava-codebase/src/repo_map.rs`

**What to build:**

```rust
pub struct RepoMap {
    graph: DependencyGraph,
    pagerank_scores: HashMap<PathBuf, f64>,
}

impl RepoMap {
    pub async fn build(root: &Path) -> Result<Self>;
    
    /// Get top N most relevant files for a query
    pub fn relevant_files(&self, query: &str, n: usize) -> Vec<PathBuf>;
    
    /// Get dependency graph
    pub fn dependencies(&self, file: &Path) -> Vec<PathBuf>;
    
    /// Get dependents (reverse dependencies)
    pub fn dependents(&self, file: &Path) -> Vec<PathBuf>;
    
    /// Get PageRank score
    pub fn importance(&self, file: &Path) -> f64;
}

pub struct DependencyGraph {
    nodes: HashMap<PathBuf, Node>,
    edges: Vec<Edge>,
}

pub struct Node {
    pub path: PathBuf,
    pub imports: Vec<String>,
    pub exports: Vec<String>,
}

pub struct Edge {
    pub from: PathBuf,
    pub to: PathBuf,
    pub weight: f64,
}
```

**Acceptance Criteria:**
- [ ] PageRank algorithm implemented
- [ ] Dependency graph built from imports
- [ ] Top-5 relevant files returned
- [ ] Tests pass

---

### Story 2.6: Multi-Strategy Condenser (6 hrs AI + 6 hrs human)

**New crate:** `crates/ava-context/`

**What to build:**

```rust
pub trait Condenser: Send + Sync {
    fn condense(&self, context: &Context, target_tokens: usize) -> Result<Context>;
    fn name(&self) -> &'static str;
    fn compression_ratio(&self) -> f32;
}

pub struct CondenserPipeline {
    strategies: Vec<Box<dyn Condenser>>,
    selector: CondenserSelector,
}

impl CondenserPipeline {
    pub fn new() -> Self {
        Self {
            strategies: vec![
                Box::new(TruncationCondenser),
                Box::new(SummarizationCondenser),
                Box::new(SlidingWindowCondenser),
                Box::new(ImportanceCondenser),
                Box::new(DeduplicationCondenser),
                Box::new(HierarchicalCondenser),
                Box::new(SemanticCondenser),
                Box::new(TurnBasedCondenser),
                Box::new(TokenPressureCondenser),
            ],
            selector: CondenserSelector::new(),
        }
    }
    
    /// Auto-select best condenser based on token pressure
    pub async fn condense(&self, context: &Context) -> Result<Context>;
}

pub struct CondenserSelector {
    // ML-based selection logic
}
```

**Acceptance Criteria:**
- [ ] 9 condenser strategies implemented
- [ ] Auto-selection based on token pressure
- [ ] Tests pass

---

## Sprint 29: LSP & Sandboxing

### Story 2.7: LSP Client (6 hrs AI + 6 hrs human)

**New crate:** `crates/ava-lsp/`

**What to build:**

```rust
pub struct LspClient {
    process: Child,
    stdin: Box<dyn AsyncWrite>,
    stdout: Box<dyn AsyncRead>,
    pending_requests: HashMap<RequestId, oneshot::Sender<Response>>,
}

impl LspClient {
    pub async fn start(server_path: &Path) -> Result<Self>;
    
    /// Go to definition
    pub async fn goto_definition(&mut self, uri: &Uri, position: Position) -> Result<Vec<Location>>;
    
    /// Find references
    pub async fn find_references(&mut self, uri: &Uri, position: Position) -> Result<Vec<Location>>;
    
    /// Get diagnostics (streaming)
    pub async fn diagnostics(&mut self) -> impl Stream<Item = Diagnostic>;
    
    /// Hover information
    pub async fn hover(&mut self, uri: &Uri, position: Position) -> Result<Option<Hover>>;
}

pub struct Location {
    pub uri: Uri,
    pub range: Range,
}

pub struct Diagnostic {
    pub range: Range,
    pub severity: DiagnosticSeverity,
    pub message: String,
}
```

**Acceptance Criteria:**
- [ ] goto_definition works
- [ ] Diagnostics stream correctly
- [ ] Zero-copy parsing
- [ ] Tests pass

---

### Story 2.8: OS-Level Sandboxing (8 hrs AI + 8 hrs human)

**New crate:** `crates/ava-sandbox/`

**What to build:**

```rust
pub struct Sandbox {
    config: SandboxConfig,
    #[cfg(target_os = "linux")]
    landlock: LandlockRules,
    #[cfg(target_os = "macos")]
    seatbelt: SeatbeltProfile,
}

impl Sandbox {
    pub fn new(config: SandboxConfig) -> Result<Self>;
    
    /// Execute command in sandbox
    pub async fn execute(&self, command: &str) -> Result<CommandOutput>;
    
    /// Check if sandbox is available on this platform
    pub fn is_available() -> bool;
}

pub struct SandboxConfig {
    pub allowed_paths: Vec<PathBuf>,
    pub network_access: bool,
    pub max_memory: Option<usize>,
    pub timeout: Option<Duration>,
}

// Linux: Landlock
#[cfg(target_os = "linux")]
mod landlock {
    pub struct LandlockRules {
        rules: Vec<LandlockRule>,
    }
}

// macOS: Seatbelt
#[cfg(target_os = "macos")]
mod seatbelt {
    pub struct SeatbeltProfile {
        rules: Vec<SeatbeltRule>,
    }
}
```

**Dependencies:**
```toml
[target.'cfg(target_os = "linux")'.dependencies]
landlock = "0.4"

[target.'cfg(target_os = "macos")'.dependencies]
# Seatbelt via entitlements
```

**Acceptance Criteria:**
- [ ] Landlock works on Linux
- [ ] Seatbelt works on macOS
- [ ] 100ms startup time
- [ ] Tests pass

---

### Story 2.9: Terminal Security Classifier (4 hrs AI + 4 hrs human)

**Extend:** `crates/ava-tools/src/security/`

**What to build:**

```rust
pub struct SecurityClassifier {
    parser: BashParser,  // tree-sitter
}

impl SecurityClassifier {
    pub fn new() -> Result<Self>;
    
    /// Analyze command for security risks
    pub fn analyze(&self, command: &str) -> SecurityReport;
    
    /// Check if command is dangerous
    pub fn is_dangerous(&self, command: &str) -> bool;
    
    /// Get risk level (0.0 - 1.0)
    pub fn risk_level(&self, command: &str) -> f32;
}

pub struct SecurityReport {
    pub risk_level: f32,
    pub risk_factors: Vec<RiskFactor>,
    pub dangerous_constructs: Vec<String>,
    pub recommendations: Vec<String>,
}

pub struct RiskFactor {
    pub description: String,
    pub severity: Severity,
    pub location: Range,
}

pub enum Severity {
    Low,
    Medium,
    High,
    Critical,
}
```

**Dependencies:**
```toml
tree-sitter = "0.24"
tree-sitter-bash = "0.23"
```

**Acceptance Criteria:**
- [ ] tree-sitter bash parser integrated
- [ ] Dangerous commands flagged
- [ ] Risk assessment works
- [ ] Tests pass

---

## Epic 2 Completion Checklist

- [ ] Sprint 27: Edit Tool Excellence (9 strategies, fuzzy matcher, recovery)
- [ ] Sprint 27 Code Review
- [ ] Sprint 28: Search & Context (BM25, PageRank, condensers)
- [ ] Sprint 28 Code Review
- [ ] Sprint 29: LSP & Sandboxing (LSP client, sandboxing, security)
- [ ] Sprint 29 Code Review
- [ ] Final Epic 2 Integration Test

---

## Dependencies Summary

**New dependencies:**
```toml
# Workspace
tantivy = "0.22"           # BM25 search
tree-sitter = "0.24"       # Code parsing
tree-sitter-bash = "0.23"  # Bash parsing

[target.'cfg(target_os = "linux")'.dependencies]
landlock = "0.4"           # Linux sandboxing
```

---

## New Crates to Create

| Crate | Purpose | Sprint |
|-------|---------|--------|
| `ava-codebase` | Repo map, BM25 search | 28 |
| `ava-context` | Context condensers | 28 |
| `ava-lsp` | LSP client | 29 |
| `ava-sandbox` | OS-level sandboxing | 29 |

---

## Implementation Order

1. **Start Sprint 27** → Implement Stories 2.1, 2.2, 2.3
2. **Sprint 27 Code Review** → Fix any issues
3. **Start Sprint 28** → Implement Stories 2.4, 2.5, 2.6
4. **Sprint 28 Code Review** → Fix any issues
5. **Start Sprint 29** → Implement Stories 2.7, 2.8, 2.9
6. **Sprint 29 Code Review** → Fix any issues
7. **Epic 2 Complete** → Ready for Epic 3