# Sprint 35: Performance & Polish

**Epic:** Complete Backend (Epic 4)  
**Duration:** 2 weeks  
**Goal:** Optimization, testing, documentation

## Stories

### Story 4.8: Performance Optimization
**Points:** 16 (AI: 8 hrs, Human: 8 hrs)

**What to build:**
- Profile with `cargo flamegraph`
- Optimize hot paths
- Zero-copy where possible

```rust
// Example optimizations:

// Before: Allocating String
pub fn read_file(&self, path: &Path) -> Result<String> {
    std::fs::read_to_string(path)
}

// After: Memory-mapped for large files
pub fn read_file(&self, path: &Path) -> Result<Cow<str>> {
    if self.is_large(path) {
        // mmap
        let mmap = unsafe { Mmap::map(&File::open(path)?)? };
        Ok(Cow::from(unsafe { str::from_utf8_unchecked(&mmap) }))
    } else {
        Ok(Cow::from(std::fs::read_to_string(path)?))
    }
}

// Zero-copy streaming
pub fn stream_tokens(&self) -> impl Stream<Item = &str> {
    // Don't allocate per token
}

// Parallel execution
pub async fn run_parallel(tasks: Vec<Task>) -> Vec<Result<Output>> {
    futures::future::join_all(tasks).await
}
```

**Acceptance Criteria:**
- [ ] 50% faster than baseline
- [ ] Memory usage reduced
- [ ] No allocations in hot path

---

### Story 4.9: Testing Suite
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
```rust
// tests/unit/
mod tool_tests;
mod agent_tests;
mod context_tests;

// tests/integration/
mod end_to_end;
mod mcp_tests;
mod lsp_tests;

// tests/property_based/
use proptest::prelude::*;

proptest! {
    #[test]
    fn edit_doesnt_crash(s in "\\PC*") {
        let result = edit_tool.apply(&s);
        prop_assert!(result.is_ok() || result.is_err());
    }
}

// tests/benchmarks/
use criterion::{black_box, criterion_group, criterion_main, Criterion};

fn benchmark_edit(c: &mut Criterion) {
    c.bench_function("edit 100 lines", |b| {
        b.iter(|| {
            edit_tool.apply(black_box(&large_file))
        })
    });
}
```

**Acceptance Criteria:**
- [ ] >80% test coverage
- [ ] Property-based tests pass
- [ ] Benchmarks established

---

### Story 4.10: Documentation
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
- Rust docs for all public APIs
- Architecture documentation
- Migration guide

```rust
//! AVA Core Library
//!
//! This crate provides the core functionality for AVA.
//!
//! # Example
//! ```
//! use ava_agent::AgentLoop;
//! 
//! let mut agent = AgentLoop::new();
//! let result = agent.run("Fix the bug").await?;
//! ```

/// The main agent loop that orchestrates LLM calls and tool execution
///
/// # Architecture
/// The agent loop follows this pattern:
/// 1. Generate LLM response
/// 2. Parse tool calls
/// 3. Execute tools in parallel
/// 4. Stream results back
/// 5. Repeat until completion
pub struct AgentLoop {
    // ...
}
```

**Acceptance Criteria:**
- [ ] `cargo doc` generates clean docs
- [ ] All public items documented
- [ ] Architecture guide written

---

## Epic 4 Complete!

**Success Criteria:**
- [ ] All 35 tools implemented
- [ ] Performance optimized
- [ ] Tests comprehensive
- [ ] Docs complete

**Backend 100% Rust!**

**Next:** Epic 5 - Frontend Integration (Sprint 36)
