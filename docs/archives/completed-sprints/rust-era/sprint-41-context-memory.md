# Sprint 41: Context & Memory Mega-Sprint

> Combines Sprints 41 + 42 from the roadmap.

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in the "Key Files to Read" section
2. Read `CLAUDE.md` for project conventions
3. Read `docs/development/roadmap.md` for context
4. Enter plan mode and produce a detailed implementation plan
5. Get the plan confirmed before proceeding

## Goal

Make AVA smarter about what context to keep and what to recall. Instead of blindly truncating old messages, AVA should use codebase intelligence to pick the RIGHT files and leverage its memory system to recall past solutions.

## Key Files to Read

```
# Codebase indexing (already functional)
crates/ava-codebase/src/lib.rs                  # Public API
crates/ava-codebase/src/search.rs               # Tantivy-based search index
crates/ava-codebase/src/pagerank.rs             # PageRank scoring
crates/ava-codebase/src/repomap.rs              # File ranking + selection
crates/ava-codebase/src/graph.rs                # Dependency graph (petgraph)
crates/ava-codebase/src/types.rs                # Types
crates/ava-codebase/Cargo.toml                  # Dependencies

# Context management (already functional)
crates/ava-context/src/manager.rs               # ContextManager
crates/ava-context/src/strategies/mod.rs         # Strategy traits
crates/ava-context/src/strategies/sliding_window.rs
crates/ava-context/src/strategies/summarization.rs
crates/ava-context/src/strategies/tool_truncation.rs
crates/ava-context/src/token_tracker.rs          # Token counting
crates/ava-context/src/condenser.rs              # Hybrid condenser

# Memory (already functional)
crates/ava-memory/src/lib.rs                    # MemorySystem (SQLite + FTS5)

# Session
crates/ava-session/src/lib.rs                   # Session persistence

# Agent (integration point)
crates/ava-agent/src/stack.rs                   # AgentStack
crates/ava-agent/src/system_prompt.rs           # System prompt generation

# Tools
crates/ava-tools/src/registry.rs                # Tool trait
```

## What Already Exists

- **ava-codebase**: Tantivy search index, petgraph dependency graph, PageRank (0.85, 20 iter), `generate_repomap()`, `select_relevant_files()`
- **ava-context**: 3-stage hybrid condenser (tool truncation → summarization → sliding window), ContextManager with sync/async compaction
- **ava-memory**: SQLite + FTS5, remember/recall/search/get_recent
- **ava-session**: Session persistence with full conversation replay

## Theme 1: Intelligent Context Selection

### Story 1.1: Relevance-Aware Context

Instead of keeping the most recent messages and dropping old ones, use codebase intelligence to keep messages about relevant files.

**Approach:**
1. When the user sends a message, extract mentioned file paths and keywords
2. Use `ava-codebase`'s search index to find related files
3. Use PageRank scores to rank which files are most important
4. When compacting, prefer keeping messages that reference high-relevance files

**Implementation:**
- File: `crates/ava-context/src/strategies/relevance.rs` (NEW)
- Implement `CondensationStrategy` trait
- Takes a reference to the codebase index
- When condensing:
  1. Score each message by the relevance of files it mentions
  2. Keep high-relevance messages, drop low-relevance ones
  3. Always keep the system prompt and last N messages (recency bias)
- Scoring: `message_score = max(file_pagerank for file in mentioned_files) + recency_bonus`

**Integration:**
- Add as a stage in the HybridCondenser pipeline (between tool truncation and sliding window)
- Only active if a codebase index is available (optional)

**Acceptance criteria:**
- Relevance strategy implemented
- Messages about important files are preserved longer
- Falls back to sliding window if no index
- Add test: `test_relevance_keeps_important_files`
- Add test: `test_relevance_drops_unrelated_messages`

### Story 1.2: Codebase Index Auto-Build

Automatically build the codebase index when the agent starts, so relevance scoring works out of the box.

**Approach:**
- When `AgentStack::new()` is called, spawn a background task to index the project
- Index all source files (`.rs`, `.ts`, `.py`, `.go`, `.js`, etc.)
- Build dependency graph from imports/use statements
- Run PageRank
- Store index in memory (RAM-based Tantivy, no disk persistence needed)

**Implementation:**
- File: `crates/ava-codebase/src/indexer.rs` (NEW)
- `async fn index_project(root: &Path) -> Result<CodebaseIndex>`
- Walk the file tree, skip `.git/`, `node_modules/`, `target/`, etc.
- Parse each file for imports → build dependency graph
- Add all files to Tantivy search index
- Return `CodebaseIndex { search: SearchIndex, graph: DependencyGraph, pagerank: HashMap<String, f64> }`

**Integration:**
- `AgentStack` holds `Option<Arc<CodebaseIndex>>`
- Pass to ContextManager for relevance scoring
- Index building is async and non-blocking (agent can start before indexing completes)

**Acceptance criteria:**
- Project indexed on startup (background)
- Skips binary files and common ignore patterns
- Index available for relevance scoring
- Agent works fine if indexing fails or is slow
- Add test with sample project directory

### Story 1.3: Codebase Search Tool

Expose the codebase index as a tool the agent can use to find relevant files.

**Tool definition:**
```rust
pub struct CodebaseSearchTool {
    index: Option<Arc<CodebaseIndex>>,
}

// Parameters:
{
    "query": "authentication middleware",
    "limit": 10
}

// Returns:
{
    "results": [
        { "path": "src/auth/middleware.rs", "score": 0.85, "snippet": "pub fn authenticate..." },
        { "path": "src/auth/mod.rs", "score": 0.72, "snippet": "mod middleware..." }
    ]
}
```

**Implementation:**
- File: `crates/ava-tools/src/core/codebase_search.rs`
- Query the Tantivy search index
- Return ranked results with snippets
- If no index available, fall back to basic grep

**Acceptance criteria:**
- Returns ranked search results
- Includes file snippets
- Falls back gracefully without index
- Add tests

## Theme 2: Memory Enhancement

### Story 2.1: Memory Tools for Agent

Add tools so the agent can actively use the memory system during conversations.

**Tools to create:**

```rust
// Remember a fact for later
pub struct RememberTool { memory: Arc<MemorySystem> }
// Params: { "key": "project_structure", "value": "This project uses a monorepo..." }

// Recall a specific memory
pub struct RecallTool { memory: Arc<MemorySystem> }
// Params: { "key": "project_structure" }

// Search memories
pub struct MemorySearchTool { memory: Arc<MemorySystem> }
// Params: { "query": "authentication", "limit": 5 }
```

**Implementation:**
- File: `crates/ava-tools/src/core/memory.rs`
- Wire to the existing `MemorySystem` in ava-memory
- Register in `register_core_tools()` (needs `MemorySystem` passed in)

**Acceptance criteria:**
- Agent can remember, recall, and search memories
- Memories persist across sessions (SQLite)
- FTS5 search works for finding relevant memories
- Add tests

### Story 2.2: Session Search Tool

Let the agent search across past sessions to find relevant conversations.

**Tool definition:**
```rust
pub struct SessionSearchTool { session_store: Arc<SessionStore> }

// Parameters:
{
    "query": "how did we fix the authentication bug",
    "limit": 5
}

// Returns:
{
    "sessions": [
        {
            "id": "abc123",
            "date": "2026-03-05",
            "snippet": "...we fixed the auth bug by adding middleware...",
            "relevance": 0.82
        }
    ]
}
```

**Implementation:**
- File: `crates/ava-tools/src/core/session_search.rs`
- Use `ava-session`'s FTS5 index to search across all sessions
- Return session summaries with relevant snippets
- Limit results to avoid context bloat

**Acceptance criteria:**
- Searches across all past sessions
- Returns relevant snippets
- Structured results
- Add tests

### Story 2.3: Auto-Context from Memory

When the agent starts a new conversation, automatically load relevant memories as context.

**Approach:**
- When `AgentStack::run()` starts, check the user's first message
- Extract keywords from the message
- Search memories for relevant entries
- If found, inject as a system message: "Relevant memories from past sessions: ..."
- Keep it concise (max 5 memories, max 500 tokens total)

**Implementation:**
- In `crates/ava-agent/src/stack.rs` or `system_prompt.rs`
- Query `MemorySystem::search()` with keywords from user message
- Format as system context
- Only inject if relevant memories found (score > threshold)

**Acceptance criteria:**
- Relevant memories auto-loaded on conversation start
- Not injected if no relevant memories exist
- Token budget respected (max 500 tokens)
- Add test

## Theme 3: Session Replay

### Story 3.1: Session List and Load

The agent should be able to list and load past sessions.

**Tools:**
```rust
pub struct SessionListTool { session_store: Arc<SessionStore> }
// Params: { "limit": 10 }
// Returns: list of { id, date, summary, message_count }

pub struct SessionLoadTool { session_store: Arc<SessionStore> }
// Params: { "id": "abc123" }
// Returns: full conversation history
```

**Acceptance criteria:**
- Agent can list recent sessions
- Agent can load a specific session
- Session history returned as structured data
- Add tests

## Implementation Order

1. Story 2.1 (memory tools) — builds on existing MemorySystem, high value
2. Story 1.3 (codebase search tool) — exposes existing index as tool
3. Story 1.2 (auto-index) — enables codebase features
4. Story 1.1 (relevance-aware context) — uses index for smarter compaction
5. Story 2.2 (session search) — builds on session FTS5
6. Story 3.1 (session list/load) — pairs with session search
7. Story 2.3 (auto-context from memory) — polish, do last

## Constraints

- **Rust only**
- New tools go in `crates/ava-tools/src/core/` and register via `register_core_tools()`
- New strategies go in `crates/ava-context/src/strategies/`
- New indexer goes in `crates/ava-codebase/src/`
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Don't break existing context management or memory system
- Codebase indexing must be non-blocking (don't slow down startup)
- All new features are optional (work without index, without memories)

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
cargo test -p ava-codebase -- --nocapture
cargo test -p ava-context -- --nocapture
cargo test -p ava-memory -- --nocapture
cargo test -p ava-tools -- --nocapture
```
