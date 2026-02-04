# OpenCode Core CLI Analysis

This document provides a comprehensive analysis of the OpenCode CLI codebase, covering the agent architecture, session management, and tools registry.

---

## Table of Contents

1. [Agent Architecture](#agent-architecture)
2. [Session Management](#session-management)
3. [Tools Registry](#tools-registry)

---

## Agent Architecture

Located in `/packages/opencode/src/agent/`

### Core File: `agent.ts`

The agent system defines different operational modes and permission configurations.

### Agent Info Schema

```typescript
export const Info = z.object({
  name: z.string(),
  description: z.string().optional(),
  mode: z.enum(["subagent", "primary", "all"]),
  native: z.boolean().optional(),
  hidden: z.boolean().optional(),
  topP: z.number().optional(),
  temperature: z.number().optional(),
  color: z.string().optional(),
  permission: PermissionNext.Ruleset,
  model: z.object({
    modelID: z.string(),
    providerID: z.string(),
  }).optional(),
  prompt: z.string().optional(),
  options: z.record(z.string(), z.any()),
  steps: z.number().int().positive().optional(),
})
```

### Built-in Agents

| Agent | Mode | Description | Key Permissions |
|-------|------|-------------|-----------------|
| `build` | primary | Default agent for executing tools | `question: allow`, `plan_enter: allow` |
| `plan` | primary | Plan mode - disallows edit tools | Edits only to `.opencode/plans/*.md` |
| `general` | subagent | Multi-step task execution | `todoread/todowrite: deny` |
| `explore` | subagent | Fast codebase exploration | Read-only tools only |
| `compaction` | primary (hidden) | Context summarization | All tools denied |
| `title` | primary (hidden) | Title generation | All tools denied |
| `summary` | primary (hidden) | Summary generation | All tools denied |

### Permission System

Default permissions:
- `*`: allow
- `doom_loop`: ask
- `external_directory.*`: ask (except truncation directory)
- `question`: deny (enabled per-agent)
- `plan_enter/plan_exit`: deny (enabled per-agent)
- `read.*.env*`: ask (security)

### Agent Configuration

Agents can be:
1. **Native** - Built into OpenCode
2. **Custom** - Defined in config via `agent` key
3. **Disabled** - Set `disable: true` in config

Custom agent config options:
- `model` - Override model
- `prompt` - Custom system prompt
- `description` - Agent description
- `temperature/topP` - Model parameters
- `mode` - "subagent", "primary", or "all"
- `color` - UI color
- `hidden` - Hide from UI
- `steps` - Max execution steps
- `permission` - Permission overrides

### Agent Generation

The `Agent.generate()` function uses AI to create new agent configurations:
- Takes a description as input
- Returns: `{ identifier, whenToUse, systemPrompt }`

### Key Design Decisions

1. **Mode-based filtering**: Agents are filtered by mode for different contexts
2. **Permission merging**: User config merges with defaults, not replaces
3. **Prompt loading**: Prompts loaded from `.txt` files (compile-time bundling)
4. **Instance state**: Agents are cached per-project instance

---

## Session Management

Located in `/packages/opencode/src/session/`

### File Overview

| File | Purpose |
|------|---------|
| `index.ts` | Session CRUD, message management, sharing |
| `message.ts` | Legacy message types |
| `message-v2.ts` | Modern message/part types with discriminated unions |
| `prompt.ts` | Main execution loop, tool resolution |
| `processor.ts` | Stream processing, doom loop detection |
| `compaction.ts` | Context overflow handling, message pruning |
| `instruction.ts` | CLAUDE.md/AGENTS.md instruction loading |
| `system.ts` | Provider-specific system prompts |
| `llm.ts` | LLM streaming wrapper |
| `summary.ts` | Session/message summarization |
| `todo.ts` | Todo list state management |
| `retry.ts` | Error retry logic with backoff |
| `revert.ts` | Undo/redo functionality |
| `status.ts` | Session busy/idle status |

### Session Schema (`index.ts`)

```typescript
export const Info = z.object({
  id: Identifier.schema("session"),
  slug: z.string(),
  projectID: z.string(),
  directory: z.string(),
  parentID: Identifier.schema("session").optional(), // For subagent sessions
  summary: z.object({
    additions: z.number(),
    deletions: z.number(),
    files: z.number(),
    diffs: Snapshot.FileDiff.array().optional(),
  }).optional(),
  share: z.object({ url: z.string() }).optional(),
  title: z.string(),
  version: z.string(),
  time: z.object({
    created: z.number(),
    updated: z.number(),
    compacting: z.number().optional(),
    archived: z.number().optional(),
  }),
  permission: PermissionNext.Ruleset.optional(),
  revert: z.object({
    messageID: z.string(),
    partID: z.string().optional(),
    snapshot: z.string().optional(),
    diff: z.string().optional(),
  }).optional(),
})
```

### Session Events

```typescript
export const Event = {
  Created: BusEvent.define("session.created", ...),
  Updated: BusEvent.define("session.updated", ...),
  Deleted: BusEvent.define("session.deleted", ...),
  Diff: BusEvent.define("session.diff", ...),
  Error: BusEvent.define("session.error", ...),
}
```

### Session Operations

| Operation | Description |
|-----------|-------------|
| `create()` | Create new session with optional parent (for subagents) |
| `fork()` | Clone session up to a specific message |
| `get()` | Fetch session by ID |
| `update()` | Modify session with editor callback |
| `remove()` | Delete session and all children |
| `messages()` | Get all messages with parts |
| `share()` / `unshare()` | Session sharing |
| `touch()` | Update `time.updated` |

### Message Model (`message-v2.ts`)

#### Part Types

The message system uses discriminated unions for type-safe parts:

| Part Type | Purpose |
|-----------|---------|
| `TextPart` | LLM text output with streaming support |
| `ReasoningPart` | Model reasoning/thinking |
| `ToolPart` | Tool invocation with state machine |
| `FilePart` | File attachments (images, PDFs) |
| `StepStartPart` | Marks step beginning with snapshot |
| `StepFinishPart` | Step completion with tokens/cost |
| `SnapshotPart` | Git snapshot reference |
| `PatchPart` | File changes tracking |
| `AgentPart` | Agent invocation marker |
| `SubtaskPart` | Subagent task definition |
| `RetryPart` | API retry tracking |
| `CompactionPart` | Compaction marker |

#### Tool State Machine

```typescript
export const ToolState = z.discriminatedUnion("status", [
  ToolStatePending,   // { status: "pending", input, raw }
  ToolStateRunning,   // { status: "running", input, title?, metadata?, time.start }
  ToolStateCompleted, // { status: "completed", input, output, title, metadata, time, attachments? }
  ToolStateError,     // { status: "error", input, error, metadata?, time }
])
```

#### Message Types

**User Message:**
```typescript
export const User = Base.extend({
  role: z.literal("user"),
  time: z.object({ created: z.number() }),
  summary: z.object({ title?, body?, diffs }).optional(),
  agent: z.string(),
  model: z.object({ providerID, modelID }),
  system: z.string().optional(),
  tools: z.record(z.string(), z.boolean()).optional(),
  variant: z.string().optional(),
})
```

**Assistant Message:**
```typescript
export const Assistant = Base.extend({
  role: z.literal("assistant"),
  time: z.object({ created, completed? }),
  error: z.discriminatedUnion("name", [...]).optional(),
  parentID: z.string(),
  modelID: z.string(),
  providerID: z.string(),
  agent: z.string(),
  path: z.object({ cwd, root }),
  summary: z.boolean().optional(),
  cost: z.number(),
  tokens: z.object({ input, output, reasoning, cache: { read, write } }),
  finish: z.string().optional(),
})
```

### Execution Loop (`prompt.ts`)

The main execution loop in `SessionPrompt.loop()`:

```
1. Start loop (create abort controller)
2. Filter compacted messages
3. Find last user/assistant messages
4. Check if done (finish reason not tool-calls)
5. Handle pending subtasks
6. Handle pending compaction
7. Check context overflow -> trigger compaction
8. Insert reminders (plan mode hints)
9. Create processor & resolve tools
10. Process stream
11. Handle result (continue/stop/compact)
12. Prune old tool outputs
```

#### Key Features:

1. **Subtask Handling**: Executes pending subtasks from SubtaskPart
2. **Compaction Detection**: Auto-triggers when tokens exceed model limits
3. **Doom Loop Detection**: Prevents repeated identical tool calls
4. **Plan Mode Reminders**: Injects planning instructions
5. **Title Generation**: Auto-generates session title on first turn

### Stream Processing (`processor.ts`)

The `SessionProcessor` handles LLM stream events:

```typescript
const eventHandlers = {
  "start": () => setStatus("busy"),
  "reasoning-start/delta/end": () => updateReasoningPart(),
  "tool-input-start/delta/end": () => createToolPart(pending),
  "tool-call": () => runTool() + doomLoopCheck(),
  "tool-result": () => updateToolPart(completed),
  "tool-error": () => updateToolPart(error),
  "start-step": () => takeSnapshot(),
  "finish-step": () => recordUsage() + checkCompaction(),
  "text-start/delta/end": () => updateTextPart(),
  "error": () => handleError() + maybeRetry(),
}
```

#### Doom Loop Detection

```typescript
const DOOM_LOOP_THRESHOLD = 3
// If last 3 tool calls are identical (same tool + same args)
// -> Ask user permission to continue
```

### Compaction (`compaction.ts`)

Handles context window overflow:

1. **Detection**: When `input + cache.read + output > usable_context`
2. **Prune**: Clears old tool outputs (keeps last 40k tokens)
3. **Summarize**: Creates compaction message with summary

```typescript
export const PRUNE_MINIMUM = 20_000   // Min tokens to prune
export const PRUNE_PROTECT = 40_000   // Keep last N tokens
```

Protected tools from pruning: `["skill"]`

### Instruction Loading (`instruction.ts`)

Loads project instructions from:
1. `AGENTS.md` / `CLAUDE.md` / `CONTEXT.md` in project
2. Global `~/.claude/CLAUDE.md`
3. Config `instructions` array (files or URLs)

Features:
- **Directory-specific**: Loads instructions from subdirectories when reading files
- **Claim tracking**: Prevents loading same instruction twice per message
- **URL support**: Fetches remote instructions with timeout

### System Prompts (`system.ts`)

Provider-specific prompts:
- GPT-5: Codex prompt
- GPT-4/o1/o3: Beast prompt
- Gemini: Gemini prompt
- Claude: Anthropic prompt
- Others: Qwen prompt (Anthropic without todos)

Environment info injected:
```
You are powered by ${model.id}
Working directory: ${Instance.directory}
Is directory a git repo: yes/no
Platform: ${process.platform}
Today's date: ${date}
```

### LLM Streaming (`llm.ts`)

Wraps AI SDK `streamText()` with:
- Provider-specific options
- System prompt assembly
- Tool resolution
- Reasoning extraction middleware
- LiteLLM proxy compatibility (dummy tool)

### Session Revert (`revert.ts`)

Undo/redo functionality:
1. `revert()`: Rolls back to message/part, reverts file snapshots
2. `unrevert()`: Restores original state
3. `cleanup()`: Removes reverted messages on next prompt

---

## Tools Registry

Located in `/packages/opencode/src/tool/`

### Core Files

| File | Purpose |
|------|---------|
| `tool.ts` | Tool definition interface |
| `registry.ts` | Tool registration and resolution |
| `truncation.ts` | Output truncation with file fallback |
| `external-directory.ts` | Permission check for paths outside project |

### Tool Interface (`tool.ts`)

```typescript
export interface Info<Parameters extends z.ZodType, M extends Metadata> {
  id: string
  init: (ctx?: InitContext) => Promise<{
    description: string
    parameters: Parameters
    execute(args: z.infer<Parameters>, ctx: Context): Promise<{
      title: string
      metadata: M
      output: string
      attachments?: MessageV2.FilePart[]
    }>
    formatValidationError?(error: z.ZodError): string
  }>
}
```

### Tool Context

```typescript
export type Context = {
  sessionID: string
  messageID: string
  agent: string
  abort: AbortSignal
  callID?: string
  extra?: { [key: string]: any }
  messages: MessageV2.WithParts[]
  metadata(input: { title?: string; metadata?: any }): void
  ask(input: Omit<PermissionNext.Request, "id" | "sessionID" | "tool">): Promise<void>
}
```

### Tool Registry (`registry.ts`)

Built-in tools:
```typescript
const tools = [
  InvalidTool,
  QuestionTool,      // Only in app/cli/desktop clients
  BashTool,
  ReadTool,
  GlobTool,
  GrepTool,
  EditTool,
  WriteTool,
  TaskTool,
  WebFetchTool,
  TodoWriteTool,
  TodoReadTool,
  WebSearchTool,     // Only for opencode provider or OPENCODE_ENABLE_EXA
  CodeSearchTool,    // Only for opencode provider or OPENCODE_ENABLE_EXA
  SkillTool,
  ApplyPatchTool,    // Only for GPT models (not GPT-4)
  LspTool,           // Experimental flag
  BatchTool,         // Experimental config
  PlanExitTool,      // Experimental plan mode + CLI
  PlanEnterTool,     // Experimental plan mode + CLI
  ...customTools,    // From config/plugins
]
```

Custom tool loading:
1. Scans `{tool,tools}/*.{js,ts}` in config directories
2. Loads plugins with `tool` exports

### All Tools Reference

#### 1. Read Tool (`read.ts`)

Reads file contents with line numbers.

**Parameters:**
```typescript
{
  filePath: string,     // Path to file
  offset?: number,      // Line number to start (0-based)
  limit?: number,       // Lines to read (default: 2000)
}
```

**Features:**
- Line numbering: `00001| content`
- Binary file detection
- Image/PDF handling (returns as attachment)
- Directory instruction loading
- Max 2000 chars per line, 50KB total

#### 2. Write Tool (`write.ts`)

Creates or overwrites files.

**Parameters:**
```typescript
{
  content: string,      // Content to write
  filePath: string,     // Absolute path
}
```

**Features:**
- LSP integration for diagnostics
- File event publishing
- Diff generation for approval

#### 3. Edit Tool (`edit.ts`)

Performs search-and-replace edits with fuzzy matching.

**Parameters:**
```typescript
{
  filePath: string,
  oldString: string,
  newString: string,
  replaceAll?: boolean,  // Default: false
}
```

**Fuzzy Matching Strategies:**
1. `SimpleReplacer` - Exact match
2. `LineTrimmedReplacer` - Trim-based matching
3. `BlockAnchorReplacer` - First/last line anchors with similarity
4. `WhitespaceNormalizedReplacer` - Collapse whitespace
5. `IndentationFlexibleReplacer` - Ignore indentation differences
6. `EscapeNormalizedReplacer` - Handle escape sequences
7. `TrimmedBoundaryReplacer` - Trim boundaries
8. `ContextAwareReplacer` - Context-based block matching
9. `MultiOccurrenceReplacer` - All occurrences

#### 4. Bash Tool (`bash.ts`)

Executes shell commands.

**Parameters:**
```typescript
{
  command: string,
  timeout?: number,      // Default: 2 min
  workdir?: string,      // Default: Instance.directory
  description: string,   // 5-10 word description
}
```

**Features:**
- Tree-sitter parsing for permission extraction
- External directory detection from `cd`, `rm`, `cp`, etc.
- Auto-kill on timeout/abort
- Real-time output streaming

#### 5. Glob Tool (`glob.ts`)

Finds files by pattern.

**Parameters:**
```typescript
{
  pattern: string,       // Glob pattern
  path?: string,         // Search directory
}
```

**Features:**
- Uses ripgrep for speed
- Sorts by modification time
- Limit: 100 files

#### 6. Grep Tool (`grep.ts`)

Searches file contents.

**Parameters:**
```typescript
{
  pattern: string,       // Regex pattern
  path?: string,         // Search directory
  include?: string,      // File glob filter
}
```

**Features:**
- Uses ripgrep
- Sorts by modification time
- Max 2000 chars per line
- Limit: 100 matches

#### 7. List Tool (`ls.ts`)

Lists directory contents as tree.

**Parameters:**
```typescript
{
  path?: string,         // Directory path
  ignore?: string[],     // Additional ignore patterns
}
```

**Default Ignores:**
`node_modules/`, `__pycache__/`, `.git/`, `dist/`, `build/`, `target/`, etc.

#### 8. Task Tool (`task.ts`)

Spawns subagent sessions.

**Parameters:**
```typescript
{
  description: string,   // 3-5 word description
  prompt: string,        // Task prompt
  subagent_type: string, // Agent name
  session_id?: string,   // Continue existing session
  command?: string,      // Triggering command
}
```

**Features:**
- Creates child session with parent ID
- Disables todoread/todowrite in subagents
- Permission filtering based on caller agent
- Real-time tool progress streaming

#### 9. Question Tool (`question.ts`)

Asks user questions.

**Parameters:**
```typescript
{
  questions: Array<{
    question: string,
    header?: string,
    options?: Array<{ label, description }>,
  }>
}
```

**Features:**
- Multiple question types (free text, options)
- Returns formatted answers

#### 10. Todo Tools (`todo.ts`)

Session-scoped todo list.

**TodoWrite Parameters:**
```typescript
{
  todos: Array<{
    content: string,
    status: string,      // pending, in_progress, completed, cancelled
    priority: string,    // high, medium, low
    id: string,
  }>
}
```

**TodoRead:** No parameters

#### 11. WebSearch Tool (`websearch.ts`)

Web search via Exa API.

**Parameters:**
```typescript
{
  query: string,
  numResults?: number,        // Default: 8
  livecrawl?: "fallback" | "preferred",
  type?: "auto" | "fast" | "deep",
  contextMaxCharacters?: number,
}
```

#### 12. WebFetch Tool (`webfetch.ts`)

Fetches and converts web pages.

**Parameters:**
```typescript
{
  url: string,
  format?: "text" | "markdown" | "html",  // Default: markdown
  timeout?: number,                        // Max: 120s
}
```

**Features:**
- HTML to Markdown conversion (Turndown)
- Cloudflare bypass retry
- 5MB max response

#### 13. CodeSearch Tool (`codesearch.ts`)

API/library documentation search via Exa.

**Parameters:**
```typescript
{
  query: string,
  tokensNum?: number,    // 1000-50000, default: 5000
}
```

#### 14. Skill Tool (`skill.ts`)

Loads skill files with instructions.

**Parameters:**
```typescript
{
  name: string,          // Skill identifier
}
```

**Features:**
- Permission-based filtering
- Markdown parsing
- Base directory context

#### 15. Batch Tool (`batch.ts`) [Experimental]

Parallel tool execution.

**Parameters:**
```typescript
{
  tool_calls: Array<{
    tool: string,
    parameters: object,
  }>
}
```

**Features:**
- Max 25 tools per batch
- Disallowed: `batch` (no recursion)
- MCP tools not supported

#### 16. Apply Patch Tool (`apply_patch.ts`)

Unified diff format for GPT models.

**Parameters:**
```typescript
{
  patchText: string,     // Full patch text
}
```

**Patch Format:**
```
*** Begin Patch
*** Add File: path/to/new/file.txt
content here

*** Update File: path/to/existing.txt
@@ context line @@
-old line
+new line

*** Delete File: path/to/delete.txt
*** End Patch
```

**Features:**
- Add/Update/Delete/Move operations
- Chunk-based fuzzy matching
- LSP diagnostics after apply

#### 17. Plan Tools (`plan.ts`) [Experimental]

Mode switching tools.

**PlanExit:** Switches from plan to build agent
**PlanEnter:** Switches from build to plan agent

Both ask user confirmation via Question tool.

#### 18. LSP Tool (`lsp.ts`) [Experimental]

Language Server Protocol operations.

**Parameters:**
```typescript
{
  operation: "goToDefinition" | "findReferences" | "hover" |
             "documentSymbol" | "workspaceSymbol" | "goToImplementation" |
             "prepareCallHierarchy" | "incomingCalls" | "outgoingCalls",
  filePath: string,
  line: number,          // 1-based
  character: number,     // 1-based
}
```

#### 19. Invalid Tool (`invalid.ts`)

Catches malformed tool calls.

**Parameters:**
```typescript
{
  tool: string,
  error: string,
}
```

Used by AI SDK's `experimental_repairToolCall`.

#### 20. MultiEdit Tool (`multiedit.ts`)

Multiple sequential edits to one file.

**Parameters:**
```typescript
{
  filePath: string,
  edits: Array<{
    filePath: string,
    oldString: string,
    newString: string,
    replaceAll?: boolean,
  }>
}
```

### Output Truncation (`truncation.ts`)

Handles large tool outputs:

```typescript
export const MAX_LINES = 2000
export const MAX_BYTES = 50 * 1024  // 50KB
```

When exceeded:
1. Truncates to limits
2. Saves full output to `~/.opencode/tool-output/tool_<id>`
3. Returns hint to use Task tool for processing
4. Auto-cleanup after 7 days

### External Directory Check (`external-directory.ts`)

Prevents operations outside project without permission:

```typescript
async function assertExternalDirectory(ctx, target, options) {
  if (Instance.containsPath(target)) return  // In project

  await ctx.ask({
    permission: "external_directory",
    patterns: [glob],
    always: [glob],
    metadata: { filepath, parentDir },
  })
}
```

---

## Key Patterns & Design Decisions

### 1. Discriminated Unions

Used extensively for type safety:
- Message types: `z.discriminatedUnion("role", [User, Assistant])`
- Part types: `z.discriminatedUnion("type", [TextPart, ToolPart, ...])`
- Tool state: `z.discriminatedUnion("status", [Pending, Running, ...])`

### 2. Event Bus

All state changes publish events:
- `session.created/updated/deleted`
- `message.updated/removed`
- `message.part.updated/removed`
- `todo.updated`
- `session.status`

### 3. Instance State

Per-project state management:
```typescript
const state = Instance.state(async () => {
  // Initialize state
  return data
}, async (current) => {
  // Cleanup on instance change
})
```

### 4. Deferred Cleanup

Using `using` syntax for guaranteed cleanup:
```typescript
using _ = defer(() => cleanup())
```

### 5. Permission System

Fine-grained permissions:
- Per-tool (`bash`, `edit`, `read`)
- Per-pattern (`edit.*.env`)
- Per-action (`allow`, `deny`, `ask`)
- Inheritance (agent -> session -> config)

### 6. Snapshot System

Git-based state tracking:
- `Snapshot.track()` - Create snapshot
- `Snapshot.patch()` - Get changes since snapshot
- `Snapshot.revert()` - Rollback changes
- `Snapshot.restore()` - Restore specific snapshot

### 7. Provider Transforms

Model-specific adaptations:
- Schema transformations
- Option merging
- Temperature/topP defaults
- Message formatting

---

## File Paths Reference

```
packages/opencode/src/
├── agent/
│   └── agent.ts              # Agent definitions and management
├── session/
│   ├── index.ts              # Session CRUD
│   ├── message.ts            # Legacy message types
│   ├── message-v2.ts         # Modern message/part types
│   ├── prompt.ts             # Execution loop
│   ├── processor.ts          # Stream processing
│   ├── compaction.ts         # Context management
│   ├── instruction.ts        # Instruction loading
│   ├── system.ts             # System prompts
│   ├── llm.ts                # LLM wrapper
│   ├── summary.ts            # Summarization
│   ├── todo.ts               # Todo state
│   ├── retry.ts              # Retry logic
│   ├── revert.ts             # Undo/redo
│   └── status.ts             # Status tracking
└── tool/
    ├── tool.ts               # Tool interface
    ├── registry.ts           # Tool registration
    ├── truncation.ts         # Output truncation
    ├── external-directory.ts # Path permission check
    ├── read.ts               # File reading
    ├── write.ts              # File writing
    ├── edit.ts               # File editing
    ├── bash.ts               # Shell execution
    ├── glob.ts               # File finding
    ├── grep.ts               # Content search
    ├── ls.ts                 # Directory listing
    ├── task.ts               # Subagent spawning
    ├── question.ts           # User questions
    ├── todo.ts               # Todo management
    ├── websearch.ts          # Web search
    ├── webfetch.ts           # Web fetching
    ├── codesearch.ts         # Code/docs search
    ├── skill.ts              # Skill loading
    ├── batch.ts              # Parallel tools
    ├── apply_patch.ts        # Unified diff
    ├── plan.ts               # Plan mode tools
    ├── lsp.ts                # LSP operations
    ├── invalid.ts            # Error handling
    └── multiedit.ts          # Multiple edits
```
