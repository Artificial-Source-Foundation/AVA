# AVA Backend Sprint Backlog 2026

> Comprehensive sprint planning for Rust migration (Epics 1-4) and competitive tool improvements (Epics 5-10)

## Overview

**Two major initiatives:**
1. **Rust Migration** (Epics 1-4): 12-18 month journey to Rust-native architecture
2. **Competitive Tools** (Epics 5-10): 6-9 month journey to best-in-class tools

**Sprint Structure:**
- Sprint 24-31 (8 sprints): Competitive tools + Rust foundation
- Sprint 32-45 (14 sprints): Full Rust migration
- Each sprint = 2 weeks

---

## Epic 1: Rust Foundation (Sprints 24-26)

**Goal:** Establish Rust workspace and migrate low-risk infrastructure

**Success Criteria:**
- [ ] 5 Rust crates compiling
- [ ] PTY fully migrated
- [ ] Database layer migrated
- [ ] TypeScript bridge working

### Sprint 24: Workspace Setup

**Stories:**

1. **Setup Rust Workspace Structure** (3 pts)
   - Create `crates/` directory structure
   - Setup `Cargo.toml` workspace
   - Configure cross-compilation (Linux/macOS/Windows)
   - Setup CI/CD for Rust builds
   - **Acceptance:** `cargo build` succeeds for all targets

2. **Create ava-types Crate** (5 pts)
   - Define core types: Tool, ToolCall, ToolResult, Session, Message
   - Implement serialization (JSON, MessagePack)
   - Add validation traits
   - **Files:** `crates/ava-types/src/lib.rs`
   - **Competitor Reference:** Codex CLI types system

3. **Create ava-platform Traits** (5 pts)
   - Define platform abstraction traits
   - Port from TypeScript interfaces
   - **Files:** `crates/ava-platform/src/lib.rs`
   - **Reference:** `packages/core/src/platform.ts`

4. **Setup NAPI-RS Bridge** (3 pts)
   - Configure TypeScript → Rust bindings
   - Create bridge types
   - **Acceptance:** TypeScript can call Rust function

### Sprint 25: Infrastructure Migration

**Stories:**

5. **Migrate PTY Manager to Rust** (8 pts)
   - Port `src-tauri/src/pty.rs`
   - Add async PTY operations
   - Maintain backward compatibility
   - **Competitor Reference:** Codex CLI PTY implementation
   - **Acceptance:** All PTY tests pass

6. **Migrate Database Layer** (8 pts)
   - Port SQLite operations from Tauri plugin
   - Add connection pooling
   - Implement migrations in Rust
   - **Files:** `crates/ava-db/src/lib.rs`
   - **Acceptance:** Database tests pass

7. **Create ava-config Crate** (3 pts)
   - Configuration loading and validation
   - Environment variable support
   - **Acceptance:** Config loads from disk

### Sprint 26: Shell & File Operations

**Stories:**

8. **Create ava-shell Crate** (8 pts)
   - Sandboxed command execution
   - Tree-sitter bash parsing (security)
   - Timeout and cancellation support
   - **Competitor Reference:** Continue's terminal classifier (1241 lines)
   - **Files:** `crates/ava-shell/src/lib.rs`
   - **Acceptance:** Security classification works

9. **Create ava-fs Crate** (5 pts)
   - Async file operations
   - Watch support (notify crate)
   - **Acceptance:** File watcher works

10. **Integrate with Tauri** (3 pts)
    - Wire Rust crates into Tauri commands
    - **Acceptance:** Frontend can call new Rust code

**Epic 1 Completion:** Foundation crates ready, ~20% of backend in Rust

---

## Epic 2: Core Tools Migration (Sprints 27-29)

**Goal:** Migrate performance-critical tools to Rust

**Success Criteria:**
- [ ] Edit tool has streaming support
- [ ] Search tool has BM25
- [ ] LSP client migrated
- [ ] 50% performance improvement on hot paths

### Sprint 27: Edit Tool Overhaul

**Stories:**

11. **Multi-Strategy Edit Framework** (13 pts)
    - Implement 9 edit strategies (from OpenCode)
    - Strategy selector based on model capability
    - **Design:**
      ```rust
      pub trait EditStrategy {
          fn apply(&self, content: &str, edit: &Edit) -> Result<String, EditError>;
      }
      ```
    - **Competitor Reference:** OpenCode 9-strategy cascade
    - **Files:** `crates/ava-tools/src/edit/strategies/`
    - **Acceptance:** All strategies have tests

12. **Streaming Diff Application** (13 pts)
    - Port Zed's Edit Agent pattern
    - Fuzzy matcher with asymmetric costs
    - Real-time application as tokens stream
    - **Competitor Reference:** Zed streaming diff
    - **Files:** `crates/ava-tools/src/edit/streaming.rs`
    - **Acceptance:** 0.5s latency for edits

13. **Build Race Pattern** (8 pts)
    - Run multiple edit strategies concurrently
    - Pick winner based on success + quality
    - **Competitor Reference:** Plandex build race
    - **Acceptance:** 9 strategies run in parallel

### Sprint 28: Search & Context

**Stories:**

14. **BM25 Search Implementation** (8 pts)
    - Add tantivy or similar BM25 engine
    - Index file contents
    - **Competitor Reference:** Codex CLI BM25
    - **Files:** `crates/ava-tools/src/search/bm25.rs`
    - **Acceptance:** Search ranking improved

15. **PageRank Repo Map** (13 pts)
    - Build dependency graph
    - Implement PageRank algorithm
    - Rank files by relevance
    - **Competitor Reference:** Aider repo map
    - **Files:** `crates/ava-codebase/src/repomap.rs`
    - **Acceptance:** Relevant files in top-5

16. **Multi-Strategy Condenser** (13 pts)
    - Port OpenHands condensers to Rust
    - Recent, Amortized, ObservationMasking, etc.
    - **Competitor Reference:** OpenHands 9 condensers
    - **Files:** `crates/ava-context/src/condensers/`
    - **Acceptance:** 9 strategies working

### Sprint 29: LSP & Safety

**Stories:**

17. **Rust LSP Client** (13 pts)
    - Zero-copy LSP communication
    - Streaming diagnostics
    - **Files:** `crates/ava-lsp/src/client.rs`
    - **Acceptance:** Real-time error detection

18. **OS-Level Sandboxing** (13 pts)
    - Linux: Landlock + bubblewrap
    - macOS: Seatbelt
    - seccomp BPF filtering
    - **Competitor Reference:** Codex CLI sandbox
    - **Files:** `crates/ava-sandbox/src/`
    - **Acceptance:** 100ms sandbox startup

19. **Terminal Security Classifier** (8 pts)
    - Tree-sitter bash parsing
    - Security risk analysis
    - **Competitor Reference:** Continue's 1241-line classifier
    - **Files:** `crates/ava-shell/src/security.rs`

**Epic 2 Completion:** Core tools in Rust, ~40% of backend migrated

---

## Epic 3: Agent Core Migration (Sprints 30-35)

**Goal:** Migrate agent loop and business logic to Rust

**Success Criteria:**
- [ ] Agent loop fully in Rust
- [ ] Commander hierarchy in Rust
- [ ] Context management in Rust
- [ ] All tests passing

### Sprint 30: Agent Loop

**Stories:**

20. **Async Agent Loop** (13 pts)
    - Port agent loop from TypeScript
    - Tokio-based async execution
    - Tool dispatch pipeline
    - **Files:** `crates/ava-agent/src/loop.rs`
    - **Acceptance:** Basic agent loop works

21. **Tool Registry** (8 pts)
    - Dynamic tool registration
    - Middleware support
    - **Files:** `crates/ava-tools/src/registry.rs`

22. **Error Recovery Pipeline** (13 pts)
    - 4-tier error recovery (from Gemini CLI)
    - LLM self-correction
    - **Competitor Reference:** Gemini CLI edit recovery
    - **Acceptance:** 85% recovery rate

### Sprint 31: Commander & Context

**Stories:**

23. **Commander Migration** (13 pts)
    - Port 3-tier Praxis hierarchy
    - Worker spawning and lifecycle
    - Budget management
    - **Files:** `crates/ava-commander/src/`
    - **Acceptance:** Delegation works

24. **Context Manager** (13 pts)
    - Token tracking
    - Compaction triggers
    - **Files:** `crates/ava-context/src/manager.rs`

25. **Observation Masking** (8 pts)
    - Keep actions, mask observations
    - **Competitor Reference:** OpenHands
    - **Acceptance:** Context efficiency +40%

### Sprint 32: LLM & MCP

**Stories:**

26. **LLM Provider Abstraction** (13 pts)
    - Port all 13+ providers
    - Async streaming support
    - **Files:** `crates/ava-llm/src/`
    - **Acceptance:** All providers work

27. **MCP Client in Rust** (13 pts)
    - Server management
    - Tool discovery
    - OAuth support
    - **Files:** `crates/ava-mcp/src/client.rs`
    - **Acceptance:** MCP tools accessible

28. **MCP Server Mode** (8 pts)
    - Expose AVA tools via MCP
    - **Competitor Reference:** Zed's MCP server mode
    - **Acceptance:** Other agents can call AVA

### Sprint 33-35: Integration & Polish

**Stories:**

29. **Bridge TypeScript to Rust** (13 pts)
    - Wire all Rust crates to TypeScript
    - Maintain backward compatibility
    - **Acceptance:** All existing tests pass

30. **Performance Optimization** (13 pts)
    - Profile and optimize hot paths
    - Zero-copy where possible
    - **Acceptance:** 50% faster than before

31. **Documentation & Examples** (5 pts)
    - Rust crate documentation
    - Migration guide
    - **Acceptance:** Docs complete

**Epic 3 Completion:** Core business logic in Rust, ~70% migrated

---

## Epic 4: Full Rust Migration (Sprints 36-45)

**Goal:** Remove TypeScript core, pure Rust implementation

**Success Criteria:**
- [ ] No TypeScript in packages/core/src/
- [ ] Pure Rust CLI
- [ ] WASM extension system
- [ ] All tests passing

### Sprint 36-40: Remaining Migration

**Stories:**

32. **Migrate Remaining Tools** (20 pts)
    - All 35 tools in Rust
    - **Acceptance:** Tool parity

33. **Migrate Session Management** (13 pts)
    - DAG-based sessions
    - Forking, merging
    - **Files:** `crates/ava-session/src/`

34. **Migrate Git Integration** (13 pts)
    - All git operations
    - **Files:** `crates/ava-tools/src/git/`

35. **Migrate Memory System** (13 pts)
    - FTS5 search
    - Cross-session recall
    - **Files:** `crates/ava-memory/src/`

36. **Migrate Permissions** (8 pts)
    - Dynamic escalation
    - Four-tier rules
    - **Files:** `crates/ava-permissions/src/`

37. **Migrate Extensions** (13 pts)
    - ExtensionAPI in Rust
    - WASM plugin support
    - **Files:** `crates/ava-extensions/src/`

### Sprint 41-43: Pure Rust CLI

**Stories:**

38. **Create Rust CLI** (13 pts)
    - Standalone binary
    - No Tauri required
    - **Acceptance:** `cargo run` works

39. **Headless Mode** (8 pts)
    - Server mode
    - API endpoints
    - **Acceptance:** HTTP API works

40. **Configuration System** (5 pts)
    - Pure Rust config
    - Hot reload

### Sprint 44-45: Final Polish

**Stories:**

41. **Remove TypeScript Core** (13 pts)
    - Delete packages/core/src/
    - Migrate tests to Rust
    - **Acceptance:** No TS in core

42. **Final Testing** (13 pts)
    - End-to-end tests
    - Performance benchmarks
    - **Acceptance:** All green

43. **Documentation** (8 pts)
    - Architecture docs
    - API docs
    - Migration guide

44. **Release Preparation** (8 pts)
    - Version bumps
    - Changelog
    - Release notes

**Epic 4 Completion:** 100% Rust, ~18 months from start

---

## Epic 5: Edit Tool Excellence (Sprints 24-27)

**Goal:** Make edit tool best-in-class

### Sprint 24-25: Multi-Strategy Framework

**Stories:**

45. **Exact Match Strategy** (3 pts)
    - Line-by-line matching
    - **Acceptance:** 60% success rate

46. **Flexible Match Strategy** (3 pts)
    - Ignore whitespace
    - **Acceptance:** +10% success

47. **Block Anchor Strategy** (5 pts)
    - Context-aware matching
    - Levenshtein distance
    - **Acceptance:** +15% success

48. **Regex Match Strategy** (5 pts)
    - Pattern-based matching
    - **Acceptance:** +5% success

49. **Fuzzy Match Strategy** (8 pts)
    - Asymmetric costs (substitution=2, indel=1)
    - **Acceptance:** +10% success

50. **Strategy Benchmarking** (5 pts)
    - Test harness
    - Performance metrics
    - **Competitor Reference:** OpenCode benchmark harness

### Sprint 26-27: Streaming & Recovery

**Stories:**

51. **Streaming Parser** (8 pts)
    - Parse edits as tokens arrive
    - **Acceptance:** Real-time display

52. **Fuzzy Matcher** (13 pts)
    - Zed-style streaming matcher
    - **Acceptance:** 0.5s latency

53. **Error Recovery Pipeline** (8 pts)
    - Cascade: exact → flexible → regex → fuzzy
    - LLM self-correction
    - **Competitor Reference:** Gemini CLI 4-tier recovery
    - **Acceptance:** 85% recovery rate

54. **Per-Hunk Review UI** (8 pts)
    - Accept/reject individual changes
    - **Competitor Reference:** Zed per-hunk
    - **Acceptance:** UI working

**Epic 5 Completion:** Edit tool success rate: 70% → 90%

---

## Epic 6: Context Intelligence (Sprints 28-31)

**Goal:** Best-in-class context management

### Sprint 28-29: Ranking & Search

**Stories:**

55. **Dependency Graph Builder** (8 pts)
    - Parse imports across languages
    - Build graph structure
    - **Files:** `packages/core/src/codebase/graph.rs`

56. **PageRank Implementation** (8 pts)
    - Rank files by importance
    - Weight definitions (3.0) > declarations (2.0)
    - **Competitor Reference:** Aider repo map
    - **Acceptance:** Top-5 relevant files

57. **BM25 Search Index** (8 pts)
    - Index all files
    - Real-time updates
    - **Competitor Reference:** Codex CLI BM25
    - **Acceptance:** Fast search

58. **Hybrid Retrieval** (5 pts)
    - Combine: PageRank (25%) + BM25 (25%) + embeddings (50%)
    - **Competitor Reference:** Continue multi-signal
    - **Acceptance:** Better results

### Sprint 30-31: Condensers

**Stories:**

59. **Recent Condenser** (3 pts)
    - Keep last N messages
    - **Acceptance:** Works

60. **Amortized Forgetting** (5 pts)
    - Gradual context removal
    - **Competitor Reference:** OpenHands
    - **Acceptance:** Smooth degradation

61. **Observation Masking** (8 pts)
    - Keep actions, mask old observations
    - **Competitor Reference:** OpenHands
    - **Acceptance:** Intent preserved

62. **LLM Summarization** (5 pts)
    - Cheap model summarizes context
    - **Acceptance:** Token reduction

63. **Condenser Selector** (5 pts)
    - Auto-select best strategy
    - Based on token pressure
    - **Acceptance:** Intelligent selection

**Epic 6 Completion:** Context efficiency +40%, relevance +30%

---

## Epic 7: Safety & Sandboxing (Sprints 32-35)

**Goal:** Kernel-level safety without Docker

### Sprint 32-33: Linux Sandboxing

**Stories:**

64. **Landlock Integration** (8 pts)
    - Filesystem access control
    - Read-only by default
    - **Competitor Reference:** Codex CLI
    - **Acceptance:** Landlock working

65. **Bubblewrap Support** (5 pts)
    - Alternative to Landlock
    - Broader compatibility
    - **Acceptance:** Works on older kernels

66. **Seccomp BPF Filters** (8 pts)
    - Syscall filtering
    - Block dangerous calls
    - **Acceptance:** Dangerous calls blocked

67. **Network Proxy** (8 pts)
    - All traffic through proxy
    - Whitelist/blacklist
    - **Competitor Reference:** Codex CLI
    - **Acceptance:** Network controlled

### Sprint 34-35: macOS & General

**Stories:**

68. **Seatbelt Integration** (8 pts)
    - macOS sandboxing
    - Profile-based restrictions
    - **Competitor Reference:** Codex CLI
    - **Acceptance:** Seatbelt working

69. **Security Profiles** (5 pts)
    - Minimal, standard, unrestricted
    - User-selectable
    - **Acceptance:** Profiles work

70. **Terminal Command Classifier** (8 pts)
    - Tree-sitter bash parsing
    - Risk assessment
    - **Competitor Reference:** Continue (1241 lines)
    - **Acceptance:** Dangerous commands flagged

71. **Risk Self-Declaration** (5 pts)
    - Tools declare own risk
    - Dynamic permission escalation
    - **Competitor Reference:** OpenHands
    - **Acceptance:** Risk model working

**Epic 7 Completion:** Sandboxing without Docker, 100ms startup

---

## Epic 8: Planning & Orchestration (Sprints 36-39)

**Goal:** Explicit planning and parallel execution

### Sprint 36-37: Planning Phase

**Stories:**

72. **Architect Agent** (8 pts)
    - Separate planning model
    - Create execution plan
    - **Competitor Reference:** Plandex
    - **Acceptance:** Plans created

73. **Auto-Context Selection** (8 pts)
    - AI selects relevant files
    - Before coding phase
    - **Competitor Reference:** Plandex
    - **Acceptance:** Good context picked

74. **Plan Representation** (5 pts)
    - Structured plan format
    - Dependency graph
    - **Acceptance:** Machine-readable

### Sprint 38-39: Validation & Race

**Stories:**

75. **Validation Loop** (8 pts)
    - Post-edit validation
    - Tree-sitter syntax check
    - Compilation check (if applicable)
    - **Competitor Reference:** Aider
    - **Acceptance:** Invalid edits caught

76. **Auto-Retry with Escalation** (5 pts)
    - Failed edit → retry with error context
    - Escalate to stronger model
    - **Acceptance:** Better recovery

77. **Build Race Pattern** (8 pts)
    - Run multiple strategies concurrently
    - Pick best result
    - **Competitor Reference:** Plandex
    - **Acceptance:** 9-way parallel execution

**Epic 8 Completion:** Big-picture coherence, higher success rates

---

## Epic 9: Extensions & Ecosystem (Sprints 40-43)

**Goal:** Rich extension system

### Sprint 40-41: Extension Infrastructure

**Stories:**

78. **ExtensionAPI in Rust** (8 pts)
    - Register tools, hooks, validators
    - Middleware support
    - **Files:** `crates/ava-extensions/src/`

79. **WASM Plugin Support** (13 pts)
    - Sandboxed extensions
    - Type-safe boundaries
    - **Acceptance:** WASM plugins load

80. **Hot Reload** (5 pts)
    - Update extensions without restart
    - **Acceptance:** Dev mode works

### Sprint 42-43: Marketplace

**Stories:**

81. **Extension Registry** (8 pts)
    - Local registry format
    - Dependency resolution
    - **Acceptance:** Registry works

82. **Rating System** (5 pts)
    - Community ratings
    - Reviews
    - **Acceptance:** Ratings displayed

83. **Vibe-Code Extensions** (8 pts)
    - AI generates extensions
    - From description
    - **Acceptance:** AI can create plugins

**Epic 9 Completion:** Vibrant extension ecosystem

---

## Epic 10: Polish & Differentiation (Sprints 44-47)

**Goal:** Unique features nobody else has

### Sprint 44-45: Unique Features

**Stories:**

84. **Branch Visualization** (8 pts)
    - DAG graph UI
    - Visual branch comparison
    - **Acceptance:** Graph displays

85. **Team Templates** (5 pts)
    - Pre-configured agent teams
    - One-click setup
    - **Acceptance:** Templates work

86. **Cross-Team Coordination** (8 pts)
    - Agents can message each other
    - Standup meetings
    - **Acceptance:** Coordination works

87. **Ambient Terminal** (5 pts)
    - One-shot commands from shell
    - Quick answers
    - **Acceptance:** CLI integration

### Sprint 46-47: Native Integrations

**Stories:**

88. **System Tray** (3 pts)
    - Quick access menu
    - **Acceptance:** Tray icon works

89. **File Watchers** (5 pts)
    - Native OS events
    - Trigger on file changes
    - **Acceptance:** Watchers trigger

90. **Notifications** (3 pts)
    - Native OS notifications
    - **Acceptance:** Notifications work

91. **Global Hotkeys** (5 pts)
    - Keyboard shortcuts
    - **Acceptance:** Hotkeys work

**Epic 10 Completion:** AVA 2.0 feature complete

---

## Sprint Summary Table

| Sprint | Focus | Stories | Points | Epics |
|--------|-------|---------|--------|-------|
| 24 | Rust setup | 4 | 16 | 1 |
| 25 | Infrastructure | 3 | 19 | 1 |
| 26 | Shell/FS | 3 | 16 | 1 |
| 27 | Edit overhaul | 3 | 34 | 2, 5 |
| 28 | Search/Context | 3 | 34 | 2, 6 |
| 29 | LSP/Safety | 3 | 34 | 2, 7 |
| 30 | Agent loop | 3 | 34 | 3 |
| 31 | Commander | 3 | 34 | 3 |
| 32 | LLM/MCP | 3 | 34 | 3 |
| 33 | Integration | 2 | 26 | 3 |
| 34-35 | Polish | 2 | 18 | 3 |
| 36-40 | Full migration | 6 | 72 | 4 |
| 41-43 | Pure Rust CLI | 3 | 26 | 4 |
| 44-45 | Final polish | 3 | 29 | 4 |
| **Total** | | **44** | **446** | |

## Parallel Work: Competitive Tools (Sprints 24-47)

| Sprint | Focus | Stories | Points |
|--------|-------|---------|--------|
| 24-25 | Edit strategies | 6 | 24 | 5 |
| 26-27 | Streaming/Recovery | 4 | 37 | 5 |
| 28-29 | Ranking/Search | 4 | 29 | 6 |
| 30-31 | Condensers | 5 | 26 | 6 |
| 32-33 | Linux sandbox | 4 | 29 | 7 |
| 34-35 | macOS/Security | 4 | 26 | 7 |
| 36-37 | Planning | 3 | 21 | 8 |
| 38-39 | Validation/Race | 3 | 21 | 8 |
| 40-41 | Extensions | 3 | 26 | 9 |
| 42-43 | Marketplace | 3 | 21 | 9 |
| 44-45 | Unique features | 4 | 26 | 10 |
| 46-47 | Native integrations | 4 | 16 | 10 |
| **Total** | | **47** | **312** | |

---

## Dependencies & Critical Path

### Rust Migration Dependencies
```
Types → Platform → FS/Shell → Tools → Agent → Commander → Session
  ↓         ↓        ↓          ↓       ↓         ↓          ↓
Config → DB → PTY → LSP → MCP → LLM → Context → Memory
```

### Competitive Tools Dependencies
```
Edit strategies → Streaming → Recovery
      ↓              ↓           ↓
Build race → Per-hunk UI → Benchmarks

PageRank → BM25 → Hybrid retrieval
    ↓         ↓          ↓
Repo map → Search → Context selection

Condensers → Observation masking → Selector
```

### Cross-Cutting Dependencies
- **Rust core needed for:** Streaming, build race, true parallelism
- **TypeScript can do:** UI, configuration, high-level logic
- **Bridge needed before:** Full Rust migration complete

---

## Resource Allocation

**Team Composition:**
- 2 Rust developers (full-time on migration)
- 2 TypeScript developers (tools + UI)
- 1 DevOps (CI/CD, cross-compilation)
- 1 QA (testing both TS and Rust)

**Capacity:**
- ~40 points/sprint per developer
- ~160 points/sprint total
- Rust migration: 446 points = ~3 sprints parallel work
- Competitive tools: 312 points = ~2 sprints parallel work
- **Total: 6-8 sprints (12-16 weeks) for first major release**

---

## Success Metrics

### Rust Migration Metrics
| Metric | Baseline | Target | Sprint |
|--------|----------|--------|--------|
| Rust code % | 5% | 20% | 26 |
| Rust code % | 20% | 40% | 29 |
| Rust code % | 40% | 70% | 35 |
| Rust code % | 70% | 100% | 45 |
| Startup time | 3s | 1s | 26 |
| Startup time | 1s | 0.5s | 35 |
| Memory usage | 300MB | 200MB | 35 |
| Memory usage | 200MB | 50MB | 45 |

### Competitive Tools Metrics
| Metric | Baseline | Target | Epic |
|--------|----------|--------|------|
| Edit success rate | 70% | 80% | 5 |
| Edit success rate | 80% | 90% | 5 |
| Edit latency | 3s | 1s | 5 |
| Edit latency | 1s | 0.5s | 5 |
| Context relevance | 60% | 75% | 6 |
| Context relevance | 75% | 85% | 6 |
| Sandbox startup | 5s | 1s | 7 |
| Sandbox startup | 1s | 0.1s | 7 |
| Extension count | 0 | 10 | 9 |
| Extension count | 10 | 30 | 9 |

---

## Risk Mitigation

### Technical Risks
1. **Rust learning curve** → Pair programming, code reviews, Rust book study
2. **Cross-platform issues** → CI testing on all platforms from day 1
3. **Performance regressions** → Benchmarks at every sprint
4. **Integration bugs** → Comprehensive test suite, gradual rollout

### Schedule Risks
1. **Underestimation** → Buffer sprints (33-35, 44-45)
2. **Team availability** → Knowledge sharing, no single points of failure
3. **Scope creep** → Strict sprint boundaries, backlog grooming

### Business Risks
1. **User disruption** → Backward compatibility, gradual migration
2. **Competitor moves** → Stay focused on differentiators (Praxis, tools)
3. **Extension ecosystem** → Start simple, iterate based on usage

---

## Conclusion

This backlog provides:
- **91 stories** across **10 epics**
- **12-18 months** for full Rust migration
- **6-9 months** for competitive tool parity
- **Clear priorities** (P0 gaps first, then P1, then differentiation)
- **Measurable success criteria** for every sprint

**Next Steps:**
1. Review with team
2. Prioritize Sprint 24 stories
3. Assign owners
4. Start development!

---

*Based on analysis of 12 competitors and current AVA architecture*
*Last updated: 2026-03-03*
