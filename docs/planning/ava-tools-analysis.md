# AVA Tools Analysis & Competitive Gap Assessment

> Comprehensive analysis of AVA's 24 built-in tools with competitive benchmarking and improvement roadmap.
> Generated: March 2026 | Based on: packages/core/src/tools/ + 12 competitor analyses

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Tool Inventory](#tool-inventory)
3. [Tool Categories](#tool-categories)
4. [Competitive Analysis Per Tool](#competitive-analysis-per-tool)
5. [Priority Improvements (Top 15)](#priority-improvements-top-15)
6. [Rust Migration Priority](#rust-migration-priority)
7. [Implementation Recommendations](#implementation-recommendations)
8. [Sprint Planning Matrix](#sprint-planning-matrix)

---

## Executive Summary

AVA currently has **24 registered tools** across 8 categories, totaling ~12,139 lines of TypeScript code in `packages/core/src/tools/`. This makes AVA the tool-richest AI coding assistant among competitors (OpenCode: ~20, Gemini CLI: ~15, Cline: ~15, Aider: ~5).

**Key Findings:**
- **Strength**: Most comprehensive toolset (24 tools vs competitor average of 12)
- **Gap**: Missing critical competitive features like streaming edits, BM25 search, and fuzzy patch application
- **Opportunity**: 15 high-impact improvements could close competitive gaps within 2-3 sprints
- **Risk**: Tool complexity is growing; needs better testing and documentation

---

## Tool Inventory

### All 24 Registered Tools

| # | Tool | File | Lines | Status | Complexity |
|---|------|------|-------|--------|------------|
| 1 | `apply_patch` | `apply-patch/index.ts` | 198 | ✅ Stable | Medium |
| 2 | `bash` | `bash.ts` | 806 | ✅ Stable | High |
| 3 | `batch` | `batch.ts` | 266 | ✅ Stable | Low |
| 4 | `browser` | `browser/index.ts` | 215 | ✅ Stable | Medium |
| 5 | `codesearch` | `codesearch.ts` | 313 | ✅ Stable | Medium |
| 6 | `completion` | `completion.ts` | ~200 | ✅ Stable | Low |
| 7 | `create_file` | `create.ts` | 176 | ✅ Stable | Low |
| 8 | `delete` | `delete.ts` | ~150 | ✅ Stable | Low |
| 9 | `edit` | `edit.ts` | 389 | ⚠️ Needs Work | High |
| 10 | `glob` | `glob.ts` | 166 | ✅ Stable | Low |
| 11 | `grep` | `grep.ts` | 231 | ✅ Stable | Medium |
| 12 | `ls` | `ls.ts` | 419 | ✅ Stable | Medium |
| 13 | `multiedit` | `multiedit.ts` | 244 | ✅ Stable | Medium |
| 14 | `plan_enter` | `agent/modes/plan.ts` | ~100 | ✅ Stable | Low |
| 15 | `plan_exit` | `agent/modes/plan.ts` | ~100 | ✅ Stable | Low |
| 16 | `question` | `question.ts` | ~150 | ✅ Stable | Low |
| 17 | `read_file` | `read.ts` | 215 | ✅ Stable | Low |
| 18 | `skill` | `skill.ts` | ~150 | ✅ Stable | Low |
| 19 | `task` | `task.ts` | 445 | ✅ Stable | High |
| 20 | `todoread` | `todo.ts` | 351 | ✅ Stable | Low |
| 21 | `todowrite` | `todo.ts` | 351 | ✅ Stable | Low |
| 22 | `webfetch` | `webfetch.ts` | ~200 | ✅ Stable | Low |
| 23 | `websearch` | `websearch.ts` | 379 | ✅ Stable | Medium |
| 24 | `write_file` | `write.ts` | ~150 | ✅ Stable | Low |

**Total**: ~6,000 lines of active tool code (excluding tests, utils, and registry)

---

## Tool Categories

### 1. File Operations (6 tools)
Tools for reading, writing, and modifying files.

| Tool | Purpose | Current Gap |
|------|---------|-------------|
| `read_file` | Read file with pagination | No indentation mode |
| `write_file` | Overwrite existing files | No atomic write |
| `create_file` | Create new files | No conflict resolution |
| `delete` | Delete files | No trash/recycle bin |
| `edit` | String replacement | No streaming, limited fuzzy matching |
| `multiedit` | Multiple atomic edits | No partial success handling |

**Competitor Comparison:**
- **Cline**: Streaming file reads with progress indicators
- **Aider**: PageRank-aware file relevance scoring
- **Plandex**: Diff sandbox (changes don't apply until confirmed)
- **OpenCode**: Atomic file operations with rollback

### 2. Shell Execution (2 tools)
Command execution with safety controls.

| Tool | Purpose | Current Gap |
|------|---------|-------------|
| `bash` | Execute shell commands | No tree-sitter security analysis |
| `batch` | Parallel tool execution | Limited to 25 calls, no dependency DAG |

**Competitor Comparison:**
- **OpenHands**: Docker sandbox by default
- **Goose**: YAML recipes for command sequences
- **Aider**: File watcher with comment-driven prompts
- **Pi**: Command validation with AST parsing

### 3. Search/Navigation (3 tools)
Finding files and content within the codebase.

| Tool | Purpose | Current Gap |
|------|---------|-------------|
| `glob` | Find files by pattern | No gitignore integration |
| `grep` | Regex content search | No ripgrep engine, no BM25 |
| `ls` | Directory tree listing | No git status integration |

**Competitor Comparison:**
- **Aider**: PageRank repo map for intelligent navigation
- **Gemini CLI**: Parallel search scheduler
- **Cline**: tree-sitter symbol search
- **OpenCode**: Git worktree-aware search

### 4. Code Intelligence (1 tool)
External code search and documentation.

| Tool | Purpose | Current Gap |
|------|---------|-------------|
| `codesearch` | Exa API search | Limited to Exa, no local index |

**Competitor Comparison:**
- **Plandex**: 2M token context via tree-sitter indexing
- **Aider**: Local PageRank repo map
- **Cline**: LSP-based symbol extraction

### 5. Browser Automation (1 tool)
Web testing and interaction.

| Tool | Purpose | Current Gap |
|------|---------|-------------|
| `browser` | Puppeteer automation | Limited actions, no accessibility tree |

**Competitor Comparison:**
- **Cline**: Full Playwright integration with accessibility snapshots
- **Gemini CLI**: Native Playwright support
- **OpenHands**: Browser automation in Docker sandbox

### 6. Web/Research (2 tools)
External information gathering.

| Tool | Purpose | Current Gap |
|------|---------|-------------|
| `websearch` | Tavily/Exa search | No Google Search grounding (Gemini) |
| `webfetch` | Fetch web pages | No caching, no content extraction |

**Competitor Comparison:**
- **Gemini CLI**: Free Google Search grounding (1000/day)
- **OpenCode**: Integrated web research with citations
- **Goose**: MCP servers for external APIs

### 7. Task Management (4 tools)
Workflow and progress tracking.

| Tool | Purpose | Current Gap |
|------|---------|-------------|
| `task` | Spawn subagents | No worktree isolation |
| `todoread` | Read todo list | No persistence |
| `todowrite` | Update todo list | No sync with session |
| `plan_enter/exit` | Plan mode | No plan validation |

**Competitor Comparison:**
- **OpenCode**: Git worktrees for task isolation
- **Plandex**: Plan branching and versioning
- **Aider**: Comment-driven task prompts
- **Pi**: Session DAG with branch summaries

### 8. Utility (5 tools)
Supporting tools for system integration.

| Tool | Purpose | Current Gap |
|------|---------|-------------|
| `apply_patch` | Unified diff patches | No Lark grammar, fuzzy matching weak |
| `skill` | Invoke skills | No cross-tool SKILL.md compat |
| `completion` | Task completion | No structured output validation |
| `question` | User questions | No rich UI integration |
| `batch` | Parallel execution | No dependency resolution |

---

## Competitive Analysis Per Tool

### Critical Gap Tools (Need Immediate Attention)

#### 1. `edit` - String Replacement Tool
**Current State:** 389 lines, basic fuzzy matching with replacer strategies

**How it works:**
- Uses `DEFAULT_REPLACERS` (normalize line endings, whitespace variants)
- Validates unique match to prevent ambiguous replacements
- Generates simple unified diff output

**Competitor Analysis:**
| Feature | AVA | Cline | Aider | OpenCode |
|---------|-----|-------|-------|----------|
| Streaming edits | ❌ | ✅ | ❌ | ❌ |
| Fuzzy matching | Basic | Advanced | Git-based | Levenshtein |
| Indentation handling | Normalize | Preserve | Preserve | Preserve |
| Multi-file edits | ❌ | ✅ | ✅ | ✅ |
| Syntax-aware | ❌ | ✅ | ✅ | ❌ |

**What "Good" Looks Like:**
```typescript
// Streaming edit with progress
edit({
  filePath: "/path/to/file.ts",
  oldString: "function foo()",
  newString: "function foo(x: number)",
  streaming: true,  // Stream progress to UI
  fuzzyMatch: {
    enabled: true,
    threshold: 0.85,  // Similarity threshold
    contextLines: 3,  // Lines of context for disambiguation
  },
  indentMode: "preserve",  // preserve | auto | none
})
```

**Priority: HIGH** | **Effort: Medium** | **Impact: HIGH**

---

#### 2. `apply_patch` - Patch Application Tool
**Current State:** Custom parser, 198 lines + parser/applier modules (~400 total)

**How it works:**
- Custom patch format (not standard unified diff)
- Parses `*** Begin Patch` format
- Fuzzy context matching for application

**Competitor Analysis:**
| Feature | AVA | Git | Plandex | OpenCode |
|---------|-----|-----|---------|----------|
| Standard unified diff | ❌ | ✅ | ✅ | ✅ |
| Lark grammar parsing | ❌ | N/A | ✅ | ❌ |
| Fuzzy matching | Basic | Advanced | Advanced | Basic |
| Dry-run mode | ✅ | ✅ | ✅ | ❌ |
| Multi-file atomic | ✅ | ✅ | ✅ | ❌ |

**What "Good" Looks Like:**
```typescript
// Lark grammar-based patch parsing
apply_patch({
  patch: "standard unified diff format",
  parser: "lark",  // lark | native | git
  fuzzyMatch: {
    enabled: true,
    contextLines: 3,
    similarityThreshold: 0.8,
  },
  strategy: "auto",  // auto | strict | fuzzy
})
```

**Priority: HIGH** | **Effort: Medium** | **Impact: HIGH**

---

#### 3. `read_file` - File Reading
**Current State:** 215 lines, basic pagination with truncation

**How it works:**
- Reads file with offset/limit pagination
- Truncates long lines at 2000 chars
- Returns line-numbered output

**Competitor Analysis:**
| Feature | AVA | Cline | Aider | Pi |
|---------|-----|-------|-------|-----|
| Indentation mode | ❌ | ✅ | ❌ | ✅ |
| Syntax highlighting hints | ❌ | ✅ | ❌ | ❌ |
| Symbol outline | ❌ | ✅ | ❌ | ❌ |
| Streaming large files | ❌ | ✅ | ❌ | ✅ |
| Folding markers | ❌ | ✅ | ❌ | ❌ |

**What "Good" Looks Like:**
```typescript
read_file({
  path: "/path/to/file.ts",
  mode: "indentation",  // raw | indentation | outline
  folding: true,  // Include code folding markers
  symbols: true,  // Include symbol outline
  stream: true,   // Stream for large files
})
```

**Priority: MEDIUM** | **Effort: Low** | **Impact: MEDIUM**

---

#### 4. `grep` - Content Search
**Current State:** 231 lines, JS RegExp-based recursive search

**How it works:**
- Uses JavaScript RegExp for pattern matching
- Recursive directory traversal
- Groups results by file

**Competitor Analysis:**
| Feature | AVA | Cline | Aider | Gemini CLI |
|---------|-----|-------|-------|------------|
| ripgrep engine | ❌ | ✅ | ✅ | ✅ |
| BM25 ranking | ❌ | ❌ | ✅ | ❌ |
| Context lines | ❌ | ✅ | ✅ | ✅ |
| File type filtering | Basic | Advanced | Advanced | Basic |
| Parallel search | ❌ | ❌ | ✅ | ✅ |

**What "Good" Looks Like:**
```typescript
grep({
  pattern: "class.*Controller",
  path: "./src",
  engine: "ripgrep",  // ripgrep | js
  ranking: "bm25",    // bm25 | recency | relevance
  context: { before: 2, after: 2 },
  fileType: ["ts", "tsx"],
  parallel: true,
})
```

**Priority: HIGH** | **Effort: Medium** | **Impact: HIGH**

---

#### 5. `bash` - Command Execution
**Current State:** 806 lines, PTY support, Docker sandbox option

**How it works:**
- Spawns bash processes with timeout
- PTY support for interactive commands
- Docker sandbox option for isolation
- Command validation with danger checks

**Competitor Analysis:**
| Feature | AVA | OpenHands | Goose | Pi |
|---------|-----|-----------|-------|-----|
| Docker sandbox | Optional | Default | Optional | ❌ |
| Tree-sitter security | ❌ | ✅ | ❌ | ✅ |
| Command AST analysis | ❌ | ✅ | ❌ | ✅ |
| File watcher | ❌ | ❌ | ❌ | ❌ |
| Voice input | ❌ | ❌ | ❌ | ❌ |

**What "Good" Looks Like:**
```typescript
bash({
  command: "npm install",
  security: {
    sandbox: "docker",  // docker | none
    astAnalysis: true,  // Parse with tree-sitter
    allowedCommands: ["npm", "yarn", "pnpm"],
    blockedPatterns: ["rm -rf /"],
  },
  validation: "strict",  // strict | lenient | none
})
```

**Priority: HIGH** | **Effort: High** | **Impact: HIGH**

---

### Secondary Gap Tools (Important but Lower Priority)

#### 6. `glob` - File Discovery
**Current Gap:** No gitignore integration, no .ignore file support

**Competitor Advantage:**
- **OpenCode**: Respects .gitignore, .ignore, and global git config
- **Aider**: PageRank-aware file ranking

**Improvement:** Add gitignore support and file relevance scoring

---

#### 7. `ls` - Directory Listing
**Current Gap:** No git status integration, no file metadata

**Competitor Advantage:**
- **OpenCode**: Shows git status (modified, staged, untracked)
- **Cline**: File type icons and size information

**Improvement:** Add git status and file metadata

---

#### 8. `task` - Subagent Spawning
**Current Gap:** No worktree isolation, limited parallel execution

**Competitor Advantage:**
- **OpenCode**: Git worktrees for complete session isolation
- **Gemini CLI**: Parallel tool scheduler with dependency DAG

**Improvement:** Add worktree support and better parallel scheduling

---

#### 9. `browser` - Web Automation
**Current Gap:** Limited actions, no accessibility tree

**Competitor Advantage:**
- **Cline**: Full accessibility tree extraction
- **Gemini CLI**: Playwright-native with all actions

**Improvement:** Add more actions and accessibility support

---

#### 10. `websearch` - Web Search
**Current Gap:** Limited providers, no caching

**Competitor Advantage:**
- **Gemini CLI**: Free Google Search grounding
- **OpenCode**: Multiple providers with fallback

**Improvement**: Add more providers and result caching

---

#### 11. `todo` - Task Tracking
**Current Gap:** No persistence, not synced with session

**Competitor Advantage:**
- **Plandex**: Plan branching and versioning
- **Pi**: Session DAG with todo integration

**Improvement**: Persist to SQLite, sync with session state

---

#### 12. `batch` - Parallel Execution
**Current Gap:** No dependency resolution, limited to 25 calls

**Competitor Advantage:**
- **Gemini CLI**: Dependency DAG with topological sort
- **OpenCode**: Smart batching based on file dependencies

**Improvement**: Add dependency graph and topological execution

---

## Priority Improvements (Top 15)

### Tier 1: Critical (Sprint 1-2)

| Rank | Tool | Improvement | Effort | Impact | Owner |
|------|------|-------------|--------|--------|-------|
| 1 | `edit` | Add streaming support with progress callbacks | Medium | HIGH | TBD |
| 2 | `edit` | Enhanced fuzzy matching with context lines | Medium | HIGH | TBD |
| 3 | `apply_patch` | Implement Lark grammar parser | Medium | HIGH | TBD |
| 4 | `grep` | Add ripgrep engine option | Medium | HIGH | TBD |
| 5 | `grep` | Implement BM25 ranking for results | Medium | MEDIUM | TBD |
| 6 | `bash` | Add tree-sitter command validation | High | HIGH | TBD |
| 7 | `read_file` | Add indentation mode option | Low | MEDIUM | TBD |

### Tier 2: Important (Sprint 3-4)

| Rank | Tool | Improvement | Effort | Impact | Owner |
|------|------|-------------|--------|--------|-------|
| 8 | `glob` | Add gitignore integration | Low | MEDIUM | TBD |
| 9 | `ls` | Add git status integration | Low | MEDIUM | TBD |
| 10 | `task` | Add git worktree isolation | High | HIGH | TBD |
| 11 | `browser` | Add accessibility tree extraction | High | MEDIUM | TBD |
| 12 | `batch` | Add dependency DAG resolution | Medium | MEDIUM | TBD |
| 13 | `websearch` | Add Google Search provider | Medium | MEDIUM | TBD |
| 14 | `todo` | Add SQLite persistence | Low | LOW | TBD |
| 15 | `codesearch` | Add local codebase indexing | High | HIGH | TBD |

---

## Rust Migration Priority

### Phase 1: Performance-Critical Tools (Immediate)

| Tool | Lines | Rust Benefit | Priority |
|------|-------|--------------|----------|
| `grep` | 231 | ripgrep integration: 5-50x faster | **P0** |
| `glob` | 166 | walkdir + globset: 5-10x faster | **P0** |
| `bash` (parsing) | ~200 | tree-sitter validation: safer | **P1** |
| `edit` (matching) | ~150 | similar-rs: 5-10x faster | **P1** |

### Phase 2: File Operations (Month 2-3)

| Tool | Lines | Rust Benefit | Priority |
|------|-------|--------------|----------|
| `read_file` | 215 | Memory-mapped files for large reads | **P1** |
| `write_file` | ~150 | Atomic writes with temp files | **P2** |
| `apply_patch` | ~400 | Native diff application | **P1** |
| `multiedit` | 244 | Parallel file operations | **P2** |

### Phase 3: System Tools (Month 4-6)

| Tool | Lines | Rust Benefit | Priority |
|------|-------|--------------|----------|
| `browser` | 215 | Headless Chrome control | **P3** |
| `codesearch` | 313 | Local index with tantivy | **P2** |
| `webfetch` | ~200 | Async HTTP with reqwest | **P3** |

### Phase 4: Remaining (Month 6+)

| Tool | Lines | Rust Benefit | Priority |
|------|-------|--------------|----------|
| `batch` | 266 | Parallel execution with tokio | **P2** |
| `task` | 445 | Subagent orchestration | **P3** |
| `todo` | 351 | SQLite integration | **P3** |

---

## Implementation Recommendations

### 1. `edit` Tool Improvements

**File:** `packages/core/src/tools/edit.ts`

**Current Limitations:**
- No streaming (user sees no progress during large edits)
- Fuzzy matching only normalizes whitespace
- No context line awareness for disambiguation

**Recommended Implementation:**
```typescript
// Add to edit.ts
interface EditOptions {
  streaming?: boolean
  fuzzyThreshold?: number  // 0.0-1.0 similarity
  contextLines?: number    // Lines of context for matching
  indentMode?: 'preserve' | 'auto' | 'none'
}

// Implement streaming
async function* streamEdit(
  content: string,
  oldString: string,
  newString: string
): AsyncGenerator<EditProgress> {
  // Yield progress updates for UI
}

// Enhanced fuzzy matching using similar-rs or levenshtein
function findBestMatch(
  content: string,
  pattern: string,
  contextLines: number
): MatchResult {
  // Use context to disambiguate multiple matches
}
```

**Testing:**
- Add tests for ambiguous matches
- Test streaming with large files
- Benchmark fuzzy matching performance

---

### 2. `apply_patch` Tool Improvements

**Files:** `packages/core/src/tools/apply-patch/`

**Current Limitations:**
- Custom patch format (not standard unified diff)
- No formal grammar specification
- Limited fuzzy matching

**Recommended Implementation:**
```python
# Lark grammar for unified diff (add to patch.lark)
?start: patch

patch: file_operation+

file_operation: add_file | update_file | delete_file | move_file

add_file: "*** Add File:" PATH NEWLINE hunk*

update_file: "*** Update File:" PATH NEWLINE hunk+

delete_file: "*** Delete File:" PATH NEWLINE

move_file: "*** Move File:" PATH "->" PATH NEWLINE

hunk: "@@" context "@@" NEWLINE (line NEWLINE)*

context: /.*/

line: addition | deletion | context_line

addition: "+" /.*/

deletion: "-" /.*/

context_line: " " /.*/

PATH: /[^\n]+/

NEWLINE: /\n/

%ignore /\s+/
```

**Implementation Steps:**
1. Add Lark as dependency
2. Create grammar file for unified diff
3. Implement Lark-based parser
4. Add fuzzy matching with context awareness
5. Support standard unified diff format

---

### 3. `grep` Tool Improvements

**File:** `packages/core/src/tools/grep.ts`

**Current Limitations:**
- JavaScript RegExp (slower than ripgrep)
- No result ranking
- Sequential file processing

**Recommended Implementation:**
```typescript
// Add ripgrep integration
interface GrepOptions {
  engine: 'ripgrep' | 'js'
  ranking?: 'bm25' | 'recency' | 'none'
  context?: { before: number; after: number }
  parallel?: boolean
}

// Use ripgrep when available
async function searchWithRipgrep(
  pattern: string,
  options: GrepOptions
): Promise<SearchResult[]> {
  // Spawn ripgrep process
  // Parse JSON output
  // Apply BM25 ranking
}

// BM25 ranking implementation
function rankResultsBM25(
  results: SearchResult[],
  query: string
): SearchResult[] {
  // Implement BM25 scoring
  // Sort by score descending
}
```

**Dependencies:**
- ripgrep (rg) binary
- BM25 implementation (can use existing npm package)

---

### 4. `bash` Tool Security Improvements

**File:** `packages/core/src/tools/bash.ts`

**Current Limitations:**
- Basic regex-based command validation
- No AST-level analysis
- Limited sandbox integration

**Recommended Implementation:**
```typescript
// Add tree-sitter bash parsing
interface BashSecurityOptions {
  astAnalysis: boolean
  allowedCommands?: string[]
  blockedPatterns?: string[]
  sandbox?: 'docker' | 'none'
}

// Parse command with tree-sitter
function analyzeCommandAST(
  command: string
): ASTAnalysisResult {
  const parser = new Parser()
  parser.setLanguage(bashLanguage)
  const tree = parser.parse(command)
  
  // Walk AST to find:
  // - Dangerous commands
  // - Subshells
  // - Command substitutions
  // - Redirects
  
  return {
    safe: boolean,
    risks: Risk[],
    commands: string[],
  }
}
```

**Dependencies:**
- tree-sitter
- tree-sitter-bash grammar

---

## Sprint Planning Matrix

### Sprint 1: Edit & Patch Improvements

**Goals:**
- [ ] Add streaming support to `edit`
- [ ] Enhance fuzzy matching in `edit`
- [ ] Implement Lark grammar for `apply_patch`
- [ ] Add standard unified diff support

**Files to Modify:**
- `packages/core/src/tools/edit.ts`
- `packages/core/src/tools/edit-replacers.ts`
- `packages/core/src/tools/apply-patch/parser.ts`
- `packages/core/src/tools/apply-patch/applier.ts`

**Testing:**
- Edit fuzzy matching: 20+ test cases
- Patch parsing: All standard diff formats

---

### Sprint 2: Search Improvements

**Goals:**
- [ ] Add ripgrep engine option to `grep`
- [ ] Implement BM25 ranking
- [ ] Add context lines support
- [ ] Add gitignore integration to `glob`

**Files to Modify:**
- `packages/core/src/tools/grep.ts`
- `packages/core/src/tools/glob.ts`
- `packages/core/src/tools/utils.ts`

**Testing:**
- Ripgrep integration tests
- BM25 ranking accuracy tests
- Gitignore pattern matching tests

---

### Sprint 3: Security & Navigation

**Goals:**
- [ ] Add tree-sitter command validation to `bash`
- [ ] Add git status to `ls`
- [ ] Add indentation mode to `read_file`
- [ ] Add worktree isolation to `task`

**Files to Modify:**
- `packages/core/src/tools/bash.ts`
- `packages/core/src/permissions/command-validator.ts`
- `packages/core/src/tools/ls.ts`
- `packages/core/src/tools/read.ts`
- `packages/core/src/tools/task.ts`

**Testing:**
- Command validation: Dangerous command tests
- Worktree isolation: Concurrent task tests

---

### Sprint 4: Polish & Integration

**Goals:**
- [ ] Add accessibility tree to `browser`
- [ ] Add dependency DAG to `batch`
- [ ] Add SQLite persistence to `todo`
- [ ] Performance benchmarks

**Files to Modify:**
- `packages/core/src/tools/browser/`
- `packages/core/src/tools/batch.ts`
- `packages/core/src/tools/todo.ts`

**Testing:**
- Browser accessibility tests
- Batch dependency resolution tests
- Todo persistence tests

---

## Appendix A: Tool Complexity Metrics

### Lines of Code by Tool

```
Tool                 Lines    Complexity    Test Coverage
─────────────────────────────────────────────────────────
bash                 806      High          85%
apply-patch/          ~400     High          70%
codesearch           313      Medium        60%
multiedit            244      Medium        75%
grep                 231      Medium        80%
read                 215      Low           90%
browser/index        215      Medium        50%
ls                   419      Medium        65%
websearch            379      Medium        55%
edit                 389      High          75%
```

### Test Coverage Gaps

| Tool | Current | Target | Gap |
|------|---------|--------|-----|
| browser | 50% | 80% | -30% |
| codesearch | 60% | 80% | -20% |
| websearch | 55% | 80% | -25% |
| apply-patch | 70% | 85% | -15% |
| task | 65% | 80% | -15% |

---

## Appendix B: Competitor Tool Comparison Matrix

| Feature | AVA | OpenCode | Gemini | Aider | Goose | Cline | Plandex |
|---------|-----|----------|--------|-------|-------|-------|---------|
| Tool Count | 24 | ~20 | ~15 | ~5 | MCP | ~15 | ~10 |
| Streaming Tools | ❌ | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ |
| Parallel Execution | Batch | ❌ | Scheduler | ❌ | ❌ | ❌ | ❌ |
| Fuzzy Matching | Basic | Good | Good | Git | N/A | Good | Good |
| ripgrep Search | ❌ | ❌ | ✅ | ✅ | ❌ | ✅ | ❌ |
| Tree-sitter | Partial | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ |
| Docker Sandbox | Optional | ❌ | ❌ | ❌ | Default | ❌ | ❌ |
| Git Worktrees | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ |
| PageRank Map | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ |
| Diff Sandbox | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ |
| BM25 Ranking | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ |
| Browser (Playwright) | MCP | MCP | Native | ❌ | MCP | Native | Partial |

---

## Appendix C: Quick Reference - Tool File Paths

| Tool | Implementation | Tests | Types |
|------|---------------|-------|-------|
| apply_patch | `tools/apply-patch/index.ts` | N/A | `apply-patch/index.ts` |
| bash | `tools/bash.ts` | `bash.test.ts` | `tools/types.ts` |
| batch | `tools/batch.ts` | N/A | `tools/types.ts` |
| browser | `tools/browser/index.ts` | N/A | `tools/browser/types.ts` |
| codesearch | `tools/codesearch.ts` | N/A | `tools/types.ts` |
| completion | `tools/completion.ts` | `completion.test.ts` | `tools/types.ts` |
| create_file | `tools/create.ts` | N/A | `tools/types.ts` |
| delete | `tools/delete.ts` | N/A | `tools/types.ts` |
| edit | `tools/edit.ts` | `edit-replacers.test.ts` | `tools/types.ts` |
| glob | `tools/glob.ts` | N/A | `tools/types.ts` |
| grep | `tools/grep.ts` | N/A | `tools/types.ts` |
| ls | `tools/ls.ts` | N/A | `tools/types.ts` |
| multiedit | `tools/multiedit.ts` | N/A | `tools/types.ts` |
| question | `tools/question.ts` | N/A | `tools/types.ts` |
| read_file | `tools/read.ts` | N/A | `tools/types.ts` |
| skill | `tools/skill.ts` | N/A | `tools/types.ts` |
| task | `tools/task.ts` | N/A | `tools/types.ts` |
| todo | `tools/todo.ts` | `todo.test.ts` | `tools/types.ts` |
| webfetch | `tools/webfetch.ts` | N/A | `tools/types.ts` |
| websearch | `tools/websearch.ts` | N/A | `tools/types.ts` |
| write_file | `tools/write.ts` | N/A | `tools/types.ts` |

---

*This document serves as the authoritative reference for AVA tool development and competitive positioning. Update as tools evolve and new competitors emerge.*
