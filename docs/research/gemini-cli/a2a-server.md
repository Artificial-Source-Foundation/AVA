# Gemini CLI A2A Server Analysis

> Agent-to-Agent (A2A) Protocol Implementation in Gemini CLI

**Analysis Date:** 2026-02-04
**Package Version:** 0.28.0-nightly
**Source:** `/docs/reference-code/gemini-cli/packages/a2a-server/`

---

## Overview

The Gemini CLI A2A Server is a standalone HTTP server that exposes the Gemini CLI's code generation capabilities via the **A2A (Agent-to-Agent) Protocol**. This enables other AI agents and applications to interact with Gemini as a coding assistant through a standardized REST/streaming API.

### Key Dependencies

| Package | Purpose |
|---------|---------|
| `@a2a-js/sdk` | A2A Protocol SDK (v0.3.8) |
| `@google/gemini-cli-core` | Core Gemini CLI logic |
| `express` | HTTP server framework (v5.1.0) |
| `@google-cloud/storage` | GCS persistence for tasks |
| `winston` | Structured logging |
| `tar` + `fs-extra` | Workspace archiving |

### Architecture Summary

```
+-------------------+     +-------------------+     +-------------------+
|   External Agent  | --> |   A2A HTTP API    | --> |  CoderAgentExecutor |
|   (Client)        |     |   (Express)       |     |  (Agent Loop)     |
+-------------------+     +-------------------+     +-------------------+
                                   |                         |
                                   v                         v
                          +-------------------+     +-------------------+
                          |   TaskStore       |     |   Task            |
                          |   (GCS/InMemory)  |     |   (Session State) |
                          +-------------------+     +-------------------+
                                                            |
                                                            v
                                                   +-------------------+
                                                   |  GeminiClient     |
                                                   |  (LLM + Tools)    |
                                                   +-------------------+
```

---

## File-by-File Breakdown

### Entry Points

#### `src/index.ts`
Simple re-exports exposing the public API:
- `CoderAgentExecutor` from executor
- Express app setup from `http/app`
- Type definitions from `types`

#### `src/http/server.ts`
CLI entry point that:
- Checks if running as main module
- Sets up uncaught exception handler
- Calls `main()` from app.ts

---

### Core Agent Logic

#### `src/agent/executor.ts` (615 lines)

The **CoderAgentExecutor** is the heart of the A2A server. It implements the `AgentExecutor` interface from `@a2a-js/sdk/server`.

**Key Classes:**

1. **TaskWrapper** - Bridges internal `Task` with SDK's `SDKTask`:
   ```typescript
   class TaskWrapper {
     task: Task;
     agentSettings: AgentSettings;

     toSDKTask(): SDKTask {
       // Serializes task state for persistence
       // Includes persisted state metadata
     }
   }
   ```

2. **CoderAgentExecutor** - Implements `AgentExecutor`:
   ```typescript
   class CoderAgentExecutor implements AgentExecutor {
     private tasks: Map<string, TaskWrapper>;
     private executingTasks: Set<string>;  // Prevents duplicate execution

     async execute(requestContext, eventBus): Promise<void>;
     async createTask(taskId, contextId, agentSettings?, eventBus?): Promise<TaskWrapper>;
     async reconstruct(sdkTask, eventBus?): Promise<TaskWrapper>;  // Hydrate from store
     async cancelTask(taskId, eventBus): Promise<void>;
   }
   ```

**Execution Flow:**

1. **Task Lookup/Creation**:
   - Check in-memory cache (`this.tasks`)
   - If not found, try to reconstruct from `TaskStore`
   - If new, create fresh task via `createTask()`

2. **Socket Close Handling**:
   - Uses `AsyncLocalStorage` to access request
   - Sets up abort on client disconnect
   - Prevents orphaned executions

3. **Agent Loop** (main execution pattern):
   ```typescript
   while (agentTurnActive) {
     // 1. Process agent events from LLM stream
     for await (const event of agentEvents) {
       if (event.type === ToolCallRequest) {
         toolCallRequests.push(event.value);
       } else {
         await task.acceptAgentMessage(event);
       }
     }

     // 2. Schedule tool calls as batch
     if (toolCallRequests.length > 0) {
       await task.scheduleToolCalls(toolCallRequests, abortSignal);
     }

     // 3. Wait for all tools to complete
     await task.waitForPendingTools();

     // 4. Get completed tools and decide next step
     const completedTools = task.getAndClearCompletedTools();

     if (completedTools.length > 0) {
       if (completedTools.every(t => t.status === 'cancelled')) {
         // All cancelled - end turn
         agentTurnActive = false;
       } else {
         // Send results back to LLM
         agentEvents = task.sendCompletedToolsToLlm(completedTools, abortSignal);
       }
     } else {
       agentTurnActive = false;
     }
   }
   ```

4. **State Persistence**:
   - Saves task state after every execution
   - Handles cancellation gracefully
   - Sets `input-required` when turn ends

---

#### `src/agent/task.ts` (1078 lines)

The **Task** class manages a single agent session's state, tool scheduling, and LLM communication.

**Key Properties:**
```typescript
class Task {
  id: string;
  contextId: string;
  scheduler: CoreToolScheduler;
  config: Config;
  geminiClient: GeminiClient;
  pendingToolConfirmationDetails: Map<string, ToolCallConfirmationDetails>;
  taskState: TaskState;
  eventBus?: ExecutionEventBus;
  completedToolCalls: CompletedToolCall[];
  autoExecute: boolean;
  promptCount: number;
  currentPromptId: string | undefined;
}
```

**Tool Scheduling Pattern:**

```typescript
async scheduleToolCalls(requests: ToolCallRequestInfo[], abortSignal: AbortSignal) {
  // 1. Filter restorable tools (file edits)
  const restorableToolCalls = requests.filter(r => EDIT_TOOL_NAMES.has(r.name));

  // 2. Create checkpoints if checkpointing enabled
  if (restorableToolCalls.length > 0 && this.config.getCheckpointingEnabled()) {
    const gitService = await this.config.getGitService();
    const { checkpointsToWrite, toolCallToCheckpointMap } =
      await processRestorableToolCalls(restorableToolCalls, gitService, this.geminiClient);
    // Write checkpoint files
    // Attach checkpoint references to requests
  }

  // 3. Pre-compute proposed content for replace operations
  const updatedRequests = await Promise.all(requests.map(async (request) => {
    if (request.name === 'replace' && !request.args['newContent']) {
      const newContent = await this.getProposedContent(
        request.args['file_path'],
        request.args['old_string'],
        request.args['new_string']
      );
      return { ...request, args: { ...request.args, newContent } };
    }
    return request;
  }));

  // 4. Schedule via CoreToolScheduler
  await this.scheduler.schedule(updatedRequests, abortSignal);
}
```

**Tool Waiting Mechanism:**
```typescript
// Promise-based tool completion tracking
private pendingToolCalls: Map<string, string>;  // callId -> status
private toolCompletionPromise?: Promise<void>;
private toolCompletionNotifier?: { resolve: () => void; reject: (reason?: Error) => void };

async waitForPendingTools(): Promise<void> {
  if (this.pendingToolCalls.size === 0) return;
  return this.toolCompletionPromise;
}

cancelPendingTools(reason: string): void {
  this.toolCompletionNotifier?.reject(new Error(reason));
  this.pendingToolCalls.clear();
  this._resetToolCompletionPromise();
}
```

**Auto-Approval (YOLO Mode):**
```typescript
if (this.autoExecute || this.config.getApprovalMode() === ApprovalMode.YOLO) {
  toolCalls.forEach((tc) => {
    if (tc.status === 'awaiting_approval' && tc.confirmationDetails) {
      tc.confirmationDetails.onConfirm(ToolConfirmationOutcome.ProceedOnce);
      this.pendingToolConfirmationDetails.delete(tc.request.callId);
    }
  });
}
```

**Event Types Handled:**
```typescript
switch (event.type) {
  case GeminiEventType.Content:        // Text output
  case GeminiEventType.ToolCallRequest: // Tool call (batched externally)
  case GeminiEventType.ToolCallResponse: // LLM-generated tool response
  case GeminiEventType.ToolCallConfirmation: // Confirmation request
  case GeminiEventType.UserCancelled:  // User cancelled
  case GeminiEventType.Thought:        // Model thinking
  case GeminiEventType.Citation:       // Source citations
  case GeminiEventType.ChatCompressed: // Context compression
  case GeminiEventType.Finished:       // Turn complete
  case GeminiEventType.ModelInfo:      // Model name update
  case GeminiEventType.Retry:          // Retry signal
  case GeminiEventType.InvalidStream:  // Stream error (retryable)
  case GeminiEventType.Error:          // Fatal error
}
```

---

### HTTP Layer

#### `src/http/app.ts` (355 lines)

Express application setup with A2A SDK integration.

**Agent Card Definition:**
```typescript
const coderAgentCard: AgentCard = {
  name: 'Gemini SDLC Agent',
  description: 'An agent that generates code...',
  url: 'http://localhost:41242/',
  protocolVersion: '0.3.0',
  version: '0.0.2',
  capabilities: {
    streaming: true,
    pushNotifications: false,
    stateTransitionHistory: true,
  },
  defaultInputModes: ['text'],
  defaultOutputModes: ['text'],
  skills: [{
    id: 'code_generation',
    name: 'Code Generation',
    description: 'Generates code snippets or complete files...',
    tags: ['code', 'development', 'programming'],
    inputModes: ['text'],
    outputModes: ['text'],
  }],
};
```

**Endpoints:**

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/.well-known/agent-card.json` | A2A discovery (via SDK) |
| POST | `/message/stream` | A2A streaming messages (via SDK) |
| POST | `/tasks` | Create new task |
| GET | `/tasks/metadata` | List all tasks |
| GET | `/tasks/:taskId/metadata` | Get task metadata |
| POST | `/executeCommand` | Execute server commands |
| GET | `/listCommands` | List available commands |

**Task Store Strategy:**
```typescript
const bucketName = process.env['GCS_BUCKET_NAME'];
let taskStoreForExecutor: TaskStore;
let taskStoreForHandler: TaskStore;

if (bucketName) {
  const gcsTaskStore = new GCSTaskStore(bucketName);
  taskStoreForExecutor = gcsTaskStore;
  taskStoreForHandler = new NoOpTaskStore(gcsTaskStore);  // Prevents double-save
} else {
  const inMemoryTaskStore = new InMemoryTaskStore();
  taskStoreForExecutor = inMemoryTaskStore;
  taskStoreForHandler = inMemoryTaskStore;
}
```

**Streaming Command Execution:**
```typescript
if (commandToExecute.streaming) {
  const eventBus = new DefaultExecutionEventBus();
  res.setHeader('Content-Type', 'text/event-stream');

  const eventHandler = (event: AgentExecutionEvent) => {
    const jsonRpcResponse = {
      jsonrpc: '2.0',
      id: 'taskId' in event ? event.taskId : event.messageId,
      result: event,
    };
    res.write(`data: ${JSON.stringify(jsonRpcResponse)}\n`);
  };

  eventBus.on('event', eventHandler);
  await commandToExecute.execute({ ...context, eventBus }, args ?? []);
  eventBus.off('event', eventHandler);
  return res.end();
}
```

#### `src/http/requestStorage.ts`

Uses `AsyncLocalStorage` to make the Express request available throughout the call stack:

```typescript
import { AsyncLocalStorage } from 'node:async_hooks';
export const requestStorage = new AsyncLocalStorage<{ req: express.Request }>();
```

Used in middleware:
```typescript
expressApp.use((req, res, next) => {
  requestStorage.run({ req }, next);
});
```

Accessed in executor for socket close handling:
```typescript
const store = requestStorage.getStore();
if (store) {
  const socket = store.req.socket;
  socket.on('end', onClientEnd);
}
```

---

### Type Definitions

#### `src/types.ts`

**CoderAgent Event Types:**
```typescript
enum CoderAgentEvent {
  ToolCallConfirmationEvent = 'tool-call-confirmation',
  ToolCallUpdateEvent = 'tool-call-update',
  TextContentEvent = 'text-content',
  StateChangeEvent = 'state-change',
  StateAgentSettingsEvent = 'agent-settings',
  ThoughtEvent = 'thought',
  CitationEvent = 'citation',
}
```

**Agent Settings (sent with first message):**
```typescript
interface AgentSettings {
  kind: CoderAgentEvent.StateAgentSettingsEvent;
  workspacePath: string;
  autoExecute?: boolean;
}
```

**Task Metadata:**
```typescript
interface TaskMetadata {
  id: string;
  contextId: string;
  taskState: TaskState;
  model: string;
  mcpServers: Array<{
    name: string;
    status: MCPServerStatus;
    tools: Array<{
      name: string;
      description: string;
      parameterSchema: unknown;
    }>;
  }>;
  availableTools: Array<{
    name: string;
    description: string;
    parameterSchema: unknown;
  }>;
}
```

**Tool Confirmation Response:**
```typescript
interface ToolConfirmationResponse {
  outcome: ToolConfirmationOutcome;
  callId: string;
}
```

**Persisted State Pattern:**
```typescript
interface PersistedStateMetadata {
  _agentSettings: AgentSettings;
  _taskState: TaskState;
}

const METADATA_KEY = '__persistedState';

function getPersistedState(metadata: PersistedTaskMetadata): PersistedStateMetadata | undefined;
function setPersistedState(metadata: PersistedTaskMetadata, state: PersistedStateMetadata): PersistedTaskMetadata;
```

---

### Commands

#### `src/commands/command-registry.ts`

Simple command registry pattern:
```typescript
class CommandRegistry {
  private readonly commands = new Map<string, Command>();

  initialize() {
    this.register(new ExtensionsCommand());
    this.register(new RestoreCommand());
    this.register(new InitCommand());
    this.register(new MemoryCommand());
  }

  register(command: Command) {
    this.commands.set(command.name, command);
    for (const subCommand of command.subCommands ?? []) {
      this.register(subCommand);
    }
  }
}
```

#### `src/commands/types.ts`

**Command Interface:**
```typescript
interface Command {
  readonly name: string;
  readonly description: string;
  readonly arguments?: CommandArgument[];
  readonly subCommands?: Command[];
  readonly topLevel?: boolean;
  readonly requiresWorkspace?: boolean;
  readonly streaming?: boolean;

  execute(config: CommandContext, args: string[]): Promise<CommandExecutionResponse>;
}

interface CommandContext {
  config: Config;
  git?: GitService;
  agentExecutor?: AgentExecutor;
  eventBus?: ExecutionEventBus;
}
```

#### `src/commands/init.ts`

Creates GEMINI.md for project initialization via agentic workflow:
- Checks if GEMINI.md exists
- If not, triggers agent to analyze project and create it
- Uses `autoExecute: true` for headless operation

#### `src/commands/restore.ts`

Checkpoint restoration:
- `restore list` - Lists available checkpoints
- `restore <name>` - Restores to checkpoint

#### `src/commands/memory.ts`

Memory management subcommands:
- `memory show` - Display current memory
- `memory refresh` - Reload from source
- `memory list` - List GEMINI.md paths
- `memory add <text>` - Append to memory

#### `src/commands/extensions.ts`

Extension management:
- `extensions list` - Show installed extensions

---

### Configuration

#### `src/config/config.ts`

**Config Loading:**
```typescript
async function loadConfig(settings: Settings, extensionLoader: ExtensionLoader, taskId: string): Promise<Config> {
  const configParams: ConfigParameters = {
    sessionId: taskId,
    model: settings.general?.previewFeatures ? PREVIEW_GEMINI_MODEL : DEFAULT_GEMINI_MODEL,
    sandbox: undefined,  // Not relevant for server
    targetDir: workspaceDir,
    approvalMode: process.env['GEMINI_YOLO_MODE'] === 'true' ? ApprovalMode.YOLO : ApprovalMode.DEFAULT,
    mcpServers: settings.mcpServers,
    checkpointing,
    interactive: true,
    enableInteractiveShell: true,
    ptyInfo: 'auto',
    // ...
  };

  // Load hierarchical memory (GEMINI.md files)
  const { memoryContent, fileCount, filePaths } = await loadServerHierarchicalMemory(...);

  const config = new Config({ ...configParams });
  await config.initialize();

  // Auth setup
  if (process.env['USE_CCPA']) {
    await config.refreshAuth(AuthType.LOGIN_WITH_GOOGLE);
  } else if (process.env['GEMINI_API_KEY']) {
    await config.refreshAuth(AuthType.USE_GEMINI);
  }

  return config;
}
```

**Environment File Discovery:**
```typescript
function findEnvFile(startDir: string): string | null {
  // Search order:
  // 1. .gemini/.env in current dir
  // 2. .env in current dir
  // 3. Walk up directories
  // 4. Fallback to home directory
}
```

#### `src/config/settings.ts`

Settings loading with env var interpolation:
```typescript
interface Settings {
  mcpServers?: Record<string, MCPServerConfig>;
  coreTools?: string[];
  excludeTools?: string[];
  telemetry?: TelemetrySettings;
  checkpointing?: { enabled?: boolean };
  folderTrust?: boolean;
  general?: { previewFeatures?: boolean };
  fileFiltering?: {
    respectGitIgnore?: boolean;
    respectGeminiIgnore?: boolean;
    enableRecursiveFileSearch?: boolean;
    customIgnoreFilePaths?: string[];
  };
}

function loadSettings(workspaceDir: string): Settings {
  // Load user settings from ~/.gemini/settings.json
  // Load workspace settings from <workspace>/.gemini/settings.json
  // Merge with workspace taking precedence
}

function resolveEnvVarsInString(value: string): string {
  // Supports $VAR_NAME and ${VAR_NAME} syntax
}
```

#### `src/config/extension.ts`

Extension loading from:
- `<workspace>/.gemini/extensions/`
- `~/.gemini/extensions/`

Each extension directory must contain `gemini-extension.json`:
```typescript
interface ExtensionConfig {
  name: string;
  version: string;
  mcpServers?: Record<string, MCPServerConfig>;
  contextFileName?: string | string[];
  excludeTools?: string[];
}
```

---

### Persistence

#### `src/persistence/gcs.ts`

**GCSTaskStore** - Persists task metadata and workspace to Google Cloud Storage:

```typescript
class GCSTaskStore implements TaskStore {
  private storage: Storage;
  private bucketName: string;

  async save(task: SDKTask): Promise<void> {
    // 1. Validate task ID (prevent path traversal)
    // 2. Compress and upload metadata as gzip JSON
    // 3. Create tar.gz of workspace directory
    // 4. Stream upload to GCS
  }

  async load(taskId: string): Promise<SDKTask | undefined> {
    // 1. Download and decompress metadata
    // 2. Extract persisted state (agent settings, task state)
    // 3. Download and extract workspace archive
    // 4. Reconstruct SDKTask
  }
}
```

**NoOpTaskStore** - Decorator that skips saves (delegates loads):
```typescript
class NoOpTaskStore implements TaskStore {
  constructor(private realStore: TaskStore) {}

  async save(task: SDKTask): Promise<void> {
    // No-op - prevents double save when handler and executor share store
  }

  async load(taskId: string): Promise<SDKTask | undefined> {
    return this.realStore.load(taskId);
  }
}
```

---

### Utilities

#### `src/utils/logger.ts`

Winston logger with custom format:
```typescript
const logger = winston.createLogger({
  level: 'info',
  format: winston.format.combine(
    winston.format.timestamp({ format: 'YYYY-MM-DD HH:mm:ss.SSS A' }),
    winston.format.printf((info) => {
      const { level, timestamp, message, ...rest } = info;
      return `[${level.toUpperCase()}] ${timestamp} -- ${message}` +
        `${Object.keys(rest).length > 0 ? `\n${JSON.stringify(rest, null, 2)}` : ''}`;
    }),
  ),
  transports: [new winston.transports.Console()],
});
```

#### `src/utils/executor_utils.ts`

Helper for publishing failed task state:
```typescript
async function pushTaskStateFailed(
  error: unknown,
  eventBus: ExecutionEventBus,
  taskId: string,
  contextId: string
) {
  eventBus.publish({
    kind: 'status-update',
    taskId,
    contextId,
    status: { state: 'failed', message: { ... } },
    final: true,
    metadata: { coderAgent: { kind: 'state-change' }, error: errorMessage },
  });
}
```

#### `src/utils/testing_utils.ts`

Test utilities including `createMockConfig()` for comprehensive Config mocking.

---

## Protocol Details

### A2A Protocol Version

- **Protocol Version:** 0.3.0
- **SDK:** `@a2a-js/sdk` v0.3.8

### Message Flow

```
Client                          Server
  |                               |
  |--- POST /tasks --------------->|  Create task
  |<-- 201 { taskId } ------------|
  |                               |
  |--- POST /message/stream ------>|  Send message (SSE)
  |    { message, taskId }        |
  |                               |
  |<-- SSE: task created ---------|
  |<-- SSE: working --------------|
  |<-- SSE: tool-call-update -----|
  |<-- SSE: tool-call-confirm ----|  (if approval needed)
  |                               |
  |--- POST /message/stream ------>|  Tool confirmation
  |    { data: { callId, outcome }}|
  |                               |
  |<-- SSE: tool-call-update -----|
  |<-- SSE: text-content ---------|
  |<-- SSE: input-required -------|  (final: true)
  |                               |
```

### Task States

| State | Description |
|-------|-------------|
| `submitted` | Task created, not yet started |
| `working` | Agent is processing |
| `input-required` | Waiting for user input (final turn state) |
| `completed` | Task finished successfully |
| `failed` | Task encountered fatal error |
| `canceled` | Task was cancelled by user |

### Event Types (CoderAgent Extensions)

| Event | Purpose |
|-------|---------|
| `tool-call-confirmation` | Request user approval for tool |
| `tool-call-update` | Tool status changed (executing, success, error) |
| `text-content` | Agent text output |
| `state-change` | Task state transition |
| `agent-settings` | Initial configuration (workspace path, auto-execute) |
| `thought` | Model's thinking process |
| `citation` | Source citations |

### Tool Confirmation Outcomes

```typescript
enum ToolConfirmationOutcome {
  ProceedOnce = 'proceed_once',
  Cancel = 'cancel',
  ProceedAlways = 'proceed_always',
  ProceedAlwaysServer = 'proceed_always_server',
  ProceedAlwaysTool = 'proceed_always_tool',
  ModifyWithEditor = 'modify_with_editor',
}
```

---

## Key Takeaways for AVA

### Features AVA Might Be Missing

1. **A2A Protocol Support**
   - Standardized agent-to-agent communication
   - Agent Card discovery (`/.well-known/agent-card.json`)
   - Streaming message protocol
   - Skill/capability advertisement

2. **Workspace Persistence**
   - Task state + workspace archiving to GCS
   - Ability to resume tasks across server restarts
   - Compressed storage (gzip + tar)

3. **Socket Close Handling**
   - `AsyncLocalStorage` for request context
   - Automatic abort on client disconnect
   - Prevents orphaned executions

4. **Batched Tool Execution**
   - Collect all tool calls from single LLM response
   - Schedule as batch for checkpointing
   - Wait for all tools before continuing

5. **Checkpointing for File Edits**
   - Automatic git checkpoints before edits
   - `EDIT_TOOL_NAMES` set for restorable tools
   - Checkpoint mapping per tool call

6. **Auto-Execute Mode**
   - Per-task `autoExecute` flag
   - Separate from global YOLO mode
   - Useful for headless/automated workflows

7. **Environment Variable Interpolation**
   - `$VAR` and `${VAR}` syntax in settings
   - Hierarchical env file discovery
   - Workspace-specific env overrides

8. **Extension System**
   - Extensions directory per workspace/user
   - MCP servers via extensions
   - Context files (GEMINI.md) per extension

9. **Server Commands**
   - `/executeCommand` endpoint for meta operations
   - Streaming command responses
   - Commands can trigger agentic workflows (init)

10. **Thought/Citation Events**
    - Separate event types for model thinking
    - Citation tracking from LLM

### Architecture Patterns Worth Adopting

1. **TaskWrapper Pattern**
   - Clean separation between internal and SDK types
   - Serialization/deserialization in one place
   - Persisted state metadata

2. **NoOp Decorator for Stores**
   - Prevents double-writes when sharing stores
   - Elegant solution for handler/executor split

3. **Tool Completion Promise**
   - Promise-based waiting for tool batch completion
   - Clean cancellation via reject()
   - Size-based auto-resolution

4. **Request Context via AsyncLocalStorage**
   - Makes request available anywhere without threading
   - Enables socket-level abort handling

5. **Hierarchical Settings Merge**
   - User settings as base
   - Workspace settings override
   - Environment variable interpolation

---

## Comparison with AVA

| Feature | Gemini A2A | AVA | Gap |
|---------|------------|--------|-----|
| Agent-to-Agent Protocol | Yes (A2A 0.3) | No | Missing |
| Task Persistence | GCS + InMemory | SQLite | Similar |
| Socket Close Handling | AsyncLocalStorage | ? | Review needed |
| Batched Tool Execution | Yes | ? | Review needed |
| Checkpointing | Git-based | ? | Review needed |
| Auto-Execute Mode | Per-task flag | Global only? | Review needed |
| Extension System | Full | MCP only | Partial |
| Server Commands | REST endpoint | CLI only? | Missing |
| Thought/Citation Events | Yes | ? | Review needed |

---

## Recommendations

1. **Consider A2A Protocol Support**
   - Would enable AVA to be controlled by other AI agents
   - Useful for multi-agent orchestration scenarios
   - SDK available (`@a2a-js/sdk`)

2. **Adopt AsyncLocalStorage Pattern**
   - Clean way to access request context
   - Enables abort-on-disconnect without threading

3. **Implement Task Persistence**
   - Already have SQLite foundation
   - Add workspace archiving for full state recovery

4. **Add Per-Task Auto-Execute**
   - Useful for automated pipelines
   - Separate from global approval mode

5. **Expose Server Commands API**
   - Enable meta-operations via API
   - Useful for IDE integrations

---

## Files Summary

| Path | Lines | Purpose |
|------|-------|---------|
| `src/agent/executor.ts` | 615 | Core agent executor |
| `src/agent/task.ts` | 1078 | Task session management |
| `src/http/app.ts` | 355 | Express app + endpoints |
| `src/http/server.ts` | 35 | Entry point |
| `src/http/requestStorage.ts` | 11 | AsyncLocalStorage |
| `src/config/config.ts` | 225 | Config loading |
| `src/config/settings.ts` | 164 | Settings management |
| `src/config/extension.ts` | 152 | Extension loading |
| `src/persistence/gcs.ts` | 316 | GCS task store |
| `src/commands/*.ts` | ~400 | Server commands |
| `src/types.ts` | 140 | Type definitions |
| `src/utils/*.ts` | ~250 | Utilities |
| **Total** | ~3700 | |
