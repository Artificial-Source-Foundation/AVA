# Cline Backend Architecture Analysis

> VS Code extension AI coding agent (~58k GitHub stars). TypeScript, monolithic single-extension architecture with gRPC-like webview communication.

---

## 1. Project Structure

```
cline/
├── src/                          # Main extension source
│   ├── core/                     # Core backend logic
│   │   ├── api/                  # LLM provider abstraction + 40+ providers
│   │   ├── assistant-message/    # XML/native tool call parsing
│   │   ├── commands/             # VS Code command registrations
│   │   ├── context/              # Context management, instructions, rules
│   │   ├── controller/           # gRPC handler hub — routes webview requests
│   │   ├── hooks/                # Lifecycle hooks (TaskStart, PreToolUse, PostToolUse, etc.)
│   │   ├── ignore/               # .clineignore file handling
│   │   ├── locks/                # SQLite-based task folder locks
│   │   ├── mentions/             # @-mention resolution (files, URLs, git, terminal)
│   │   ├── permissions/          # Command permission controller (allow/deny globs)
│   │   ├── prompts/              # System prompt building, variant system, responses
│   │   ├── slash-commands/       # /compact, /newtask, /smol, /reportbug, etc.
│   │   ├── storage/              # StateManager, disk persistence, remote config
│   │   ├── task/                 # THE MAIN CLASS — Task (agent loop), ToolExecutor, subagents
│   │   ├── webview/              # Legacy webview bridge (pre-gRPC)
│   │   └── workspace/            # Multi-root workspace support
│   ├── integrations/             # External system integrations
│   │   ├── checkpoints/          # Shadow git checkpoint system
│   │   ├── claude-code/          # Claude Code CLI integration
│   │   ├── diagnostics/          # VS Code diagnostics bridge
│   │   ├── editor/               # DiffViewProvider, FileEditProvider
│   │   ├── notifications/        # System notifications
│   │   ├── openai-codex/         # OpenAI Codex integration
│   │   └── terminal/             # Terminal management (VSCode + standalone)
│   ├── services/                 # Shared services
│   │   ├── browser/              # Puppeteer browser automation
│   │   ├── mcp/                  # MCP client (McpHub)
│   │   ├── telemetry/            # Usage telemetry
│   │   └── tree-sitter/          # Code definition extraction
│   ├── shared/                   # Types shared between extension + webview
│   ├── hosts/                    # Host abstraction (VSCode, CLI, standalone)
│   └── packages/                 # Internal packages
├── proto/                        # Protobuf definitions (gRPC protocol)
│   ├── cline/                    # 16 .proto files for app domain
│   └── host/                     # 5 .proto files for host abstraction
├── cli/                          # React Ink terminal UI
├── webview-ui/                   # React webview (chat UI)
├── standalone/                   # Standalone mode (non-VSCode)
└── evals/                        # Evaluation framework
```

**Key metrics:**
- `src/core/task/index.ts` (the Task class): **3,547 lines** — the single largest file, contains the full agent loop
- 40+ LLM provider handlers in `src/core/api/providers/`
- 16 protobuf service definitions in `proto/cline/`

**Architecture style:** Monolithic single-class agent. The `Task` class owns the entire agent loop, streaming, tool execution, context management, and checkpoint coordination. `Controller` is the outer shell that manages task lifecycle and webview communication.

---

## 2. Tools

Cline defines tools as an enum (`ClineDefaultTool`) with 25 built-in tools. Tool handlers live in `src/core/task/tools/handlers/`, one file per handler.

| Tool Name | Enum Key | Handler File | Purpose |
|-----------|----------|-------------|---------|
| `read_file` | `FILE_READ` | `ReadFileToolHandler.ts` | Read file contents with line ranges |
| `write_to_file` | `FILE_NEW` | `WriteToFileToolHandler.ts` | Create new files |
| `replace_in_file` | `FILE_EDIT` | `WriteToFileToolHandler.ts` (shared) | Edit existing files with search/replace blocks |
| `apply_patch` | `APPLY_PATCH` | `ApplyPatchHandler.ts` | Apply unified diffs |
| `execute_command` | `BASH` | `ExecuteCommandToolHandler.ts` | Run shell commands |
| `search_files` | `SEARCH` | `SearchFilesToolHandler.ts` | Regex search across files (ripgrep) |
| `list_files` | `LIST_FILES` | `ListFilesToolHandler.ts` | Directory listing (top-level or recursive) |
| `list_code_definition_names` | `LIST_CODE_DEF` | `ListCodeDefinitionNamesToolHandler.ts` | Extract symbols via tree-sitter |
| `browser_action` | `BROWSER` | `BrowserToolHandler.ts` | Puppeteer browser automation |
| `use_mcp_tool` | `MCP_USE` | `UseMcpToolHandler.ts` | Call MCP server tools |
| `access_mcp_resource` | `MCP_ACCESS` | `AccessMcpResourceHandler.ts` | Read MCP resources |
| `load_mcp_documentation` | `MCP_DOCS` | `LoadMcpDocumentationHandler.ts` | Load MCP server docs into context |
| `ask_followup_question` | `ASK` | `AskFollowupQuestionToolHandler.ts` | Ask user a clarifying question |
| `attempt_completion` | `ATTEMPT` | `AttemptCompletionHandler.ts` | Signal task completion with result |
| `plan_mode_respond` | `PLAN_MODE` | `PlanModeRespondHandler.ts` | Respond in plan mode |
| `act_mode_respond` | `ACT_MODE` | `ActModeRespondHandler.ts` | Respond in act mode |
| `new_task` | `NEW_TASK` | `NewTaskHandler.ts` | Spawn a follow-up task |
| `web_fetch` | `WEB_FETCH` | `WebFetchToolHandler.ts` | Fetch and convert web pages |
| `web_search` | `WEB_SEARCH` | `WebSearchToolHandler.ts` | Web search |
| `focus_chain` | `TODO` | (no handler) | Focus chain / todo list management |
| `condense` | `CONDENSE` | `CondenseHandler.ts` | Manually trigger context condensation |
| `summarize_task` | `SUMMARIZE_TASK` | `SummarizeTaskHandler.ts` | Auto-summarize before context overflow |
| `report_bug` | `REPORT_BUG` | `ReportBugHandler.ts` | User-triggered bug reporting |
| `new_rule` | `NEW_RULE` | `WriteToFileToolHandler.ts` (shared) | Create a new .clinerules file |
| `generate_explanation` | `GENERATE_EXPLANATION` | `GenerateExplanationToolHandler.ts` | AI-generated inline explanations for diffs |
| `use_skill` | `USE_SKILL` | `UseSkillToolHandler.ts` | Load a skill from .clinerules |
| `use_subagents` | `USE_SUBAGENTS` | `SubagentToolHandler.ts` | Spawn subagent(s) for parallel research |

**Tool registration pattern:**

Tools are registered via `ToolExecutorCoordinator`, which maps each `ClineDefaultTool` enum value to a handler factory:

```typescript
// src/core/task/tools/ToolExecutorCoordinator.ts
private readonly toolHandlersMap: Record<ClineDefaultTool, (v: ToolValidator) => IToolHandler | undefined> = {
    [ClineDefaultTool.FILE_READ]: (v) => new ReadFileToolHandler(v),
    [ClineDefaultTool.BASH]: (v) => new ExecuteCommandToolHandler(v),
    // ...
}
```

Each handler implements `IToolHandler`:
```typescript
interface IToolHandler {
    readonly name: ClineDefaultTool
    execute(config: TaskConfig, block: ToolUse): Promise<ToolResponse>
    getDescription(block: ToolUse): string
}
```

Handlers that support streaming UI updates also implement `IPartialBlockHandler`:
```typescript
interface IPartialBlockHandler {
    handlePartialBlock(block: ToolUse, uiHelpers: StronglyTypedUIHelpers): Promise<void>
}
```

**Dynamic tools:** MCP tools are dynamically registered at runtime. Tool names containing the `CLINE_MCP_TOOL_IDENTIFIER` separator are normalized to the `MCP_USE` handler. Custom subagent tools from `~/Documents/Cline/Agents/` YAML configs are also dynamically registered.

**Read-only tools** (safe to run during initial checkpoint commit):
```typescript
const READ_ONLY_TOOLS = [
    LIST_FILES, FILE_READ, SEARCH, LIST_CODE_DEF,
    BROWSER, ASK, WEB_SEARCH, WEB_FETCH, USE_SKILL, USE_SUBAGENTS
]
```

---

## 3. Agent Loop

The agent loop lives entirely in `src/core/task/index.ts` (the `Task` class, 3,547 lines). The core flow:

### Lifecycle

```
Controller.initTask()
    → new Task(params)
    → task.startTask(text, images, files)   // or task.resumeTaskFromHistory()
        → initiateTaskLoop(userContent)
            → while (!abort) {
                  didEndLoop = recursivelyMakeClineRequests(userContent)
                  if (didEndLoop) break
                  userContent = noToolsUsed()   // nudge model to use tools
                  consecutiveMistakeCount++
              }
```

### `recursivelyMakeClineRequests(userContent)`

This is the main request cycle. Each iteration:

1. **Build system prompt** — calls `attemptApiRequest()` which assembles the full prompt context (rules, MCP docs, skills, workspace info, browser state, etc.)
2. **Stream LLM response** — yields chunks from the `ApiStream` async generator
3. **Parse assistant message** — uses `parseAssistantMessageV2()` for XML tool calls, or native tool call parsing via `StreamResponseHandler`
4. **Present content blocks** — `presentAssistantMessage()` processes each block:
   - `text` blocks: displayed in chat, with `<thinking>` / `<think>` tag stripping
   - `tool_use` blocks: routed to `ToolExecutor.executeTool(block)`
5. **Collect tool results** — pushed into `userMessageContent` array
6. **Wait for all tools** — `pWaitFor(() => userMessageContentReady)`
7. **Save checkpoint** if enabled
8. **Loop** — the collected tool results become the next user message

### Streaming Architecture

```typescript
// src/core/api/transform/stream.ts
type ApiStream = AsyncGenerator<ApiStreamChunk>
type ApiStreamChunk =
    | ApiStreamTextChunk      // { type: "text", text, id?, signature? }
    | ApiStreamThinkingChunk  // { type: "reasoning", reasoning, details?, signature? }
    | ApiStreamUsageChunk     // { type: "usage", inputTokens, outputTokens, ... }
    | ApiStreamToolCallsChunk // { type: "tool_calls", tool_call: { function: { name, arguments } } }
```

The `StreamResponseHandler` handles both XML-based tool parsing (for providers that use text-based tools) and native tool call accumulation (for providers like Anthropic/OpenAI that return structured tool calls). It uses a JSON streaming parser (`@streamparser/json`) to parse partial tool call arguments as they arrive.

### Dual Tool Call Modes

Cline supports two tool call modes simultaneously:
- **XML tools** (legacy): Tools defined as XML tags in the system prompt, parsed from text output via `parseAssistantMessageV2()`
- **Native tool calls**: Tools sent as structured function definitions to the API, returned as structured `tool_use` blocks

The mode is determined by `enableNativeToolCalls` which depends on the model's `apiFormat` and user settings. Some providers (OpenAI Responses API) require native tool calling.

### Error Handling & Retries

Provider-level retries use a decorator pattern:
```typescript
// src/core/api/retry.ts
@withRetry({ maxRetries: 3, baseDelay: 1000, maxDelay: 10000 })
```

Task-level error handling in `attemptApiRequest()`:
1. First chunk failure triggers context window check
2. Context window exceeded: automatic truncation + retry (once)
3. Other errors: auto-retry up to 3 times with exponential backoff (2s, 4s, 8s)
4. After auto-retries exhausted: present `api_req_failed` ask to user
5. Auth/balance errors: never auto-retried

### Parallel Tool Calling

Controlled by `enableParallelToolCalling` setting. When enabled, multiple tool blocks in a single assistant message are all executed. When disabled, only the first tool block executes and subsequent ones get a `toolAlreadyUsed` error message.

### Consecutive Mistake Tracking

```typescript
if (consecutiveMistakeCount >= maxConsecutiveMistakes) {
    // In YOLO mode: fail the task
    // Otherwise: ask user "mistake_limit_reached"
}
```

---

## 4. LLM Providers

Cline supports **40+ providers** through a simple handler interface:

```typescript
// src/core/api/index.ts
interface ApiHandler {
    createMessage(systemPrompt: string, messages: ClineStorageMessage[], tools?: ClineTool[]): ApiStream
    getModel(): ApiHandlerModel   // { id: string, info: ModelInfo }
    getApiStreamUsage?(): Promise<ApiStreamUsageChunk | undefined>
    abort?(): void
}
```

### Provider List

| Provider | Handler | Notes |
|----------|---------|-------|
| Anthropic | `AnthropicHandler` | Default fallback |
| OpenRouter | `OpenRouterHandler` | Model marketplace |
| AWS Bedrock | `AwsBedrockHandler` | Cross-region, prompt cache |
| Google Vertex | `VertexHandler` | Gemini models |
| Google Gemini | `GeminiHandler` | Direct API |
| OpenAI (compatible) | `OpenAiHandler` | Any OpenAI-compat endpoint |
| OpenAI Native | `OpenAiNativeHandler` | Direct OpenAI API |
| OpenAI Codex | `OpenAiCodexHandler` | Responses API |
| Ollama | `OllamaHandler` | Local models |
| LM Studio | `LmStudioHandler` | Local models |
| DeepSeek | `DeepSeekHandler` | |
| Mistral | `MistralHandler` | |
| Groq | `GroqHandler` | |
| Together | `TogetherHandler` | |
| Fireworks | `FireworksHandler` | |
| Cerebras | `CerebrasHandler` | |
| SambaNova | `SambanovaHandler` | |
| xAI | `XAIHandler` | |
| HuggingFace | `HuggingFaceHandler` | |
| Nebius | `NebiusHandler` | |
| Qwen | `QwenHandler` | China/international regions |
| Qwen Code | `QwenCodeHandler` | OAuth-based |
| Doubao | `DoubaoHandler` | |
| Moonshot | `MoonshotHandler` | |
| Minimax | `MinimaxHandler` | |
| LiteLLM | `LiteLlmHandler` | Proxy |
| Requesty | `RequestyHandler` | |
| Baseten | `BasetenHandler` | |
| AskSage | `AskSageHandler` | |
| SAP AI Core | `SapAiCoreHandler` | Enterprise |
| Vercel AI Gateway | `VercelAIGatewayHandler` | |
| Huawei Cloud MaaS | `HuaweiCloudMaaSHandler` | |
| Dify | `DifyHandler` | |
| Claude Code | `ClaudeCodeHandler` | Wraps Claude Code CLI |
| VS Code LM | `VsCodeLmHandler` | VS Code language model API |
| Cline (hosted) | `ClineHandler` | Cline's own API |
| OCA | `OcaHandler` | |
| AIHubMix | `AIhubmixHandler` | |
| ZAI | `ZAiHandler` | |
| Hicap | `HicapHandler` | |
| Nous Research | `NousResearchHandler` | |

### Provider Selection

Cline supports **separate providers for Plan mode and Act mode**:
```typescript
function buildApiHandler(configuration: ApiConfiguration, mode: Mode): ApiHandler {
    const apiProvider = mode === "plan" ? planModeApiProvider : actModeApiProvider
    return createHandlerForProvider(apiProvider, options, mode)
}
```

### System Prompt Variants

The system prompt is modular with model-family-specific variants:
```
src/core/prompts/system-prompt/
├── components/     # Shared sections (rules, capabilities, editing_files)
├── variants/       # Model-specific configs
│   ├── generic/    # Default fallback
│   ├── next-gen/   # Claude 4, GPT-5, Gemini 2.5
│   ├── xs/         # Small/local models
│   ├── gpt-5/      # GPT-5 specific
│   ├── gemini-3/   # Gemini 3 specific
│   ├── hermes/     # Hermes models
│   └── glm/        # GLM models
├── tools/          # Tool definitions per variant
├── templates/      # Template engine with {{PLACEHOLDER}} resolution
└── registry/       # ClineToolSet — tool spec registry
```

---

## 5. Context/Token Management

### Context Window Calculation

```typescript
// src/core/context/context-management/context-window-utils.ts
function getContextWindowInfo(api: ApiHandler) {
    let contextWindow = api.getModel().info.contextWindow || 128_000
    let maxAllowedSize: number
    switch (contextWindow) {
        case 64_000:  maxAllowedSize = contextWindow - 27_000; break  // deepseek
        case 128_000: maxAllowedSize = contextWindow - 30_000; break  // most models
        case 200_000: maxAllowedSize = contextWindow - 40_000; break  // claude
        default:      maxAllowedSize = Math.max(contextWindow - 40_000, contextWindow * 0.8)
    }
    return { contextWindow, maxAllowedSize }
}
```

### ContextManager

`src/core/context/context-management/ContextManager.ts` (~1,200 lines) is the core context management system:

1. **File read optimization** — Tracks which messages contain file contents (`EditType.READ_FILE_TOOL`, `EditType.FILE_MENTION`, etc.) and can replace full file contents with summaries when context is tight
2. **Truncation** — `getNextTruncationRange()` calculates which message range to delete, using a "quarter" strategy (delete the next quarter of early messages)
3. **Deleted range tracking** — `conversationHistoryDeletedRange: [start, end]` stored in task state; messages in this range are excluded from API calls but preserved on disk
4. **Context history serialization** — Persisted as JSON to `{taskDir}/context_history.json`

### Auto-Condensation

When context approaches the limit:
1. First attempt: **file read optimization** — replace file contents with truncated versions
2. If still too large: **truncation** — delete a quarter of early messages
3. On `summarize_task` tool call: model generates a comprehensive summary that replaces the full conversation

The `summarizeTask` prompt (`src/core/prompts/contextManagement.ts`) is injected when context is running low, forcing the model to either call `attempt_completion` or `summarize_task`.

### Auto-Condense (Next-Gen Models)

For newer models, a proactive condensation threshold of 75% context window usage triggers compaction before the request:
```typescript
if (useAutoCondense && isNextGenModelFamily(modelId)) {
    const autoCondenseThreshold = 0.75
    // ...
}
```

---

## 6. gRPC Protocol

Cline uses a **gRPC-like protocol over VS Code message passing** (not actual gRPC). Protobuf is used for type generation, but transport is via `postMessage()`.

### Proto Files

```
proto/cline/
├── common.proto        # Shared types: Empty, StringRequest, Int64, Metadata, Diagnostics
├── task.proto          # TaskService: newTask, cancelTask, askResponse, explainChanges
├── ui.proto            # UiService: scrollToSettings, subscribeToPartialMessage, subscribeToState
├── state.proto         # StateService: getLatestState, updateSettings, togglePlanActMode
├── models.proto        # ModelService: getAvailableModels, ApiProvider enum, ApiConfiguration
├── mcp.proto           # McpService: toggleMcpServer, restartMcpServer, subscribeToMcpServers
├── checkpoints.proto   # CheckpointsService: checkpointDiff, checkpointRestore
├── browser.proto       # BrowserService: browser connection, discovery
├── commands.proto      # CommandsService: openFile, openDiff
├── file.proto          # FileService: file operations
├── hooks.proto         # HooksService: hook management
├── slash.proto         # SlashService: slash command operations
├── account.proto       # AccountService: auth, subscribeToAuthCallback
├── oca_account.proto   # OcaAccountService: OCA auth
├── web.proto           # WebService: web operations
└── worktree.proto      # WorktreeService: git worktree management

proto/host/
├── window.proto        # Window operations (showMessage, getTabs)
├── workspace.proto     # Workspace operations (getPaths, openPanel)
├── diff.proto          # Diff operations (openMultiFileDiff)
├── env.proto           # Environment info
└── testing.proto       # Test utilities
```

### Transport Layer

The gRPC handler receives messages from the webview via VS Code's `postMessage` and routes them:

```typescript
// src/core/controller/grpc-handler.ts
async function handleGrpcRequest(controller, postMessageToWebview, request: GrpcRequest) {
    if (request.is_streaming) {
        await handleStreamingRequest(controller, postMessageToWebview, request)
    } else {
        await handleUnaryRequest(controller, postMessageToWebview, request)
    }
}
```

Messages have this shape:
```typescript
// Request (webview -> extension)
interface GrpcRequest {
    service: string      // e.g. "TaskService"
    method: string       // e.g. "newTask"
    message: any         // Proto-encoded request
    request_id: string   // Correlation ID
    is_streaming: boolean
}

// Response (extension -> webview)
interface ExtensionMessage {
    type: "grpc_response"
    grpc_response: {
        message?: any
        error?: string
        request_id: string
        is_streaming?: boolean
        sequence_number?: number
    }
}
```

### Service Registry

```typescript
// src/core/controller/grpc-service.ts
class ServiceRegistry {
    registerMethod(methodName, handler, metadata?: { isStreaming: boolean })
    handleRequest(controller, method, message): Promise<any>
    handleStreamingRequest(controller, method, message, responseStream, requestId?)
}
```

Generated handlers are imported from `src/generated/hosts/vscode/protobus-services` and route to domain-specific handlers in `src/core/controller/<domain>/`.

### Key Services

- **TaskService** — `newTask`, `cancelTask`, `askResponse`, `explainChanges`
- **StateService** — `getLatestState`, `updateSettings`, `subscribeToState` (streaming)
- **UiService** — `subscribeToPartialMessage` (streaming), `subscribeToAddToInput` (streaming)
- **McpService** — `toggleMcpServer`, `subscribeToMcpServers` (streaming)
- **CheckpointsService** — `checkpointDiff`, `checkpointRestore`, `subscribeToCheckpoints` (streaming)

### Recording

All gRPC requests and responses are optionally recorded via `GrpcRecorderBuilder` for debugging/replay.

---

## 7. MCP Integration

`src/services/mcp/McpHub.ts` — Full MCP client implementation.

### Transport

Uses the official `@modelcontextprotocol/sdk`:
- **StdioClientTransport** — for local MCP servers (spawns subprocess)
- **SSEClientTransport** — for remote servers via Server-Sent Events
- **StreamableHTTPClientTransport** — for newer HTTP streaming transport

### Features

- **Connection management** — `McpConnection[]` array, auto-reconnect on file watcher triggers
- **File watcher** — Watches `mcp_settings.json` for configuration changes
- **OAuth** — `McpOAuthManager` handles OAuth flows for authenticated MCP servers
- **Tool auto-approve** — Per-tool auto-approve settings stored in MCP config
- **Marketplace** — `McpMarketplaceCatalog` for discovering and installing MCP servers
- **Notifications** — Real-time MCP server notifications displayed in chat

### MCP Tool Integration

MCP tools are registered with unique short keys to avoid long tool names:
```typescript
// Tool name format: "c<5-char-nanoid>__toolName"
const uid = "c" + nanoid(5)  // e.g. "cAb3Xy"
McpHub.mcpServerKeys.set(uid, serverName)
```

When the agent calls an MCP tool, the `CLINE_MCP_TOOL_IDENTIFIER` separator in the name is used to split server key from tool name, then routed to `UseMcpToolHandler`.

### MCP in System Prompt

MCP server capabilities (tools, resources, prompts) are dynamically injected into the system prompt via `loadMcpDocumentation()`.

---

## 8. Task Model (Controller -> Task Hierarchy)

### Controller

`src/core/controller/index.ts` — Singleton per webview panel. Manages:

- **Task lifecycle** — `initTask()`, `clearTask()`, `cancelTask()`
- **State management** — `StateManager` for global/workspace state
- **MCP Hub** — shared `McpHub` instance across tasks
- **Auth services** — `AuthService`, `OcaAuthService`, `ClineAccountService`
- **gRPC routing** — All webview requests flow through Controller
- **Remote config** — Periodic fetch of remote configuration (hourly)

### Task

`src/core/task/index.ts` — One active Task at a time per Controller. The Task owns:

- **Agent loop** — `initiateTaskLoop()` / `recursivelyMakeClineRequests()`
- **API handler** — Built for the current provider/mode
- **ToolExecutor** — Routes tool calls to handlers
- **MessageStateHandler** — Manages both `clineMessages` (UI) and `apiConversationHistory` (API)
- **ContextManager** — Context window tracking and optimization
- **CheckpointManager** — Shadow git snapshots
- **BrowserSession** — Puppeteer instance
- **CommandExecutor** — Shell command execution
- **FocusChainManager** — Progress tracking / todo list

### Relationship

```
Controller (1 per panel)
    ├── McpHub (shared)
    ├── StateManager (shared)
    ├── WorkspaceManager (lazy)
    └── Task? (0 or 1 active)
           ├── TaskState (mutable state bag)
           ├── MessageStateHandler
           ├── ToolExecutor
           │     └── ToolExecutorCoordinator
           │           └── Map<string, IToolHandler>
           ├── ContextManager
           ├── CheckpointManager?
           ├── BrowserSession
           ├── CommandExecutor
           ├── FocusChainManager?
           └── various trackers (FileContext, ModelContext, Environment)
```

### TaskState

`src/core/task/TaskState.ts` — A mutable state bag with 30+ fields:

```typescript
class TaskState {
    isStreaming: boolean
    abort: boolean
    didRejectTool: boolean
    didAlreadyUseTool: boolean
    consecutiveMistakeCount: number
    conversationHistoryDeletedRange?: [number, number]
    autoRetryAttempts: number
    currentStreamingContentIndex: number
    // ... 20+ more fields
}
```

### Concurrency

A single `Mutex` (`p-mutex`) protects all state modifications:
```typescript
private stateMutex = new Mutex()
private async withStateLock<T>(fn: () => T | Promise<T>): Promise<T> {
    return await this.stateMutex.withLock(fn)
}
```

---

## 9. Permissions/Safety

### Auto-Approve System

`src/core/task/tools/autoApprove.ts` — Three tiers of auto-approval:

1. **YOLO mode** (`yoloModeToggled`) — Auto-approve everything
2. **Auto-approve all** (`autoApproveAllToggled`) — Auto-approve everything
3. **Granular settings** (`autoApprovalSettings.actions`) — Per-category:
   - `readFiles` / `readFilesExternally`
   - `editFiles` / `editFilesExternally`
   - `executeSafeCommands` / `executeAllCommands`
   - `useBrowser`
   - `useMcp`

**Path-aware approval:** For file operations, checks if the target path is inside the workspace. External paths require separate `*Externally` approval flags.

### Command Permission Controller

`src/core/permissions/CommandPermissionController.ts` — Environment variable-based command filtering:

```bash
CLINE_COMMAND_PERMISSIONS='{"allow": ["npm *", "git *"], "deny": ["rm -rf *"], "allowRedirects": false}'
```

Features:
- Glob pattern matching for allow/deny lists
- Recursive subshell parsing (handles `$()`, `()`)
- Dangerous character detection (backticks outside single quotes, newlines)
- Redirect operator blocking (unless `allowRedirects: true`)
- Shell operator splitting (&&, ||, |, ;) — each segment validated independently

### Plan Mode Restrictions

In strict plan mode, file modification tools are blocked:
```typescript
private static readonly PLAN_MODE_RESTRICTED_TOOLS = [
    ClineDefaultTool.FILE_NEW,
    ClineDefaultTool.FILE_EDIT,
    ClineDefaultTool.NEW_RULE,
    ClineDefaultTool.APPLY_PATCH,
]
```

### .clineignore

`ClineIgnoreController` provides gitignore-style file exclusion. The `ToolValidator` checks paths against ignore rules before tool execution.

### User Approval Flow

For non-auto-approved tools:
1. Tool handler calls `ask("tool", toolDescription)` — presents approval dialog in UI
2. User clicks approve/reject
3. If rejected: `taskState.didRejectTool = true` — all subsequent tools in this turn are skipped

---

## 10. Browser Automation

`src/services/browser/BrowserSession.ts` — Puppeteer-based browser automation.

### Architecture

```typescript
class BrowserSession {
    private browser?: Browser     // puppeteer-core Browser instance
    private page?: Page
    private currentMousePosition?: string
    private cachedWebSocketEndpoint?: string
}
```

### Chrome Discovery

Priority order:
1. User-configured `chromeExecutablePath` from settings
2. System Chrome via `chrome-launcher`
3. Bundled Chromium via `puppeteer-chromium-resolver`

### Remote Browser Support

Can connect to a remote Chrome instance:
```typescript
async testConnection(host: string): Promise<{ success: boolean; message: string; endpoint?: string }>
```

### Browser Actions (from proto)

```protobuf
enum BrowserAction {
    LAUNCH = 0;
    CLICK = 1;
    TYPE = 2;
    SCROLL_DOWN = 3;
    SCROLL_UP = 4;
    CLOSE = 5;
}
```

The `BrowserToolHandler` takes screenshots after each action and sends them to the model for vision-based navigation. Actions include coordinate-based clicking, typing, and scrolling.

### WebP Support

Uses WebP format for screenshots when the model supports it (smaller files):
```typescript
const useWebp = this.api ? !modelDoesntSupportWebp(apiHandlerModel) : true
```

### UrlContentFetcher

Separate `UrlContentFetcher` class for headless page content extraction (used by `@url` mentions and `web_fetch` tool).

---

## 11. Subagent System

`src/core/task/tools/subagent/` — Lightweight subagent system for parallel research.

### Architecture

```
SubagentToolHandler (use_subagents tool)
    → SubagentBuilder (configures tools, system prompt, API handler)
        → SubagentRunner (runs independent agent loop)
```

### SubagentBuilder

```typescript
// Default allowed tools for subagents — read-only + bash + attempt_completion
const SUBAGENT_DEFAULT_ALLOWED_TOOLS = [
    FILE_READ, LIST_FILES, SEARCH, LIST_CODE_DEF,
    BASH, USE_SKILL, ATTEMPT
]
```

Subagents:
- Get their own `ApiHandler` (can use a different model via `modelId` override)
- Get a stripped-down system prompt with a subagent suffix instruction
- Cannot modify files (by default, unless explicitly configured)
- Use `backgroundExec` mode for commands (no VS Code terminal)

### SubagentRunner

`SubagentRunner.run(prompt, onProgress)` — Independent agent loop:

1. Builds conversation with initial user message
2. Streams LLM responses, handles tool calls
3. Manages its own `ContextManager` for context window
4. Reports progress via callback: `{ stats, latestToolCall, status, result }`
5. Terminates on `attempt_completion` or max retries

Key differences from main loop:
- No user approval — all tool calls auto-approved
- No checkpoints
- No UI messages (suppressed `say`)
- Independent context management with proactive compaction

### Custom Agent Configs

Users can define custom subagent configurations in `~/Documents/Cline/Agents/*.md`:

```yaml
---
name: SecurityAuditor
description: Reviews code for security vulnerabilities
modelId: anthropic/claude-sonnet-4
tools: [read_file, search_files, list_files]
skills: [security-review]
---
You are a security auditor...
```

These are loaded by `AgentConfigLoader` (singleton) and registered as dynamic tools.

---

## 12. Unique Features

### Checkpoints System

`src/integrations/checkpoints/` — Git-based state snapshots without interfering with user's repo.

- **Shadow git repository** — Creates an isolated `.git` directory for tracking changes
- **Per-workspace hashing** — Each workspace gets a unique shadow git identified by path hash
- **Checkpoint on tool completion** — Saves snapshot after each significant tool execution
- **Restore to any checkpoint** — User can roll back to any previous state
- **Multi-file diff view** — Shows all changes between checkpoints in a multi-file diff editor
- **Safe directory detection** — Prevents checkpoints in sensitive directories (home, desktop)

### Focus Chain (Progress Tracking)

`src/core/task/focus-chain/` — A todo/checklist system:

- Model maintains a `task_progress` checklist via the `focus_chain` tool parameter
- Displayed in the UI as a progress indicator
- File watcher detects external changes to the focus chain file
- Used by `summarize_task` to preserve progress across context compaction

### @Mentions System

`src/core/mentions/index.ts` — Rich content injection via `@` syntax:

| Mention | Resolves To |
|---------|-------------|
| `@/path/to/file` | File contents injected into context |
| `@/path/to/dir/` | Directory listing |
| `@http://...` | URL content fetched via headless browser |
| `@problems` | VS Code diagnostics panel content |
| `@terminal` | Latest terminal output |
| `@git-changes` | Working directory changes |
| `@<commit-hash>` | Git commit info |
| `@workspace:path` | Multi-root workspace file reference |

### Hooks System

`src/core/hooks/` — User-defined lifecycle hooks (shell scripts):

| Hook | Trigger |
|------|---------|
| `TaskStart` | When a new task begins |
| `TaskResume` | When a task is resumed |
| `TaskCancel` | When a task is cancelled |
| `PreToolUse` | Before each tool execution (post-approval) |
| `PostToolUse` | After each tool execution |
| `PreCompact` | Before context window truncation |
| `UserPromptSubmit` | When user submits a message |

Hooks can:
- Cancel the operation (`{ cancel: true }`)
- Inject context into the conversation (`{ contextModification: "..." }`)
- Stream output to the UI (`hook_output_stream`)

### Rules System

Multiple rule sources, all loaded into the system prompt:

- **Global .clinerules** — `~/.cline/rules/` (global to all workspaces)
- **Local .clinerules** — `.clinerules/` in workspace root
- **Cursor rules** — `.cursor/rules/` (compatibility)
- **Windsurf rules** — `.windsurfrules` (compatibility)
- **AGENTS.md** — Agent-specific rules
- **Conditional rules** — Rules with frontmatter conditions (file globs, project type)

### Skills

`src/core/context/instructions/user-instructions/skills.ts` — Auto-invoked knowledge modules:

- Discovered from `.clinerules/skills/` directories
- Matched to context by file globs or project type
- Loaded via `use_skill` tool when needed

### Explain Changes

The `generate_explanation` tool lets users get AI-generated inline explanations for checkpoint diffs — comments added directly to the diff view.

### Multi-Root Workspace Support

Full support for VS Code multi-root workspaces:
- `@workspace:name` mention syntax
- `WorkspaceRootManager` tracks all roots
- VCS type detection per root
- Workspace-scoped state via `WorkspaceStateManager`

### Plan/Act Mode

Dual-mode operation:
- **Plan mode** — Model can only read and respond, cannot modify files
- **Act mode** — Full tool access
- Strict plan mode blocks file modification tools entirely
- Separate provider/model configuration per mode

---

## Summary: Key Architectural Decisions

1. **Monolithic Task class** — The 3,547-line `Task` class contains the entire agent loop, making it the single point of truth but also a maintenance challenge.

2. **gRPC-over-postMessage** — Using protobuf for type safety while tunneling over VS Code's message passing gives them cross-platform type generation (Go, Java packages defined in proto options) without actual gRPC networking.

3. **Shadow git for checkpoints** — Clever approach to version control without touching the user's repo. Uses git's own diffing/restore capabilities.

4. **40+ providers with no abstraction layer** — Each provider is a standalone handler with provider-specific options. No shared base class or adapter pattern beyond the `ApiHandler` interface.

5. **Dual XML/native tool modes** — Supporting both XML-parsed tools and native function calling adds complexity but enables compatibility with models that lack structured output.

6. **Task-scoped everything** — Each new Task creates fresh instances of ContextManager, BrowserSession, CheckpointManager, etc. No resource pooling.

7. **Single active task** — Controller holds at most one Task. No concurrent task execution at the extension level (subagents run within the task).
