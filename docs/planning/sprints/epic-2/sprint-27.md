# Sprint 27: Edit Tool Excellence

**Epic:** Essential Tools (Epic 2)  
**Duration:** 2 weeks  
**Goal:** Multi-strategy edit, streaming fuzzy matcher, error recovery

## Stories

### Story 2.1: 9 Edit Strategies
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
Multi-strategy edit framework in `crates/ava-tools/src/edit/strategies/`

```rust
pub trait EditStrategy {
    fn apply(&self, content: &str, edit: &Edit) -> Result<String>;
}

pub struct ExactMatch;
pub struct FlexibleMatch;  // ignore whitespace  
pub struct BlockAnchor;    // context-aware with Levenshtein
pub struct RegexMatch;
pub struct FuzzyMatch;     // Levenshtein distance
pub struct LineNumberMatch; // offset-based
pub struct WholeFileReplace;
pub struct SearchReplace;  // SEARCH/REPLACE blocks
pub struct UnifiedDiff;    // patch format

impl EditStrategy for ExactMatch {
    fn apply(&self, content: &str, edit: &Edit) -> Result<String> {
        // Exact line-by-line matching
    }
}
```

**Acceptance Criteria:**
- [ ] All 9 strategies implemented
- [ ] Each has unit tests
- [ ] Strategy selector works

---

### Story 2.2: Streaming Fuzzy Matcher
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
Streaming diff application in `crates/ava-tools/src/edit/streaming.rs`

```rust
pub struct StreamingMatcher {
    substitution_cost: usize, // 2
    indel_cost: usize,        // 1
}

impl StreamingMatcher {
    pub fn match_stream(&self, stream: TokenStream) -> impl Stream<Item = Match> {
        // Asymmetric costs: prefer insert/delete over substitution
        // This preserves more original code
    }
    
    pub async fn apply_streaming(&self, tokens: impl Stream<Item = Token>) {
        // Apply edits as tokens arrive
        // Real-time fuzzy matching
    }
}
```

**Competitor Reference:** Zed's Edit Agent pattern

**Acceptance Criteria:**
- [ ] 0.5s latency for edits
- [ ] Changes apply as tokens stream
- [ ] Fuzzy matching works

---

### Story 2.3: Error Recovery Pipeline
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
4-tier recovery in `crates/ava-tools/src/edit/recovery.rs`

```rust
pub struct RecoveryPipeline {
    strategies: Vec<Box<dyn EditStrategy>>,
}

impl RecoveryPipeline {
    pub async fn apply_with_recovery(&self, content: &str, edit: &Edit) -> Result<String> {
        // Try: exact → flexible → regex → fuzzy
        for strategy in &self.strategies {
            if let Ok(result) = strategy.apply(content, edit) {
                return Ok(result);
            }
        }
        
        // All failed: LLM self-correction
        self.llm_correct(content, edit).await
    }
    
    async fn llm_correct(&self, content: &str, failed_edit: &Edit) -> Result<String> {
        // Send error context to LLM
        // Get corrected edit
        // Apply corrected version
    }
}
```

**Competitor Reference:** Gemini CLI 4-tier recovery (85% success rate)

**Acceptance Criteria:**
- [ ] 85% recovery rate
- [ ] Auto-escalation works
- [ ] LLM correction works

---

## Sprint Goal

**Success Criteria:**
- [ ] Edit success rate: 70% → 90%
- [ ] Latency: 3s → 0.5s
- [ ] Recovery rate: 85%

**Next:** Sprint 28 - Search & Context
