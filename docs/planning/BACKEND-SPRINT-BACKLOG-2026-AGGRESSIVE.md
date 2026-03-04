# AVA Backend Sprint Backlog 2026 - AI-Accelerated

> Direct-to-Rust sprint planning. No temporary TypeScript. Final architecture from day 1.

## Philosophy

**With AI writing code:**
- No gradual migration needed
- No TypeScript bridges  
- No "prototype in TS, port to Rust"
- **Write it in Rust correctly the first time**

**Timeline:** 6-9 months (not 18)
**Approach:** AI pairs write Rust directly
**Result:** Pure Rust backend + TypeScript frontend only

---

## Architecture (Final State)

```
┌─────────────────────────────────────┐
│  Frontend (TypeScript/SolidJS)      │
│  - UI components                    │
│  - User interactions                │
│  - Calls Rust via Tauri commands    │
└──────────────┬──────────────────────┘
               │ Tauri IPC
┌──────────────▼──────────────────────┐
│  Backend (100% Rust)                │
│                                     │
│  ┌──────────────┐ ┌──────────────┐ │
│  │ Agent Loop   │ │ Tool Registry│ │
│  │ (tokio)      │ │ (35 tools)   │ │
│  └──────────────┘ └──────────────┘ │
│  ┌──────────────┐ ┌──────────────┐ │
│  │ LLM Client   │ │ MCP Client   │ │
│  │ (13+ prov)   │ │ (servers)    │ │
│  └──────────────┘ └──────────────┘ │
│  ┌──────────────┐ ┌──────────────┐ │
│  │ Sandboxing   │ │ Context Mgmt │ │
│  │ (Landlock)   │ │ (9 condens)  │ │
│  └──────────────┘ └──────────────┘ │
└─────────────────────────────────────┘
```

**Frontend:** TypeScript (UI only)  
**Backend:** Rust (everything else)

---

## Timeline Overview

| Phase | Duration | Sprints | Focus |
|-------|----------|---------|-------|
| **Foundation** | 6 weeks | 24-26 | Core crates, types, platform |
| **Essential Tools** | 6 weeks | 27-29 | Edit, search, LSP, sandbox |
| **Agent Core** | 6 weeks | 30-32 | Loop, commander, LLM, MCP |
| **Complete Backend** | 6 weeks | 33-35 | Remaining tools, polish |
| **Integration** | 6 weeks | 36-38 | Frontend wiring, testing |
| **Ship It** | 6 weeks | 39-41 | Bug fixes, perf, docs |

**Total: 36 weeks (9 months)** with AI assistance

### Execution Update (2026-03-04)

- Epic 1 (Sprints 24-26): COMPLETE
- Epic 2 (Sprints 27-29): COMPLETE
- New crates delivered: `ava-db`, `ava-codebase`, `ava-context`, `ava-lsp`, `ava-sandbox`
- Major modules delivered: edit strategies/recovery, BM25 search, dependency graph/PageRank repo map, context condenser, LSP transport/client foundations, sandbox policy planners, AST terminal security classifier
- Verification gates passed:
  - `cargo build --all-targets`
  - `cargo test --workspace`
  - `cargo clippy --workspace -- -D warnings`

---

## Epic 1: Foundation (Sprints 24-26)

**Goal:** Core Rust infrastructure - everything else builds on this

### Sprint 24: Workspace & Types

**Story 1.1: Workspace Setup** (AI: 2 hrs, Human: 2 hrs)
```bash
crates/
├── ava-types/          # Core types
├── ava-platform/       # Platform traits
├── ava-config/         # Configuration
├── ava-logger/         # Logging
└── Cargo.toml          # Workspace root
```
- Multi-target compilation (Linux/macOS/Windows)
- CI/CD for Rust builds
- **Acceptance:** `cargo build --all-targets` passes

**Story 1.2: Core Types** (AI: 4 hrs, Human: 4 hrs)
```rust
// ava-types/src/lib.rs
pub struct Tool {
    pub name: String,
    pub description: String,
    pub parameters: Parameters,
}

pub enum ToolResult {
    Success { output: String },
    Error { error: String },
}

pub struct Session {
    pub id: Uuid,
    pub messages: Vec<Message>,
    pub context: Context,
}
```
- Serialization (serde)
- Validation
- **Acceptance:** All types compile, tests pass

**Story 1.3: Platform Abstraction** (AI: 4 hrs, Human: 4 hrs)
```rust
// ava-platform/src/lib.rs
#[async_trait]
pub trait Platform: Send + Sync {
    async fn read_file(&self, path: &Path) -> Result<String>;
    async fn write_file(&self, path: &Path, content: &str) -> Result<()>;
    async fn execute_shell(&self, command: &str) -> Result<Output>;
    async fn spawn_pty(&self, command: &str) -> Result<Pty>;
}
```
- Platform detection
- **Acceptance:** Trait compiles

**Sprint 24 Total:** 20 points, 2 days AI + 2 days human

### Sprint 25: Infrastructure

**Story 1.4: Database Layer** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-db/src/lib.rs
pub struct Database {
    pool: SqlitePool,
}

impl Database {
    pub async fn save_session(&self, session: &Session) -> Result<()>;
    pub async fn load_session(&self, id: Uuid) -> Result<Session>;
    pub async fn search_sessions(&self, query: &str) -> Result<Vec<Session>>;
}
```
- SQLite with sqlx
- Migrations
- Connection pooling
- **Acceptance:** CRUD operations work

**Story 1.5: Shell Execution** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-shell/src/lib.rs
pub struct ShellExecutor {
    timeout: Duration,
}

impl ShellExecutor {
    pub async fn execute(&self, command: &str) -> Result<Output>;
    pub async fn execute_streaming(&self, command: &str) -> impl Stream<Item = Output>;
}
```
- Async command execution
- Timeout support
- Streaming output
- **Acceptance:** Commands execute, timeout works

**Story 1.6: File Operations** (AI: 4 hrs, Human: 4 hrs)
```rust
// ava-fs/src/lib.rs
pub struct FileSystem;

impl FileSystem {
    pub async fn read(&self, path: &Path) -> Result<String>;
    pub async fn write(&self, path: &Path, content: &str) -> Result<()>;
    pub async fn watch(&self, path: &Path) -> impl Stream<Item = Event>;
}
```
- Async file I/O
- File watching (notify crate)
- **Acceptance:** File ops work, watcher triggers

**Sprint 25 Total:** 32 points, 3 days AI + 3 days human

### Sprint 26: Core Foundation

**Story 1.7: Configuration System** (AI: 4 hrs, Human: 4 hrs)
```rust
// ava-config/src/lib.rs
pub struct Config {
    pub model: ModelConfig,
    pub permissions: PermissionConfig,
    pub extensions: Vec<ExtensionConfig>,
}

impl Config {
    pub fn load() -> Result<Self>;
    pub fn save(&self) -> Result<()>;
}
```
- YAML/JSON support
- Environment variables
- Hot reload
- **Acceptance:** Config loads from disk

**Story 1.8: Logging & Telemetry** (AI: 4 hrs, Human: 4 hrs)
```rust
// ava-logger/src/lib.rs
pub fn init_logging() -> Result<()>;
pub fn log_tool_call(tool: &str, duration: Duration);
pub fn log_llm_request(tokens: usize, cost: f64);
```
- Structured logging
- Metrics collection
- **Acceptance:** Logs write to file

**Story 1.9: Error Handling** (AI: 4 hrs, Human: 4 hrs)
```rust
// ava-types/src/error.rs
#[derive(Error, Debug)]
pub enum AvaError {
    #[error("Tool failed: {0}")]
    ToolError(String),
    #[error("LLM error: {0}")]
    LLMError(String),
    #[error("IO error: {0}")]
    IoError(#[from] std::io::Error),
}
```
- thiserror for ergonomics
- Structured errors
- **Acceptance:** All crates use common error type

**Sprint 26 Total:** 24 points, 2.5 days AI + 2.5 days human

**Epic 1 Complete:** Foundation crates ready

---

## Epic 2: Essential Tools (Sprints 27-29)

**Goal:** Best-in-class edit, search, LSP, sandbox

### Sprint 27: Edit Tool Excellence

**Story 2.1: 9 Edit Strategies** (AI: 8 hrs, Human: 8 hrs)
```rust
// ava-tools/src/edit/strategies/
pub trait EditStrategy {
    fn apply(&self, content: &str, edit: &Edit) -> Result<String>;
}

pub struct ExactMatch;
pub struct FlexibleMatch;  // ignore whitespace
pub struct BlockAnchor;    // context-aware
pub struct RegexMatch;
pub struct FuzzyMatch;     // Levenshtein
// ... 4 more
```
- Port from OpenCode analysis
- **Acceptance:** All 9 strategies work

**Story 2.2: Streaming Fuzzy Matcher** (AI: 8 hrs, Human: 8 hrs)
```rust
// ava-tools/src/edit/streaming.rs
pub struct StreamingMatcher {
    substitution_cost: usize, // 2
    indel_cost: usize,        // 1
}

impl StreamingMatcher {
    pub fn match_stream(&self, stream: TokenStream) -> impl Stream<Item = Match>;
}
```
- Port Zed's approach
- Asymmetric costs
- Real-time matching
- **Acceptance:** 0.5s latency for edits

**Story 2.3: Error Recovery Pipeline** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-tools/src/edit/recovery.rs
pub struct RecoveryPipeline {
    strategies: Vec<Box<dyn EditStrategy>>,
}

impl RecoveryPipeline {
    pub async fn apply_with_recovery(&self, content: &str, edit: &Edit) -> Result<String> {
        // Try exact → flexible → regex → fuzzy
        // If all fail: LLM self-correction
    }
}
```
- 4-tier recovery (Gemini CLI pattern)
- LLM self-correction
- **Acceptance:** 85% recovery rate

**Sprint 27 Total:** 44 points, 4.5 days AI + 4.5 days human

### Sprint 28: Search & Context

**Story 2.4: BM25 Search** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-tools/src/search/bm25.rs
pub struct BM25Index {
    index: tantivy::Index,
}

impl BM25Index {
    pub fn add_document(&mut self, path: &Path, content: &str);
    pub fn search(&self, query: &str) -> Vec<SearchResult>;
}
```
- Tantivy integration
- Real-time indexing
- **Acceptance:** BM25 ranking works

**Story 2.5: PageRank Repo Map** (AI: 8 hrs, Human: 8 hrs)
```rust
// ava-codebase/src/repomap.rs
pub struct RepoMap {
    graph: Graph<Node, Edge>,
}

impl RepoMap {
    pub fn build(root: &Path) -> Result<Self>;
    pub fn rank_files(&self, query: &str) -> Vec<RankedFile>;
}

// Weight definitions (3.0) > declarations (2.0) > identifiers (0.5)
```
- Dependency graph building
- PageRank algorithm
- **Acceptance:** Top-5 relevant files

**Story 2.6: Multi-Strategy Condenser** (AI: 8 hrs, Human: 8 hrs)
```rust
// ava-context/src/condensers/
pub trait Condenser {
    fn condense(&self, context: &Context) -> Context;
}

pub struct RecentCondenser;
pub struct AmortizedForgetting;
pub struct ObservationMasking;  // keep actions, mask observations
pub struct LLMSummarization;
// ... 5 more (from OpenHands)
```
- 9 condenser strategies
- Auto-selection based on token pressure
- **Acceptance:** All strategies work

**Sprint 28 Total:** 44 points, 4.5 days AI + 4.5 days human

### Sprint 29: LSP & Sandboxing

**Story 2.7: LSP Client** (AI: 8 hrs, Human: 8 hrs)
```rust
// ava-lsp/src/client.rs
pub struct LSPClient {
    connection: Connection,
}

impl LSPClient {
    pub async fn goto_definition(&self, params: DefinitionParams) -> Result<Location>;
    pub async fn get_diagnostics(&self, path: &Path) -> Vec<Diagnostic>;
    pub fn stream_diagnostics(&self) -> impl Stream<Item = Diagnostic>;
}
```
- Zero-copy communication
- Streaming diagnostics
- **Acceptance:** Real-time error detection

**Story 2.8: OS-Level Sandboxing** (AI: 8 hrs, Human: 8 hrs)
```rust
// ava-sandbox/src/lib.rs
pub struct Sandbox {
    ruleset: LandlockRuleset,
}

impl Sandbox {
    pub fn new() -> Result<Self> {
        // Linux: Landlock + seccomp
        // macOS: Seatbelt
    }
    
    pub async fn execute(&self, command: &str) -> Result<Output>;
}
```
- Landlock (Linux)
- Seatbelt (macOS)
- Seccomp BPF
- Network proxy
- **Acceptance:** 100ms sandbox startup

**Story 2.9: Terminal Security Classifier** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-shell/src/security.rs
pub struct SecurityClassifier;

impl SecurityClassifier {
    pub fn classify(&self, command: &str) -> RiskLevel {
        // Tree-sitter bash parsing
        // Check for: rm -rf, curl | sh, etc.
    }
}
```
- Tree-sitter for bash
- Risk assessment
- **Acceptance:** Dangerous commands flagged

**Sprint 29 Total:** 44 points, 4.5 days AI + 4.5 days human

**Epic 2 Complete:** Essential tools best-in-class

---

## Epic 3: Agent Core (Sprints 30-32)

**Goal:** Agent loop, commander, LLM, MCP

### Sprint 30: Agent Loop

**Story 3.1: Async Agent Loop** (AI: 8 hrs, Human: 8 hrs)
```rust
// ava-agent/src/loop.rs
pub struct AgentLoop {
    llm: Box<dyn LLMProvider>,
    tools: ToolRegistry,
    context: ContextManager,
}

impl AgentLoop {
    pub async fn run(&mut self, goal: &str) -> Result<Session> {
        loop {
            let response = self.llm.generate(&self.context).await?;
            let tool_calls = parse_tool_calls(&response);
            
            for call in tool_calls {
                let result = self.tools.execute(call).await?;
                self.context.add_tool_result(result);
            }
            
            if should_complete(&response) {
                break;
            }
        }
    }
}
```
- Tokio-based async
- Tool dispatch pipeline
- **Acceptance:** Basic loop works

**Story 3.2: Tool Registry** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-tools/src/registry.rs
pub struct ToolRegistry {
    tools: HashMap<String, Box<dyn Tool>>,
    middleware: Vec<Box<dyn Middleware>>,
}

impl ToolRegistry {
    pub fn register(&mut self, tool: Box<dyn Tool>);
    pub async fn execute(&self, call: ToolCall) -> Result<ToolResult>;
}
```
- Dynamic registration
- Middleware support
- **Acceptance:** 35 tools registered

**Story 3.3: Context Manager** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-context/src/manager.rs
pub struct ContextManager {
    condensers: Vec<Box<dyn Condenser>>,
    token_limit: usize,
}

impl ContextManager {
    pub async fn compact_if_needed(&mut self);
    pub fn get_context(&self) -> Context;
}
```
- Token tracking
- Auto-compaction
- **Acceptance:** Context stays under limit

**Sprint 30 Total:** 40 points, 4 days AI + 4 days human

### Sprint 31: Commander & LLM

**Story 3.4: Commander (Praxis Hierarchy)** (AI: 8 hrs, Human: 8 hrs)
```rust
// ava-commander/src/lib.rs
pub struct Commander {
    leads: Vec<Lead>,
    budget: Budget,
}

impl Commander {
    pub async fn delegate(&self, task: Task) -> Result<Worker> {
        // Route to appropriate lead
        // FrontendLead, BackendLead, QALead, etc.
    }
}

pub struct Lead {
    workers: Vec<Worker>,
    specialization: Domain,
}
```
- 3-tier hierarchy
- Worker spawning
- Budget management
- **Acceptance:** Delegation works

**Story 3.5: LLM Provider Abstraction** (AI: 8 hrs, Human: 8 hrs)
```rust
// ava-llm/src/lib.rs
#[async_trait]
pub trait LLMProvider: Send + Sync {
    async fn generate(&self, messages: &[Message]) -> Result<String>;
    async fn generate_stream(&self, messages: &[Message]) -> impl Stream<Item = String>;
    fn estimate_cost(&self, tokens: usize) -> f64;
}

pub struct OpenAIProvider;
pub struct AnthropicProvider;
pub struct OpenRouterProvider;
// ... 10+ more
```
- 13+ providers
- Streaming support
- Cost tracking
- **Acceptance:** All providers work

**Story 3.6: Model Router** (AI: 4 hrs, Human: 4 hrs)
```rust
// ava-llm/src/router.rs
pub struct ModelRouter {
    providers: HashMap<String, Box<dyn LLMProvider>>,
}

impl ModelRouter {
    pub fn route(&self, task: &Task) -> &dyn LLMProvider {
        // Route based on: task type, cost, speed, quality
    }
}
```
- Per-task model selection
- Cost optimization
- **Acceptance:** Smart routing works

**Sprint 31 Total:** 40 points, 4 days AI + 4 days human

### Sprint 32: MCP & Integration

**Story 3.7: MCP Client** (AI: 8 hrs, Human: 8 hrs)
```rust
// ava-mcp/src/client.rs
pub struct MCPClient {
    servers: HashMap<String, MCPServer>,
}

impl MCPClient {
    pub async fn connect(&mut self, config: ServerConfig) -> Result<()>;
    pub async fn list_tools(&self, server: &str) -> Vec<Tool>;
    pub async fn call_tool(&self, server: &str, call: ToolCall) -> Result<ToolResult>;
}
```
- Server management
- Tool discovery
- OAuth support
- **Acceptance:** MCP tools accessible

**Story 3.8: MCP Server Mode** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-mcp/src/server.rs
pub struct AVAMCPServer;

impl MCPServer for AVAMCPServer {
    fn list_tools(&self) -> Vec<Tool>;
    async fn call_tool(&self, call: ToolCall) -> Result<ToolResult>;
}
```
- Expose AVA tools via MCP
- Other agents can call AVA
- **Acceptance:** Zed can use AVA tools

**Story 3.9: Session Management** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-session/src/lib.rs
pub struct SessionManager {
    db: Database,
}

impl SessionManager {
    pub async fn create(&self) -> Session;
    pub async fn fork(&self, session: &Session) -> Session;
    pub async fn merge(&self, base: &Session, branch: &Session) -> Session;
    pub async fn search(&self, query: &str) -> Vec<Session>;
}
```
- DAG-based sessions
- Fork/merge
- FTS5 search
- **Acceptance:** All operations work

**Sprint 32 Total:** 40 points, 4 days AI + 4 days human

**Epic 3 Complete:** Core agent functionality

---

## Epic 4: Complete Backend (Sprints 33-35)

**Goal:** All remaining tools, polish, performance

### Sprint 33: Remaining Tools

**Story 4.1: Git Tools** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-tools/src/git/
pub struct GitTool;

impl Tool for GitTool {
    async fn execute(&self, action: GitAction) -> Result<String> {
        match action {
            GitAction::Commit(msg) => self.commit(msg).await,
            GitAction::Branch(name) => self.branch(name).await,
            GitAction::PR { title, body } => self.create_pr(title, body).await,
            // ... 6 more git operations
        }
    }
}
```
- 6 git tools
- **Acceptance:** Git operations work

**Story 4.2: Browser Tool** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-tools/src/browser.rs
pub struct BrowserTool;

impl Tool for BrowserTool {
    async fn navigate(&self, url: &str) -> Result<Page>;
    async fn click(&self, selector: &str) -> Result<()>;
    async fn extract(&self) -> Result<String>;
}
```
- Browser automation
- Accessibility tree
- **Acceptance:** Can browse websites

**Story 4.3: Memory Tools** (AI: 4 hrs, Human: 4 hrs)
```rust
// ava-memory/src/lib.rs
pub struct MemorySystem {
    db: Database,
}

impl MemorySystem {
    pub async fn remember(&self, key: &str, value: &str);
    pub async fn recall(&self, key: &str) -> Option<String>;
    pub async fn search(&self, query: &str) -> Vec<Memory>;
}
```
- Long-term memory
- FTS5 search
- **Acceptance:** Cross-session recall

**Story 4.4: Permission System** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-permissions/src/lib.rs
pub struct PermissionSystem {
    rules: Vec<Rule>,
}

impl PermissionSystem {
    pub fn evaluate(&self, tool: &str, args: &Args) -> Permission {
        // Four-tier: Always, Auto Attached, Agent Requested, Manual
        // Dynamic escalation based on args
    }
}
```
- Four-tier rules
- Dynamic escalation
- **Acceptance:** Granular permissions

**Sprint 33 Total:** 44 points, 4.5 days AI + 4.5 days human

### Sprint 34: Extensions & Validation

**Story 4.5: Extension System** (AI: 8 hrs, Human: 8 hrs)
```rust
// ava-extensions/src/lib.rs
pub trait Extension: Send + Sync {
    fn register_tools(&self, registry: &mut ToolRegistry);
    fn register_hooks(&self, hooks: &mut HookRegistry);
}

pub struct ExtensionManager {
    extensions: Vec<Box<dyn Extension>>,
}
```
- Native extensions (dylib)
- WASM support (sandboxed)
- Hot reload
- **Acceptance:** Extensions load

**Story 4.6: Validation Pipeline** (AI: 6 hrs, Human: 6 hrs)
```rust
// ava-validator/src/lib.rs
pub struct ValidationPipeline;

impl ValidationPipeline {
    pub async fn validate_edit(&self, path: &Path, content: &str) -> Result<()> {
        // 1. Tree-sitter syntax check
        // 2. Compilation check (if applicable)
        // 3. Lint check
        // If fail: retry with error context
    }
}
```
- Post-edit validation
- Auto-retry
- **Acceptance:** Invalid edits caught

**Story 4.7: Reflection Loop** (AI: 4 hrs, Human: 4 hrs)
```rust
// ava-agent/src/reflection.rs
pub struct ReflectionLoop;

impl ReflectionLoop {
    pub async fn reflect(&self, result: &ToolResult) -> Result<Action> {
        // Check for errors
        // Self-correct if needed
        // Aider pattern
    }
}
```
- Self-correction
- Error recovery
- **Acceptance:** Auto-retry works

**Sprint 34 Total:** 36 points, 3.5 days AI + 3.5 days human

### Sprint 35: Performance & Polish

**Story 4.8: Performance Optimization** (AI: 8 hrs, Human: 8 hrs)
- Profile with `cargo flamegraph`
- Zero-copy where possible
- Memory-mapped files
- **Acceptance:** 50% faster than baseline

**Story 4.9: Testing Suite** (AI: 6 hrs, Human: 6 hrs)
```rust
// tests/
mod unit;
mod integration;
mod property_based;  // proptest
mod benchmark;       // criterion
```
- Comprehensive test coverage
- **Acceptance:** >80% coverage

**Story 4.10: Documentation** (AI: 4 hrs, Human: 4 hrs)
- Rust docs (cargo doc)
- Architecture docs
- API reference
- **Acceptance:** Docs complete

**Sprint 35 Total:** 36 points, 3.5 days AI + 3.5 days human

**Epic 4 Complete:** Backend 100% Rust, polished

---

## Epic 5: Frontend Integration (Sprints 36-38)

**Goal:** Wire TypeScript frontend to Rust backend

### Sprint 36: Tauri Integration

**Story 5.1: Tauri Commands** (AI: 6 hrs, Human: 6 hrs)
```rust
// src-tauri/src/commands.rs
#[tauri::command]
async fn execute_tool(tool: String, args: Value) -> Result<Value> {
    let registry = state.tool_registry.lock().await;
    registry.execute(tool, args).await
}

#[tauri::command]
async fn agent_run(goal: String) -> Result<Session> {
    let mut agent = state.agent.lock().await;
    agent.run(&goal).await
}
```
- Expose Rust functions to TypeScript
- **Acceptance:** Frontend can call Rust

**Story 5.2: Event Streaming** (AI: 6 hrs, Human: 6 hrs)
```rust
// Stream LLM tokens to frontend
#[tauri::command]
async fn agent_stream(goal: String, window: Window) -> Result<()> {
    let mut stream = agent.generate_stream(&goal).await;
    while let Some(token) = stream.next().await {
        window.emit("token", token)?;
    }
}
```
- Real-time streaming
- **Acceptance:** Tokens appear as they generate

**Story 5.3: State Management** (AI: 4 hrs, Human: 4 hrs)
```rust
// src-tauri/src/state.rs
pub struct AppState {
    agent: Mutex<AgentLoop>,
    tools: ToolRegistry,
    db: Database,
}
```
- Shared state between commands
- **Acceptance:** State persists

**Sprint 36 Total:** 32 points, 3 days AI + 3 days human

### Sprint 37: Frontend Updates

**Story 5.4: Update Tool Calls** (AI: 6 hrs, Human: 6 hrs)
```typescript
// src/hooks/useTools.ts
export async function executeTool(tool: string, args: unknown) {
  return await invoke('execute_tool', { tool, args });
}
```
- Replace TS tool implementations with Rust calls
- **Acceptance:** Tools work via Rust

**Story 5.5: Update Agent Loop** (AI: 6 hrs, Human: 6 hrs)
```typescript
// src/hooks/useAgent.ts
export function useAgent() {
  const run = async (goal: string) => {
    await invoke('agent_run', { goal });
  };
  
  const stream = async (goal: string, onToken: (t: string) => void) => {
    listen('token', (event) => onToken(event.payload));
    await invoke('agent_stream', { goal });
  };
}
```
- Replace TS agent with Rust agent
- **Acceptance:** Agent runs in Rust

**Story 5.6: Update UI Components** (AI: 4 hrs, Human: 4 hrs)
- Per-hunk review UI
- Streaming display
- Progress indicators
- **Acceptance:** UI reflects Rust backend

**Sprint 37 Total:** 32 points, 3 days AI + 3 days human

### Sprint 38: Testing & Migration

**Story 5.7: End-to-End Tests** (AI: 6 hrs, Human: 6 hrs)
- Test full workflows
- Edit → validate → commit
- **Acceptance:** E2E tests pass

**Story 5.8: Remove Old TypeScript** (AI: 4 hrs, Human: 4 hrs)
- Delete packages/core/src/
- Keep only frontend code
- **Acceptance:** No TS backend code

**Story 5.9: Migration Guide** (AI: 2 hrs, Human: 2 hrs)
- Document breaking changes
- Migration script
- **Acceptance:** Users can migrate

**Sprint 38 Total:** 24 points, 2 days AI + 2 days human

**Epic 5 Complete:** Frontend integrated, old code removed

---

## Epic 6: Ship It (Sprints 39-41)

**Goal:** Production ready

### Sprint 39: Bug Fixes

**Story 6.1: Bug Bash** (Team: 40 pts)
- Fix all P0/P1 bugs
- Edge cases
- Platform-specific issues
- **Acceptance:** Zero P0 bugs

### Sprint 40: Performance

**Story 6.2: Final Optimization** (Team: 40 pts)
- Profile critical paths
- Optimize hot loops
- Reduce allocations
- **Acceptance:** Meets perf targets

### Sprint 41: Release

**Story 6.3: Release Prep** (Team: 30 pts)
- Version bump
- Changelog
- Release notes
- Distribution builds
- **Acceptance:** Ready to ship

---

## Summary

### Timeline

| Phase | Sprints | Duration | Focus |
|-------|---------|----------|-------|
| Foundation | 24-26 | 6 weeks | Core crates |
| Tools | 27-29 | 6 weeks | Best-in-class tools |
| Agent | 30-32 | 6 weeks | Loop, LLM, MCP |
| Backend | 33-35 | 6 weeks | Complete, polish |
| Integration | 36-38 | 6 weeks | Frontend wiring |
| Ship | 39-41 | 6 weeks | Bugs, perf, release |
| **Total** | **18 sprints** | **36 weeks** | **9 months** |

### Story Points

| Epic | Points | AI Hours | Human Hours |
|------|--------|----------|-------------|
| 1. Foundation | 76 | 30 | 30 |
| 2. Tools | 132 | 54 | 54 |
| 3. Agent | 120 | 48 | 48 |
| 4. Complete | 116 | 46 | 46 |
| 5. Integration | 88 | 34 | 34 |
| 6. Ship | 110 | 44 | 44 |
| **Total** | **642** | **256** | **256** |

**Total effort:** ~512 hours AI + 512 hours human = **~6.5 person-months**

With 2 AI-human pairs: **3.25 months**
With 4 AI-human pairs: **1.6 months**

### Success Metrics

| Metric | Before | After |
|--------|--------|-------|
| Startup time | 3s | 0.1s |
| Edit latency | 3s | 0.5s |
| Edit success | 70% | 90% |
| Memory usage | 300MB | 50MB |
| Sandbox startup | 5s | 0.1s |
| Binary size | Huge (node) | ~10MB |

---

## No Temporary TypeScript

**Deleted concepts:**
- ❌ TypeScript bridge
- ❌ Gradual migration
- ❌ Dual implementations
- ❌ "Prototype in TS first"
- ❌ 18-month timeline

**New approach:**
- ✅ Write Rust correctly first time
- ✅ AI writes most code
- ✅ Human reviews and tests
- ✅ Direct path to final architecture
- ✅ 9-month timeline (aggressive but realistic with AI)

---

## Next Steps

1. **Sprint 24 (Now):** Setup workspace, write types
2. **Parallel work:** 2-4 AI-human pairs
3. **Daily standups:** Review AI-generated code
4. **Weekly demos:** Show working features
5. **Ship in 9 months:** Best-in-class Rust backend

Ready to start Sprint 24?
