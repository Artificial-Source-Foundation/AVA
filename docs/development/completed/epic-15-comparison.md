# Epic 15: In-Depth Codebase Comparison

> Estela vs. SOTA AI Coding Agents - Technical Deep Dive

---

## Executive Summary

| Project | Language | LOC (Core) | Stars | Architecture |
|---------|----------|------------|-------|--------------|
| **Estela** | TypeScript | ~23,000 | - | Monorepo, Platform-agnostic |
| **OpenCode** | TypeScript | ~10,000 | 70k+ | Bun-native, Plugin-based |
| **Aider** | Python | ~35,000 | 25k+ | Monolithic, Git-native |
| **Goose** | Rust | ~15,000 | 15k+ | Crate-based, MCP-first |
| **Gemini CLI** | TypeScript | ~57,000 | 50k+ | Monorepo, Policy-driven |

---

## 1. Module-by-Module Comparison

### 1.1 Agent Loop Implementation

#### Estela (~2,900 LOC)
```
packages/core/src/agent/
├── loop.ts          (450 LOC)  - Main executor
├── evaluator.ts     (350 LOC)  - Progress tracking
├── planner.ts       (350 LOC)  - Task planning
├── recovery.ts      (400 LOC)  - Error recovery with backoff
├── events.ts        (350 LOC)  - Event emission & buffering
└── types.ts         (500 LOC)  - Type definitions
```

**Key Pattern: Event-driven with recovery**
```typescript
// Estela uses enum-based termination tracking
enum AgentTerminateMode {
  ERROR, TIMEOUT, GOAL, MAX_TURNS, ABORTED, NO_COMPLETE_TASK
}

interface AgentConfig {
  maxTimeMinutes: number     // Default: 10
  maxTurns: number           // Default: 20
  maxRetries: number         // Default: 3
  gracePeriodMs: number      // Default: 60,000
}
```

#### OpenCode (~1,100 LOC)
```
packages/opencode/src/session/
├── processor.ts     (406 LOC)  - Main loop with doom detection
├── llm.ts           (200 LOC)  - LLM streaming
└── message-v2.ts    (22KB)     - Message types (15+ part types)
```

**Key Pattern: Doom loop detection**
```typescript
// OpenCode detects identical tool calls (3 threshold)
const DOOM_LOOP_THRESHOLD = 3;

const lastThree = parts.slice(-3);
if (lastThree.every(p =>
  p.type === "tool" &&
  p.tool === toolName &&
  JSON.stringify(p.input) === JSON.stringify(input)
)) {
  // Ask permission to continue or break
}
```

#### Aider (~3,000 LOC)
```
aider/
├── main.py          (1,274 LOC) - Event loop + orchestration
├── commands.py      (1,694 LOC) - Command dispatcher
└── coders/base_coder.py (2,000+ LOC) - Base coder with edit loop
```

**Key Pattern: Exception-based control flow**
```python
class SwitchCoder(Exception):
    """Signal to switch to different edit mode"""
    pass

def cmd_chat_mode(self, args):
    ef = args.strip()
    raise SwitchCoder(edit_format=ef)  # Mode switch via exception
```

#### Goose (~1,300 LOC)
```
crates/goose/src/
├── execution/manager.rs (312 LOC) - Agent lifecycle
├── scheduler.rs         (988 LOC) - Task scheduling
└── session/session_manager.rs (1,639 LOC) - Session control
```

**Key Pattern: Builder pattern for state updates**
```rust
session_manager
    .update(session_id)
    .user_provided_name("My Project")
    .session_type(SessionType::User)
    .total_tokens(Some(5000))
    .apply()
    .await?;
```

#### Gemini CLI (~3,200 LOC)
```
packages/core/src/core/
├── client.ts        (500+ LOC) - GeminiClient loop
├── geminiChat.ts    (400+ LOC) - Chat/turn management
└── services/        (11.9k LOC) - Support services
```

**Key Pattern: Hook integration with deduplication**
```typescript
private hookStateMap = new Map<string, {
  hasFiredBeforeAgent: boolean;
  cumulativeResponse: string;
  activeCalls: number;
}>();

// Hooks fire once per prompt, deduplicated
if (hookState.hasFiredBeforeAgent) return undefined;
```

### Comparison Table: Agent Loop

| Feature | Estela | OpenCode | Aider | Goose | Gemini CLI |
|---------|--------|----------|-------|-------|------------|
| Max turns configurable | ✅ 20 | ✅ 100 | ✅ var | ✅ var | ✅ 100 |
| Doom loop detection | ❌ | ✅ 3x | ❌ | ❌ | ❌ |
| Recovery with backoff | ✅ | ❌ | ❌ | ❌ | ✅ |
| Event streaming | ✅ | ✅ | ❌ | ✅ | ✅ |
| Progress evaluation | ✅ | ❌ | ❌ | ❌ | ❌ |
| Planning phase | ✅ | ❌ | ❌ | ❌ | ❌ |
| Hook system | ❌ | ❌ | ❌ | ❌ | ✅ |

---

### 1.2 Tool System

#### Estela (~2,876 LOC)
```
packages/core/src/tools/
├── bash.ts          (490 LOC)  - Shell with PTY
├── read.ts          (215 LOC)  - File reading
├── write.ts         (165 LOC)  - File writing
├── grep.ts          (231 LOC)  - Pattern search
├── glob.ts          (166 LOC)  - File globbing
├── define.ts        (206 LOC)  - Custom tool definition
├── locks.ts         (221 LOC)  - File locking
└── registry.ts      (123 LOC)  - Tool registration
```

**Key Pattern: defineTool() with Zod validation**
```typescript
interface ToolConfig<T> {
  name: string
  description: string
  validate: (params: unknown) => T
  execute: (params: T, context: ToolContext) => Promise<string>
}

// Usage
const myTool = defineTool<MyParams>({
  name: 'my-tool',
  validate: z.object({ path: z.string() }).parse,
  execute: async (params, ctx) => { ... }
})
```

#### OpenCode (~2,400 LOC)
```
packages/opencode/src/tool/
├── registry.ts      (163 LOC)  - Central registry
├── tool.ts          (90 LOC)   - Tool.define interface
├── bash.ts          (258 LOC)  - Tree-sitter parsing
├── edit.ts          (655 LOC)  - LSP integration
├── read.ts          (211 LOC)  - With truncation
└── batch.ts         (175 LOC)  - Batch operations
```

**Key Pattern: Tool.define() with async init**
```typescript
export const BashTool = Tool.define("bash", async () => {
  return {
    description: DESCRIPTION.replaceAll("${directory}", Instance.directory),
    parameters: z.object({ command: z.string() }),
    async execute(params, ctx) {
      // 1. Parse with tree-sitter (bash grammar)
      const tree = await parser().then(p => p.parse(params.command));
      // 2. Extract directories for permission
      // 3. Request permission via ctx.ask()
      await ctx.ask({ permission: "bash", patterns: [...] });
      // 4. Execute
      return Truncate.output(result, {}, ctx.agent);
    }
  }
});
```

#### Aider (~20,000 LOC)
```
aider/coders/
├── base_coder.py        (2,000+ LOC) - Base class
├── editblock_coder.py   (706 LOC)   - SEARCH/REPLACE
├── udiff_coder.py       (429 LOC)   - Unified diff
├── patch_coder.py       (706 LOC)   - Complex patches
├── wholefile_coder.py   (144 LOC)   - Full replacement
├── search_replace.py    (757 LOC)   - Matching utilities
└── [15+ format coders]
```

**Key Pattern: Pluggable edit format coders**
```python
class Coder:
    edit_format = None  # Subclasses override

    def get_edits(self) -> List[Edit]:
        """Parse LLM output into edits"""
        pass

    def apply_edits(self, edits):
        """Apply edits to filesystem"""
        pass

# SEARCH/REPLACE format
<<<<<<< SEARCH
original text
=======
replacement text
>>>>>>> REPLACE
```

#### Goose (~3,600 LOC MCP)
```
crates/goose-mcp/src/
├── rmcp_developer.rs (3,614 LOC) - Developer tools server
│   ├── text_editor    - View, write, replace, insert, undo
│   ├── shell          - Command execution
│   └── screen_capture - Display capture
└── mcp_server_runner.rs (100 LOC) - Server spawning
```

**Key Pattern: MCP-first tool definition**
```rust
// Tools defined as MCP servers
tool_router! {
    self.screen_capture_handler,
    self.text_editor_handler,
    self.shell_handler,
    self.image_processor_handler,
}

// Spawned as separate processes
macro_rules! builtin {
    ($name:ident, $server_ty:ty) => {{
        fn spawn(r: DuplexStream, w: DuplexStream) {
            spawn_and_serve::<$server_ty>(r, w);
        }
    }};
}
```

#### Gemini CLI (~26,600 LOC)
```
packages/core/src/tools/
├── tools.ts         (765 LOC)  - Base classes & types
├── edit.ts          (765 LOC)  - Complex diff logic
├── shell.ts         (526 LOC)  - Process groups
├── write-file.ts    (555 LOC)  - File creation
├── read-file.ts     (288 LOC)  - Efficient reading
├── ripGrep.ts       (618 LOC)  - Code search
├── mcp-tool.ts      (421 LOC)  - MCP integration
└── tool-registry.ts (550 LOC)  - Dynamic discovery
```

**Key Pattern: ToolBuilder/ToolInvocation separation**
```typescript
// ToolBuilder validates and creates ToolInvocation
interface ToolBuilder<TParams, TResult> {
  build(params: TParams): ToolInvocation<TParams, TResult>;
}

// ToolInvocation is the validated, ready-to-execute state
interface ToolInvocation<TParams, TResult> {
  getDescription(): string;
  shouldConfirmExecute(signal: AbortSignal): Promise<Details | false>;
  execute(signal: AbortSignal): Promise<TResult>;
}
```

### Comparison Table: Tools

| Feature | Estela | OpenCode | Aider | Goose | Gemini CLI |
|---------|--------|----------|-------|-------|------------|
| Total tool LOC | 2,876 | 2,400 | 20,000 | 3,600 | 26,600 |
| Edit formats | 1 | 1 | 15+ | 1 | 1 |
| File locking | ✅ | ✅ | ❌ | ❌ | ❌ |
| Batch operations | ❌ | ✅ | ❌ | ❌ | ❌ |
| Tree-sitter parsing | ❌ | ✅ bash | ✅ all | ❌ | ❌ |
| LSP integration | ❌ | ✅ | ❌ | ❌ | ❌ |
| MCP-native | ✅ | ✅ | ❌ | ✅ | ✅ |
| Process groups | ✅ | ❌ | ❌ | ✅ | ✅ |
| Binary detection | ✅ | ✅ | ❌ | ❌ | ✅ |

---

### 1.3 Permission System

#### Estela (~784 LOC)
```
packages/core/src/permissions/
├── manager.ts       (400 LOC)  - Risk assessment
├── rules.ts         (200 LOC)  - 13 built-in rules
└── types.ts         (184 LOC)  - Type definitions
```

**Key Pattern: Risk-based approval**
```typescript
interface PermissionRule {
  pattern: string | RegExp
  risk: 'low' | 'medium' | 'high' | 'critical'
  requireConfirmation: boolean
}

const RULES = [
  { pattern: /rm -rf/, risk: 'critical', requireConfirmation: true },
  { pattern: /\.env/, risk: 'high', requireConfirmation: true },
  // ... 13 built-in rules
]
```

#### OpenCode (~500 LOC)
```
packages/opencode/src/permission/
├── next.ts          (500 LOC)  - PermissionNext system
└── (integrated in tool/*.ts)
```

**Key Pattern: ctx.ask() per-tool permission**
```typescript
// Each tool requests permission explicitly
await ctx.ask({
  permission: "edit",
  patterns: [path.relative(Instance.worktree, filePath)],
  always: ["*"],
  metadata: { filepath, diff }
});
```

#### Aider (~0 LOC dedicated)
**No permission system** - relies on user trust and git rollback.

#### Goose (~637 LOC)
```
crates/goose/src/permission/
├── permission_judge.rs       (273 LOC)  - LLM-based detection
├── permission_inspector.rs   (187 LOC)  - Permission checking
├── permission_store.rs       (144 LOC)  - Persistence
└── permission_confirmation.rs (23 LOC)  - UI
```

**Key Pattern: Three-layer permission model**
```rust
// Layer 1: User-defined (permission.yaml)
pub enum PermissionLevel {
    AlwaysAllow,
    AskBefore,
    NeverAllow,
}

// Layer 2: Tool-level registry
pub struct PermissionInspector {
    readonly_tools: HashSet<String>,
    regular_tools: HashSet<String>,
}

// Layer 3: LLM-based read-only detection
pub async fn detect_read_only_tools(
    provider: Arc<dyn Provider>,
    tool_requests: Vec<&ToolRequest>,
) -> Vec<String>
```

#### Gemini CLI (~1,000+ LOC)
```
packages/core/src/
├── tools/tools.ts   (200 LOC)  - Kind enum & policy
├── policy/          (500+ LOC) - Policy engine
└── confirmation-bus/(300 LOC)  - Message bus
```

**Key Pattern: Message bus with correlation IDs**
```typescript
enum Kind {
  Read, Edit, Delete, Move, Search, Execute, Think, Fetch, Communicate, Other
}

const MUTATOR_KINDS: Kind[] = [Kind.Edit, Kind.Delete, Kind.Move, Kind.Execute];

// Policy decisions via message bus
protected getMessageBusDecision(signal: AbortSignal): Promise<'ALLOW' | 'DENY' | 'ASK_USER'> {
  const request: ToolConfirmationRequest = {
    correlationId: randomUUID(),
    toolCall: { name: this._toolName, args: this.params },
  };
  // Subscribe and wait for response with 30s timeout
}
```

### Comparison Table: Permissions

| Feature | Estela | OpenCode | Aider | Goose | Gemini CLI |
|---------|--------|----------|-------|-------|------------|
| Permission LOC | 784 | 500 | 0 | 637 | 1,000+ |
| Risk classification | ✅ | ❌ | ❌ | ❌ | ✅ Kind |
| LLM-based detection | ❌ | ❌ | ❌ | ✅ | ❌ |
| Path restrictions | ✅ | ✅ | ❌ | ✅ | ✅ |
| Persistent approvals | ✅ | ❌ | ❌ | ✅ | ✅ |
| Context-aware caching | ❌ | ❌ | ❌ | ✅ | ❌ |
| Message bus | ❌ | ❌ | ❌ | ❌ | ✅ |

---

### 1.4 Codebase Understanding

#### Estela (~2,835 LOC)
```
packages/core/src/codebase/
├── indexer.ts       (350 LOC)  - File discovery
├── graph.ts         (300 LOC)  - Dependency graph
├── ranking.ts       (250 LOC)  - PageRank algorithm
├── repomap.ts       (250 LOC)  - Repo mapping
├── imports.ts       (200 LOC)  - Import parsing
└── symbols.ts       (200 LOC)  - Symbol extraction
```

**Key Pattern: Custom PageRank implementation**
```typescript
export function calculatePageRank(
  graph: Map<string, DependencyNode>,
  options?: { damping?: number; iterations?: number }
): Map<string, number> {
  const damping = options?.damping ?? 0.85;
  const iterations = options?.iterations ?? 100;

  // Iterative PageRank calculation
  for (let i = 0; i < iterations; i++) {
    for (const [file, node] of graph) {
      let sum = 0;
      for (const importer of node.importedBy) {
        const importerNode = graph.get(importer);
        sum += ranks.get(importer)! / importerNode!.imports.length;
      }
      newRanks.set(file, (1 - damping) / N + damping * sum);
    }
  }
}
```

#### OpenCode (~1,000 LOC)
```
packages/opencode/src/
├── patch/           (400 LOC)  - Patch management
├── project/         (300 LOC)  - Project detection
└── lsp/             (300 LOC)  - Language Server Protocol
```

**Key Pattern: LSP for symbol extraction**
```typescript
// Uses LSP for diagnostics and symbols
const diagnostics = await LSP.diagnostics();
```

#### Aider (~848 LOC)
```
aider/
├── repomap.py       (848 LOC)  - NetworkX PageRank
└── queries/         (tree-sitter language pack)
```

**Key Pattern: NetworkX PageRank with personalization**
```python
# Line 506: PageRank with chat file boosting
ranked = nx.pagerank(G, weight="weight", **pers_args)

# Personalization weights
if referencer in chat_rel_fnames:
    use_mul *= 50  # 50x boost for files mentioned in chat

if ident in mentioned_idents:
    mul *= 10  # 10x for mentioned symbols

# Binary search for token budget
def get_ranked_tags(self, chat_fnames, other_fnames, max_map_tokens):
    # Find optimal number within token limit
```

#### Goose (~0 LOC dedicated)
**No codebase understanding** - relies on MCP tools and LLM's inherent capabilities.

#### Gemini CLI (~0 LOC dedicated)
**No codebase understanding** - relies on tools like ripGrep and glob.

### Comparison Table: Codebase Understanding

| Feature | Estela | OpenCode | Aider | Goose | Gemini CLI |
|---------|--------|----------|-------|-------|------------|
| Understanding LOC | 2,835 | 1,000 | 848 | 0 | 0 |
| File indexing | ✅ | ✅ | ✅ | ❌ | ❌ |
| Symbol extraction | ✅ | ✅ LSP | ✅ tree-sitter | ❌ | ❌ |
| Dependency graph | ✅ | ❌ | ✅ | ❌ | ❌ |
| PageRank ranking | ✅ | ❌ | ✅ NetworkX | ❌ | ❌ |
| Circular dep detection | ✅ | ❌ | ❌ | ❌ | ❌ |
| Repo map generation | ✅ | ✅ | ✅ | ❌ | ❌ |
| Token budget search | ❌ | ❌ | ✅ binary | ❌ | ❌ |
| Languages supported | 19+ | via LSP | 100+ | N/A | N/A |

---

### 1.5 Memory System

#### Estela (~2,747 LOC)
```
packages/core/src/memory/
├── manager.ts       (300 LOC)  - Unified interface
├── episodic.ts      (200 LOC)  - Session memories
├── semantic.ts      (200 LOC)  - Fact memories
├── procedural.ts    (150 LOC)  - Pattern memories
├── consolidation.ts (200 LOC)  - Decay & consolidation
├── embedding.ts     (250 LOC)  - Vector embeddings
└── store.ts         (200 LOC)  - SQLite storage
```

**Key Pattern: Three-tier cognitive memory**
```typescript
// Episodic: Session memories
interface EpisodicMemory {
  sessionId: string
  summary: string
  decisions: string[]
  toolsUsed: string[]
  outcome: 'success' | 'failure' | 'partial'
}

// Semantic: Factual knowledge
interface SemanticMemory {
  fact: string
  source: string
  confidence: number  // 0-1
  tags: string[]
}

// Procedural: Skill patterns
interface ProceduralMemory {
  context: string     // "User asks to X"
  action: string      // "Run this command"
  tools: string[]
  successRate: number
}

// Consolidation with decay
const DEFAULT_DECAY_RATE = 0.001;  // λ in e^(-λt)
```

#### OpenCode (~0 LOC dedicated)
**No memory system** - relies on session storage and compaction.

#### Aider (~0 LOC dedicated)
**No memory system** - uses git history and repo map caching.

#### Goose (~191 LOC)
```
crates/goose/src/session/
└── extension_data.rs (191 LOC)  - Extension storage
```

**Key Pattern: Extension-based storage**
```rust
pub struct ExtensionData {
    data: HashMap<String, serde_json::Value>,
}
```

#### Gemini CLI (~0 LOC dedicated)
**No memory system** - uses compression service for long conversations.

### Comparison Table: Memory

| Feature | Estela | OpenCode | Aider | Goose | Gemini CLI |
|---------|--------|----------|-------|-------|------------|
| Memory LOC | 2,747 | 0 | 0 | 191 | 0 |
| Episodic memory | ✅ | ❌ | ❌ | ❌ | ❌ |
| Semantic memory | ✅ | ❌ | ❌ | ❌ | ❌ |
| Procedural memory | ✅ | ❌ | ❌ | ❌ | ❌ |
| Vector embeddings | ✅ | ❌ | ❌ | ❌ | ❌ |
| Similarity search | ✅ | ❌ | ❌ | ❌ | ❌ |
| Memory consolidation | ✅ | ❌ | ❌ | ❌ | ❌ |
| SQLite storage | ✅ | ❌ | ✅ cache | ✅ | ❌ |

---

### 1.6 Parallel Execution / Multi-Agent

#### Estela (~2,550 LOC)
```
packages/core/src/commander/
├── executor.ts      (250 LOC)  - Worker execution
├── registry.ts      (250 LOC)  - Worker registry
├── tool-wrapper.ts  (250 LOC)  - Workers as tools
└── parallel/
    ├── scheduler.ts (407 LOC)  - DAG scheduling
    ├── batch.ts     (276 LOC)  - Batch execution
    ├── conflict.ts  (265 LOC)  - Conflict detection
    └── activity.ts  (245 LOC)  - Activity multiplexing
```

**Key Pattern: DAG-based task scheduling with conflict detection**
```typescript
// Workers exposed as delegate_* tools
const CODER_WORKER = { name: 'coder', tools: ['read', 'write', 'bash'] };
const TESTER_WORKER = { name: 'tester', tools: ['bash', 'read', 'grep'] };

// DAG scheduler with dependencies
class TaskScheduler {
  schedule(tasks: Task[]): ExecutionPlan {
    this.validateDAG();  // No cycles
    return this.topologicalSort();
  }
}

// Conflict detection for parallel execution
class ConflictDetector {
  partition(tasks: Task[]): { parallel: Task[], sequential: Task[] } {
    // Reader-writer lock semantics
    // Same file → sequential
    // Different files → parallel
  }
}
```

#### OpenCode (~175 LOC)
```
packages/opencode/src/tool/
└── batch.ts         (175 LOC)  - Basic batching
```

**Key Pattern: Simple batch execution**
```typescript
export const BatchTool = Tool.define("batch", {
  parameters: z.object({
    items: z.array(z.object({ tool: z.string(), args: z.any() }))
  }),
  execute: async (params, ctx) => {
    return Promise.all(params.items.map(item => callTool(item.tool, item.args)));
  }
});
```

#### Aider (~0 LOC)
**No parallel execution** - single-threaded Python.

#### Goose (~988 LOC)
```
crates/goose/src/
└── scheduler.rs     (988 LOC)  - Task scheduling
```

**Key Pattern: Tokio-based async scheduling**
```rust
// Session types include SubAgent
pub enum SessionType {
    User,
    Scheduled,
    SubAgent,  // Child agents
    Hidden,
    Terminal,
}
```

#### Gemini CLI (~0 LOC dedicated)
**No parallel execution** - sequential tool calls.

### Comparison Table: Parallel Execution

| Feature | Estela | OpenCode | Aider | Goose | Gemini CLI |
|---------|--------|----------|-------|-------|------------|
| Parallel LOC | 2,550 | 175 | 0 | 988 | 0 |
| DAG scheduling | ✅ | ❌ | ❌ | ❌ | ❌ |
| Conflict detection | ✅ | ❌ | ❌ | ❌ | ❌ |
| Workers as tools | ✅ | ❌ | ❌ | ❌ | ❌ |
| Activity multiplexing | ✅ | ❌ | ❌ | ❌ | ❌ |
| Batch operations | ✅ | ✅ | ❌ | ❌ | ❌ |
| Sub-agent spawning | ✅ | ❌ | ❌ | ✅ | ❌ |

---

## 2. Shell Execution Comparison

### Process Group Handling

#### Estela
```typescript
// packages/core/src/tools/bash.ts
// Uses platform abstraction for process groups
const result = await platform.shell.execute(command, {
  cwd: workdir,
  timeout,
  signal: controller.signal,
});

// Handles PTY for interactive commands
if (command.includes('vim') || command.includes('ssh')) {
  return platform.shell.executePty(command, options);
}
```

#### OpenCode
```typescript
// No explicit process group handling
// Uses Bun's spawn or $ shell API
const result = await spawn("bash", ["-c", params.command], { cwd, timeout });
```

#### Gemini CLI
```typescript
// packages/core/src/tools/shell.ts (lines 173-180)
// Wraps command to capture process group
const commandToExecute = (() => {
  let command = strippedCommand.trim();
  if (!command.endsWith('&')) command += ';';
  return `{ ${command} }; __code=$?; pgrep -g 0 >${tempFilePath} 2>&1; exit $__code;`;
})();

// Kill entire process group (lines 388-397)
if (isWindows) {
  cpSpawn('taskkill', ['/pid', child.pid.toString(), '/f', '/t']);
} else {
  process.kill(-child.pid, 'SIGTERM');  // Negative PID = process group
  await sleep(SIGKILL_TIMEOUT_MS);
  if (!exited) {
    process.kill(-child.pid, 'SIGKILL');
  }
}
```

#### Goose
```rust
// MCP developer server handles shell execution
// Uses Rust's std::process with process groups
```

### Comparison: Shell Execution Features

| Feature | Estela | OpenCode | Aider | Goose | Gemini CLI |
|---------|--------|----------|-------|-------|------------|
| Process groups | ✅ | ❌ | ❌ | ✅ | ✅ |
| PTY support | ✅ | ❌ | ❌ | ❌ | ❌ |
| SIGKILL escalation | ✅ | ❌ | ❌ | ✅ | ✅ |
| Inactivity timeout | ✅ | ❌ | ❌ | ❌ | ✅ |
| Background PID tracking | ❌ | ❌ | ❌ | ❌ | ✅ pgrep |
| Binary output detection | ✅ | ✅ | ❌ | ❌ | ✅ |
| Tree-sitter parsing | ❌ | ✅ | ❌ | ❌ | ❌ |

---

## 3. Provider Abstraction

### Lines of Code

| Project | Provider LOC | Providers Supported |
|---------|--------------|---------------------|
| Estela | ~500 | 7 (Anthropic, OpenAI, OpenRouter, Google, GLM, Kimi, Copilot) |
| OpenCode | ~2,000 | 20+ (via AI SDK) |
| Aider | ~1,300 | 10+ (via litellm) |
| Goose | ~15,000 | 30+ (native implementations) |
| Gemini CLI | ~500 | 1 (Gemini only) |

### Key Patterns

#### Estela
```typescript
// Factory pattern with provider registration
export async function createClient(provider: LLMProvider): Promise<LLMClient> {
  const auth = await resolveAuth(provider);
  switch (provider) {
    case 'anthropic': return createAnthropicClient(auth);
    case 'openai': return createOpenAIClient(auth);
    // ...
  }
}
```

#### OpenCode
```typescript
// Dynamic provider loading via AI SDK
const BUNDLED_PROVIDERS = {
  "@ai-sdk/anthropic": createAnthropic,
  "@ai-sdk/openai": createOpenAI,
  "@ai-sdk/google": createGoogleGenerativeAI,
  "@ai-sdk/amazon-bedrock": createAmazonBedrock,
  "@ai-sdk/azure": createAzure,
  "@openrouter/ai-sdk-provider": createOpenRouter,
  // ... 10+ more
}

// Provider-specific message transforms
function normalizeMessages(msgs, model) {
  if (model.api.npm === "@ai-sdk/anthropic") {
    // Filter empty content
  }
  if (model.api.id.includes("claude")) {
    // Sanitize tool call IDs (alphanumeric only)
  }
  if (model.providerID === "mistral") {
    // Tool IDs must be 9 chars
  }
}
```

#### Aider
```python
# Uses litellm for multi-provider support
from litellm import completion

# Custom model settings in models.py
MODEL_SETTINGS = {
    "gpt-4": { "max_tokens": 8192 },
    "claude-3-opus": { "max_tokens": 4096 },
}
```

#### Goose
```rust
// Native trait implementation for each provider
pub trait Provider: Send + Sync {
    async fn complete(
        &self,
        session_id: &str,
        system_prompt: &str,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<(Message, Usage)>;
}

// 30+ implementations: Anthropic, OpenAI, Azure, Bedrock, Vertex, etc.
```

---

## 4. Configuration & Settings

### Lines of Code

| Project | Config LOC | Key Features |
|---------|------------|--------------|
| Estela | 2,082 | Zod validation, reactive updates, credential store |
| OpenCode | ~1,500 | YAML config, feature flags, env vars |
| Aider | ~945 | CLI args, .aider files, model settings |
| Goose | 2,977 | YAML, migration, extension config |
| Gemini CLI | ~1,000 | Policy engine, hooks, MCP config |

### Validation Patterns

#### Estela (Zod)
```typescript
export const SettingsSchema = z.object({
  provider: ProviderSettingsSchema,
  agent: AgentSettingsSchema,
  permissions: PermissionSettingsSchema,
  // ...
});

// Runtime validation
const result = SettingsSchema.safeParse(settings);
if (!result.success) {
  throw new SettingsValidationError(result.error);
}
```

#### OpenCode (Zod + YAML)
```typescript
// Uses Zod for schema validation with YAML configs
const Agent = z.object({
  name: z.string(),
  mode: z.enum(["subagent", "primary", "all"]),
  permission: PermissionNext.Ruleset,
  // ...
});
```

#### Goose (Rust types)
```rust
// Strong typing via Rust's type system
pub struct PermissionConfig {
    pub always_allow: Vec<String>,
    pub ask_before: Vec<String>,
    pub never_allow: Vec<String>,
}
```

---

## 5. Test Coverage & Quality

| Project | Test LOC | Test/Impl Ratio | CI/CD |
|---------|----------|-----------------|-------|
| Estela | ~2,000 | ~10% | GitHub Actions |
| OpenCode | ~20,000 | ~200% | GitHub Actions |
| Aider | ~15,000 | ~50% | GitHub Actions |
| Goose | ~5,000 | ~30% | GitHub Actions |
| Gemini CLI | ~35,000 | ~60% | GitHub Actions |

---

## 6. Unique Strengths by Project

### Estela
1. **Cognitive memory system** - Only agent with episodic/semantic/procedural memory
2. **DAG-based parallel execution** - Conflict detection & scheduling
3. **Workers-as-tools pattern** - Hierarchical delegation
4. **Full validation pipeline** - 6 validators in sequence
5. **Platform abstraction** - Same code runs in Node.js & Tauri

### OpenCode
1. **Tree-sitter bash parsing** - Static analysis of shell commands
2. **LSP integration** - Real-time diagnostics
3. **Doom loop detection** - Prevents infinite tool loops
4. **20+ LLM providers** - Best multi-provider support
5. **Plugin system** - Extensible architecture

### Aider
1. **15+ edit formats** - Best format flexibility
2. **PageRank with personalization** - Smart file ranking
3. **Git-native** - Automatic commits, easy rollback
4. **100+ languages** - Broadest language support
5. **Binary search for tokens** - Optimal context usage

### Goose
1. **MCP-first architecture** - Best protocol support
2. **LLM-based permission detection** - Smart readonly inference
3. **Rust performance** - Lowest resource usage
4. **30+ native providers** - No SDK dependencies
5. **Session types** - User/Scheduled/SubAgent/Hidden

### Gemini CLI
1. **ToolBuilder/ToolInvocation separation** - Clean validation
2. **Message bus for policy** - Decoupled confirmation
3. **Process group handling** - Best shell safety
4. **Hook system** - Before/after agent events
5. **Root command extraction** - Fine-grained permissions

---

## 7. What Estela Should Adopt

### From OpenCode
- [ ] Doom loop detection (3x identical tool calls)
- [ ] Tree-sitter bash parsing for command analysis
- [ ] LSP integration for diagnostics

### From Aider
- [ ] Binary search for token budget optimization
- [ ] Personalization weights in PageRank
- [ ] Multiple edit format support

### From Goose
- [ ] LLM-based read-only tool detection
- [ ] Context-aware permission caching with expiry
- [ ] Session type differentiation (User/SubAgent/Hidden)

### From Gemini CLI
- [ ] ToolBuilder/ToolInvocation pattern for validation
- [ ] Background PID tracking via pgrep
- [ ] Hook system for extensibility

---

## 8. Architecture Diagrams

### Estela Architecture
```
┌────────────────────────────────────────────────────────────┐
│                     LLM Providers (7)                      │
│         Anthropic, OpenAI, OpenRouter, Google, etc.        │
└────────────────────────────┬───────────────────────────────┘
                             │
┌────────────────────────────▼───────────────────────────────┐
│                    Agent Executor                          │
│    Plans → Tools → Validates → Recovers → Events          │
└────────────────────────────┬───────────────────────────────┘
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
   ┌────▼────┐          ┌────▼────┐         ┌────▼─────┐
   │Commander │          │  Tools  │         │Validator │
   │(Workers) │          │Registry │         │Pipeline  │
   └────┬────┘          └────┬────┘         └────┬─────┘
        │                    │                    │
   ┌────▼────────────────────▼────────────────────▼─────┐
   │            Code Understanding (Codebase)            │
   │   Indexer → Symbols → Graph → PageRank → RepoMap   │
   └─────────────────────────┬───────────────────────────┘
                             │
   ┌─────────────────────────▼───────────────────────────┐
   │              Persistent State & Memory              │
   │   Config │ Memory (3-tier) │ Session │ Git │ Diff   │
   └─────────────────────────────────────────────────────┘
```

### OpenCode Architecture
```
┌─────────────────────────────────────────┐
│           AI SDK (20+ providers)        │
└─────────────────────┬───────────────────┘
                      │
┌─────────────────────▼───────────────────┐
│      Session Processor (Doom Loop)      │
│   Stream → Tools → Compact → Events     │
└─────────────────────┬───────────────────┘
                      │
      ┌───────────────┼───────────────┐
      │               │               │
 ┌────▼────┐    ┌────▼────┐    ┌────▼────┐
 │  Tools  │    │   LSP   │    │  Patch  │
 │ (batch) │    │Diagnose │    │ (diffs) │
 └─────────┘    └─────────┘    └─────────┘
```

### Aider Architecture
```
┌─────────────────────────────────────────┐
│            litellm (10+ models)         │
└─────────────────────┬───────────────────┘
                      │
┌─────────────────────▼───────────────────┐
│         Coder (15+ edit formats)        │
│   get_edits → apply_edits → validate    │
└─────────────────────┬───────────────────┘
                      │
      ┌───────────────┼───────────────┐
      │               │               │
 ┌────▼────┐    ┌────▼────┐    ┌────▼────┐
 │ RepoMap │    │   Git   │    │Commands │
 │PageRank │    │ Commits │    │ Dispatch│
 └─────────┘    └─────────┘    └─────────┘
```

---

## 9. Conclusion

Estela achieves **comprehensive feature parity** with SOTA agents while introducing unique capabilities:

| Category | Estela Rank | Key Differentiator |
|----------|-------------|-------------------|
| Agent Loop | 2nd | Planning + recovery (OpenCode has doom loop) |
| Tools | 3rd | File locking (Aider has 15+ formats) |
| Permissions | 2nd | Risk-based (Goose has LLM detection) |
| Codebase | 1st | Full PageRank + circular dep detection |
| Memory | 1st | Only agent with 3-tier cognitive memory |
| Parallel | 1st | Only agent with DAG + conflict detection |
| Providers | 3rd | 7 providers (OpenCode has 20+) |

**Total Implementation: ~23,000 LOC** (competitive with OpenCode at ~10K, smaller than Aider at ~35K)

The comparison reveals that while each agent excels in specific areas, Estela's unique combination of memory system, parallel execution, and comprehensive validation pipeline positions it as a next-generation AI coding assistant.
