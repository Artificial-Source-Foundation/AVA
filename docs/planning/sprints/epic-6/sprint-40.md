# Sprint 40: Performance

**Epic:** Ship It (Epic 6)  
**Duration:** 2 weeks  
**Goal:** Optimize critical paths

## Stories

### Story 6.3: Performance Optimization
**Points:** 20 (Team: Full sprint)

**What to optimize:**

**1. Startup Time**
Target: < 100ms

```rust
// Lazy loading
pub struct ToolRegistry {
    tools: HashMap<String, Box<dyn Tool>>,
    loaded: HashSet<String>,
}

impl ToolRegistry {
    pub fn get(&mut self, name: &str) -> Option<&dyn Tool> {
        if !self.loaded.contains(name) {
            self.load_tool(name)?;
        }
        self.tools.get(name).map(|t| t.as_ref())
    }
}
```

**2. Edit Latency**
Target: < 500ms

```rust
// Cache compiled regex
lazy_static! {
    static ref EDIT_PATTERNS: Vec<Regex> = vec![
        Regex::new(r"...").unwrap(),
        // ...
    ];
}

// Parallel fuzzy matching
pub async fn find_best_match(edit: &Edit) -> Option<Match> {
    let matchers: Vec<_> = strategies.iter()
        .map(|s| s.find(edit))
        .collect();
        
    let results = futures::future::join_all(matchers).await;
    results.into_iter().flatten().max_by_key(|m| m.score)
}
```

**3. Memory Usage**
Target: < 50MB idle

```rust
// Use Arc for shared data
pub struct Context {
    messages: Arc<Vec<Message>>, // Shared, not cloned
}

// Drop unused sessions
pub async fn cleanup_old_sessions(&self) {
    let old = self.sessions.iter()
        .filter(|s| s.last_accessed < now - Duration::hours(24))
        .map(|s| s.id)
        .collect::<Vec<_>>();
        
    for id in old {
        self.sessions.remove(&id);
    }
}
```

**4. Tool Execution**
Target: < 50ms per tool

```rust
// Connection pooling for LLM
pub struct LLMClient {
    pool: Pool<reqwest::Client>,
}

// Reuse processes for shell
pub struct ShellPool {
    processes: Vec<Child>,
}
```

**Benchmarking:**
```rust
// Add benchmarks
#[bench]
fn bench_edit_100_lines(b: &mut Bencher) {
    let file = generate_file(100);
    let edit = create_edit(&file);
    
    b.iter(|| {
        edit_tool.apply(black_box(&edit))
    });
}
```

**Acceptance Criteria:**
- [ ] Startup < 100ms
- [ ] Edit latency < 500ms
- [ ] Memory < 50MB idle
- [ ] Tool execution < 50ms

---

### Story 6.4: Load Testing
**Points:** 10 (Team: Half sprint)

**What to test:**
- 100 concurrent sessions
- 1000 tool calls per minute
- Large files (10MB+)
- Long sessions (1000+ messages)

**Acceptance Criteria:**
- [ ] Handles load
- [ ] No crashes
- [ ] Performance acceptable

---

## Sprint Goal

**Success Criteria:**
- [ ] Meets all performance targets
- [ ] Benchmarks established
- [ ] Load tested

**Next:** Sprint 41 - Release
