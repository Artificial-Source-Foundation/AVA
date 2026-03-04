# Continue — Backend Architecture Analysis

> IDE-agnostic AI coding assistant (VS Code + JetBrains). ~26k GitHub stars.
> License: Apache-2.0. TypeScript monorepo.

---

## Table of Contents

1. [Project Structure](#1-project-structure)
2. [IDE-Agnostic Core](#2-ide-agnostic-core)
3. [3-Process Model](#3-3-process-model)
4. [Protocol Definitions](#4-protocol-definitions)
5. [Tools](#5-tools)
6. [Agent Loop](#6-agent-loop)
7. [LLM Providers](#7-llm-providers)
8. [Context Providers](#8-context-providers)
9. [Autocomplete System](#9-autocomplete-system)
10. [Context / Token Management](#10-context--token-management)
11. [Configuration](#11-configuration)
12. [Unique Features](#12-unique-features)

---

## 1. Project Structure

### Top-Level Layout

```
continue/
├── core/                    # IDE-agnostic backend (~70K lines, ~464 .ts files)
├── extensions/
│   ├── vscode/              # VS Code extension (primary)
│   ├── intellij/            # JetBrains plugin (Kotlin)
│   └── cli/                 # CLI interface
├── gui/                     # React webview UI (shared between IDEs)
├── packages/
│   ├── config-yaml/         # YAML config schema + loader
│   ├── config-types/        # Shared config type definitions
│   ├── openai-adapters/     # Unified OpenAI-compatible API layer
│   ├── llm-info/            # Model metadata (context lengths, capabilities)
│   ├── fetch/               # Fetch wrapper with request options
│   ├── terminal-security/   # Terminal command security evaluation
│   ├── hub/                 # Continue Hub client
│   └── continue-sdk/        # SDK for external integrations
├── binary/                  # Standalone binary build target
├── actions/                 # GitHub Actions
├── docs/                    # Documentation site
├── eval/                    # Evaluation harness
├── skills/                  # Skills (cn-check)
├── scripts/                 # Build/release scripts
└── sync/                    # Sync utilities
```

### Key Architectural Insight

The project is a **monorepo with no build orchestrator** (no Nx, Turborepo, or pnpm workspaces). Instead, `core/` is consumed directly via TypeScript path aliases (`import { ... } from "core/..."`) by the extensions. The `packages/` directory contains independently publishable npm packages linked via `file:` references in `core/package.json`.

---

## 2. IDE-Agnostic Core

**Path:** `core/`

The entire backend lives in `core/` with zero imports from `vscode`, JetBrains APIs, or any IDE-specific module. The core communicates with IDEs exclusively through two abstractions:

### The `IDE` Interface

**File:** `core/index.d.ts` (line 831)

This is the central abstraction that makes Continue IDE-agnostic. Every IDE (VS Code, JetBrains, CLI) must implement this interface:

```typescript
export interface IDE {
  // Workspace
  getIdeInfo(): Promise<IdeInfo>;
  getIdeSettings(): Promise<IdeSettings>;
  getWorkspaceDirs(): Promise<string[]>;

  // File Operations
  readFile(fileUri: string): Promise<string>;
  writeFile(path: string, contents: string): Promise<void>;
  removeFile(path: string): Promise<void>;
  fileExists(fileUri: string): Promise<boolean>;
  openFile(path: string): Promise<void>;
  saveFile(fileUri: string): Promise<void>;
  readRangeInFile(fileUri: string, range: Range): Promise<string>;
  showLines(fileUri: string, startLine: number, endLine: number): Promise<void>;
  showVirtualFile(title: string, contents: string): Promise<void>;
  getOpenFiles(): Promise<string[]>;
  getCurrentFile(): Promise<{ isUntitled: boolean; path: string; contents: string } | undefined>;
  getPinnedFiles(): Promise<string[]>;

  // Search
  getSearchResults(query: string, maxResults?: number): Promise<string>;
  getFileResults(pattern: string, maxResults?: number): Promise<string[]>;

  // Terminal / Shell
  runCommand(command: string, options?: TerminalOptions): Promise<void>;
  subprocess(command: string, cwd?: string): Promise<[string, string]>;
  getTerminalContents(): Promise<string>;

  // Git
  getDiff(includeUnstaged: boolean): Promise<string[]>;
  getBranch(dir: string): Promise<string>;
  getRepoName(dir: string): Promise<string | undefined>;
  getGitRootPath(dir: string): Promise<string | undefined>;

  // Directory
  listDir(dir: string): Promise<[string, FileType][]>;
  getFileStats(files: string[]): Promise<FileStatsMap>;

  // Diagnostics / Debug
  getProblems(fileUri?: string): Promise<Problem[]>;
  getDebugLocals(threadIndex: number): Promise<string>;
  getTopLevelCallStackSources(threadIndex: number, stackDepth: number): Promise<string[]>;
  getAvailableThreads(): Promise<Thread[]>;

  // LSP
  gotoDefinition(location: Location): Promise<RangeInFile[]>;
  gotoTypeDefinition(location: Location): Promise<RangeInFile[]>;
  getSignatureHelp(location: Location): Promise<SignatureHelp | null>;
  getReferences(location: Location): Promise<RangeInFile[]>;
  getDocumentSymbols(textDocumentIdentifier: string): Promise<DocumentSymbol[]>;

  // Secrets
  readSecrets(keys: string[]): Promise<Record<string, string>>;
  writeSecrets(secrets: { [key: string]: string }): Promise<void>;

  // UI
  openUrl(url: string): Promise<void>;
  showToast(type: ToastType, message: string, ...otherParams: any[]): Promise<any>;

  // Identity
  getUniqueId(): Promise<string>;
  isTelemetryEnabled(): Promise<boolean>;
  isWorkspaceRemote(): Promise<boolean>;
  getTags(artifactId: string): Promise<IndexTag[]>;
  getClipboardContent(): Promise<{ text: string; copiedAt: string }>;

  // Callbacks
  onDidChangeActiveTextEditor(callback: (fileUri: string) => void): void;
}
```

### IDE Implementations

| Implementation | File | Notes |
|---|---|---|
| VS Code | `extensions/vscode/src/VsCodeIde.ts` | Direct `vscode` API calls |
| JetBrains | `extensions/intellij/.../IntelliJIde.kt` | Kotlin, bridges to JVM |
| Core (Message-based) | `core/protocol/messenger/messageIde.ts` | Wraps IDE calls as messages |

The `MessageIde` class is particularly clever -- it implements the `IDE` interface by forwarding every method call as a typed message through the messenger protocol. This is how Core communicates with the IDE without importing any IDE-specific code:

```typescript
// core/protocol/messenger/messageIde.ts
export class MessageIde implements IDE {
  constructor(
    private readonly request: <T extends keyof ToIdeFromWebviewOrCoreProtocol>(
      messageType: T,
      data: ToIdeFromWebviewOrCoreProtocol[T][0],
    ) => Promise<ToIdeFromWebviewOrCoreProtocol[T][1]>,
    ...
  ) {}

  async readFile(fileUri: string): Promise<string> {
    return await this.request("readFile", { filepath: fileUri });
  }
  // ... every IDE method is a message request
}
```

---

## 3. 3-Process Model

Continue uses a 3-process architecture: **Core**, **Extension (IDE)**, and **GUI (Webview)**.

### Communication Topology

```
┌─────────────────────────────────────────────┐
│                 IDE Extension                │
│  (VS Code / JetBrains / CLI)                │
│                                             │
│  Implements IDE interface                   │
│  Hosts webview                              │
│  Routes messages between Core and GUI       │
└──────────┬──────────────┬───────────────────┘
           │              │
    InProcessMessenger    │  postMessage (webview)
    (same Node.js proc)   │  (serialized JSON)
           │              │
           ▼              ▼
┌──────────────┐   ┌──────────────────────┐
│    Core      │   │    GUI (Webview)      │
│              │   │                      │
│  Agent loop  │   │  React app           │
│  LLM calls   │   │  Chat UI             │
│  Tools       │   │  Settings            │
│  Indexing    │   │  History              │
│  Config      │   │  Model selection     │
└──────────────┘   └──────────────────────┘
```

### VS Code (In-Process)

In VS Code, Core runs **in the same Node.js process** as the extension. Communication uses the `InProcessMessenger`:

```typescript
// core/protocol/messenger/index.ts
export class InProcessMessenger<ToProtocol, FromProtocol>
  implements IMessenger<ToProtocol, FromProtocol> {

  protected myTypeListeners = new Map<keyof ToProtocol, (message: Message) => any>();
  protected externalTypeListeners = new Map<keyof FromProtocol, (message: Message) => any>();

  invoke<T extends keyof ToProtocol>(messageType: T, data: ...): ... {
    const listener = this.myTypeListeners.get(messageType);
    return listener(msg);  // Direct function call, no serialization
  }
}
```

The `VsCodeMessenger` (`extensions/vscode/src/extension/VsCodeMessenger.ts`) orchestrates all three processes:
- It extends `InProcessMessenger` for Core communication
- It uses `webviewProtocol.postMessage()` for GUI communication
- It registers pass-through handlers that forward messages between GUI and Core

### JetBrains (Binary Process)

For JetBrains, Core runs as a **separate binary process** (`binary/`). The Kotlin extension communicates with it via a different transport (likely IPC/stdio), but the protocol types are identical.

### Message Pass-Through

A key pattern: the Extension acts as a **router**. Many messages from the GUI need to reach Core and vice versa. These are listed in explicit pass-through arrays:

```typescript
// core/protocol/passThrough.ts
export const WEBVIEW_TO_CORE_PASS_THROUGH = [
  "ping", "abort", "history/list", "history/save",
  "llm/streamChat", "tools/call", "autocomplete/complete",
  "context/getContextItems", "mcp/reloadServer",
  // ... 90+ message types
];

export const CORE_TO_WEBVIEW_PASS_THROUGH = [
  "configUpdate", "indexProgress", "indexing/statusUpdate",
  "addContextItem", "refreshSubmenuItems", "toolCallPartialOutput",
  // ... 14 message types
];
```

---

## 4. Protocol Definitions

**Path:** `core/protocol/`

The protocol system is the backbone of Continue's architecture. It uses **TypeScript type-level protocol definitions** -- each message type maps to a `[RequestData, ResponseData]` tuple.

### Protocol Files

| File | Direction | Purpose |
|---|---|---|
| `core.ts` | GUI/IDE -> Core | All core operations (chat, tools, config, indexing) |
| `ide.ts` | Core/GUI -> IDE | IDE operations (file I/O, git, LSP, terminal) |
| `webview.ts` | Core/IDE -> GUI | UI updates (config, indexing progress, context) |
| `coreWebview.ts` | GUI -> Core (direct) | Profile/org selection |
| `ideCore.ts` | IDE -> Core (direct) | Re-exports of shared protocol |
| `ideWebview.ts` | GUI -> IDE (direct) | IDE-specific actions (apply, diff, focus) |
| `passThrough.ts` | Router config | Lists which messages to pass through |
| `messenger/index.ts` | Transport | Message format and `InProcessMessenger` |
| `messenger/messageIde.ts` | Adapter | IDE interface over messages |

### Base Protocol Type

```typescript
// core/protocol/index.ts
export type IProtocol = Record<string, [any, any]>;
```

### Message Format

```typescript
// core/protocol/messenger/index.ts
export interface Message<T = any> {
  messageType: string;
  messageId: string;  // UUID for request/response correlation
  data: T;
}
```

### Messenger Interface

```typescript
export interface IMessenger<ToProtocol, FromProtocol> {
  send<T extends keyof FromProtocol>(messageType: T, data: FromProtocol[T][0], messageId?: string): string;
  on<T extends keyof ToProtocol>(messageType: T, handler: (message: Message<ToProtocol[T][0]>) => Promise<ToProtocol[T][1]>): void;
  request<T extends keyof FromProtocol>(messageType: T, data: FromProtocol[T][0]): Promise<FromProtocol[T][1]>;
  invoke<T extends keyof ToProtocol>(messageType: T, data: ToProtocol[T][0], messageId?: string): ToProtocol[T][1];
  onError(handler: (message: Message, error: Error) => void): void;
}
```

### Key Protocol Messages (Core)

```typescript
// core/protocol/core.ts -- ToCoreFromIdeOrWebviewProtocol
{
  // Chat
  "llm/streamChat": [{ messages, completionOptions, title }, AsyncGenerator<ChatMessage, PromptLog>],
  "llm/complete": [{ prompt, completionOptions, title }, string],

  // Tools
  "tools/call": [{ toolCall: ToolCall }, { contextItems, errorMessage?, mcpUiState? }],
  "tools/evaluatePolicy": [{ toolName, basePolicy, parsedArgs }, { policy, displayValue? }],
  "tools/preprocessArgs": [{ toolName, args }, { preprocessedArgs?, errorReason? }],

  // Context
  "context/getContextItems": [{ name, query, fullInput, selectedCode }, ContextItemWithId[]],
  "context/loadSubmenuItems": [{ title }, ContextSubmenuItem[]],

  // History
  "history/list": [{ offset?, limit? }, SessionMetadata[]],
  "history/load": [{ id }, Session],
  "history/save": [Session, void],

  // Autocomplete
  "autocomplete/complete": [AutocompleteInput, string[]],
  "autocomplete/accept": [{ completionId }, void],
  "autocomplete/cancel": [undefined, void],

  // Indexing
  "index/forceReIndex": [{ dirs?, shouldClearIndexes? } | undefined, void],
  "index/setPaused": [boolean, void],

  // Config
  "config/getSerializedProfileInfo": [undefined, { result, profileId, organizations, selectedOrgId }],
  "config/refreshProfiles": [{ reason?, selectOrgId? } | undefined, void],
  "config/updateSelectedModel": [{ profileId, role, title }, GlobalContextModelSelections],

  // Conversation
  "conversation/compact": [{ index, sessionId }, string | undefined],
}
```

---

## 5. Tools

### Tool Interface

**File:** `core/index.d.ts` (line 1133)

```typescript
export interface Tool {
  type: "function";
  function: {
    name: string;
    description?: string;
    parameters?: Record<string, any>;  // JSON Schema
  };
  displayTitle: string;
  wouldLikeTo?: string;           // "read {{{ filepath }}}" -- Handlebars template
  isCurrently?: string;           // "reading {{{ filepath }}}"
  hasAlready?: string;            // "read {{{ filepath }}}"
  readonly: boolean;
  isInstant?: boolean;            // Can execute without waiting
  uri?: string;                   // HTTP or MCP URI for external tools
  faviconUrl?: string;
  group: string;                  // "Built-In" or MCP server name
  defaultToolPolicy?: ToolPolicy; // "allowedWithoutPermission" | "allowedWithPermission" | "disabled"
  toolCallIcon?: string;
  preprocessArgs?: (args, extras) => Promise<Record<string, unknown>>;
  evaluateToolCallPolicy?: (basePolicy, parsedArgs, processedArgs?) => ToolPolicy;
  systemMessageDescription?: {
    prefix: string;
    exampleArgs?: Array<[string, string | number]>;
  };
  mcpMeta?: McpToolMeta;
}
```

### Built-In Tools

**Path:** `core/tools/definitions/` (definitions) and `core/tools/implementations/` (implementations)

| Tool Name | File | Description | Read-Only | Default Policy |
|---|---|---|---|---|
| `read_file` | `readFile.ts` | Read contents of an existing file | Yes | allowedWithoutPermission |
| `read_file_range` | `readFileRange.ts` | Read specific line range from a file (experimental) | Yes | allowedWithoutPermission |
| `read_currently_open_file` | `readCurrentlyOpenFile.ts` | Read the file currently active in the IDE editor | Yes | allowedWithPermission |
| `edit_existing_file` | `editFile.ts` | Edit file using "lazy" diff format (non-agent models) | No | allowedWithPermission |
| `single_find_and_replace` | `singleFindAndReplace.ts` | Exact string find-and-replace in a file (non-agent models) | No | allowedWithPermission |
| `multi_edit` | `multiEdit.ts` | Multiple find-and-replace operations in one call (agent models) | No | allowedWithPermission |
| `create_new_file` | `createNewFile.ts` | Create a new file with contents | No | allowedWithPermission |
| `run_terminal_command` | `runTerminalCommand.ts` | Execute shell commands in the IDE terminal | No | allowedWithPermission |
| `grep_search` | `grepSearch.ts` | Regex search across repository using ripgrep | Yes | allowedWithoutPermission |
| `file_glob_search` | `globSearch.ts` | Find files by glob pattern recursively | Yes | allowedWithoutPermission |
| `search_web` | `searchWeb.ts` | Web search (signed-in users only) | Yes | allowedWithoutPermission |
| `fetch_url_content` | `fetchUrlContent.ts` | Fetch and convert webpage content | Yes | allowedWithPermission |
| `view_diff` | `viewDiff.ts` | View current git working changes | Yes | allowedWithoutPermission |
| `ls` | `ls.ts` | List files and folders in a directory | Yes | allowedWithoutPermission |
| `create_rule_block` | `createRuleBlock.ts` | Create a persistent rule for future conversations | No | disabled |
| `request_rule` | `requestRule.ts` | Retrieve an agent-requested rule by name | No | disabled |
| `read_skill` | `readSkill.ts` | Read a skill's instructions by name | Yes | - |
| `codebase` | `codebaseTool.ts` | Semantic codebase search (experimental) | Yes | allowedWithPermission |
| `view_repo_map` | `viewRepoMap.ts` | View repository structure map (experimental) | Yes | allowedWithPermission |
| `view_subdirectory` | `viewSubdirectory.ts` | View contents of a specific subdirectory (experimental) | Yes | allowedWithPermission |

### Tool Selection Logic

**File:** `core/tools/index.ts`

Tools are split into **base** (always available) and **config-dependent** (conditionally loaded):

```typescript
// Base tools -- always included
export const getBaseToolDefinitions = () => [
  readFileTool, createNewFileTool, runTerminalCommandTool,
  globSearchTool, viewDiffTool, readCurrentlyOpenFileTool,
  lsTool, createRuleBlock, fetchUrlContentTool,
];

// Config-dependent
export const getConfigDependentToolDefinitions = async (params) => {
  const tools = [];
  tools.push(requestRuleTool(params), readSkillTool(params));

  if (isSignedIn) tools.push(searchWebTool);
  if (enableExperimentalTools) tools.push(viewRepoMapTool, viewSubdirectoryTool, codebaseTool, readFileRangeTool);

  // Model-dependent: recommended agent models get multi_edit, others get edit + find-and-replace
  if (isRecommendedAgentModel(modelName)) {
    tools.push(multiEditTool);
  } else {
    tools.push(editFileTool, singleFindAndReplaceTool);
  }

  if (!isRemote) tools.push(grepSearchTool);
  return tools;
};
```

### Tool Execution Flow

**File:** `core/tools/callTool.ts`

```
Tool call arrives
  ├── Has URI? → callToolFromUri()
  │     ├── http/https → HTTP POST to external endpoint
  │     └── mcp:// → MCPManagerSingleton → client.callTool()
  └── No URI → callBuiltInTool()
        └── Switch on tool name → specific implementation
```

### Tool Policy System

**Files:** `core/tools/policies/fileAccess.ts`, `packages/terminal-security/`

Continue has a 3-tier permission model for tools:

1. **`allowedWithoutPermission`** -- Auto-approved (read-only tools within workspace)
2. **`allowedWithPermission`** -- Requires user confirmation
3. **`disabled`** -- Tool is not available

Policies can be **dynamically evaluated** per-call:
- File access tools check `isWithinWorkspace` -- outside workspace always requires permission
- Terminal commands go through `evaluateTerminalCommandSecurity()` which analyzes the command
- Users can override policies via `toolOverrides` in config

### MCP Tool Integration

MCP tools are integrated via URIs: `mcp://{serverId}/{toolName}`. The `callToolFromUri` function decodes the URI and calls the MCP client:

```typescript
// core/tools/callTool.ts
case "mcp:":
  const [mcpId, toolName] = decodeMCPToolUri(uri);
  const client = MCPManagerSingleton.getInstance().getConnection(mcpId);
  const response = await client.client.callTool({ name: toolName, arguments: args });
```

---

## 6. Agent Loop

Continue does **not** have a traditional agentic loop in the core. The agent loop is **GUI-driven**:

### Architecture

The GUI (webview) drives the conversation cycle:
1. User sends a message
2. GUI sends `llm/streamChat` to Core
3. Core streams back `ChatMessage` chunks (which may include tool calls)
4. GUI detects tool calls in the response
5. GUI sends `tools/call` to Core for each tool
6. Core executes the tool, returns context items
7. GUI appends tool results to the conversation
8. GUI sends another `llm/streamChat` with the updated conversation
9. Repeat until no more tool calls

### The Core Class

**File:** `core/core.ts` (~1,540 lines)

The `Core` class is the main orchestrator. It registers message handlers for every protocol message and delegates to subsystems:

```typescript
export class Core {
  configHandler: ConfigHandler;
  codeBaseIndexer: CodebaseIndexer;
  completionProvider: CompletionProvider;
  nextEditProvider: NextEditProvider;
  private docsService: DocsService;

  constructor(
    private readonly messenger: IMessenger<ToCoreProtocol, FromCoreProtocol>,
    private readonly ide: IDE,
  ) {
    // Initialize subsystems
    this.configHandler = new ConfigHandler(this.ide, ...);
    this.codeBaseIndexer = new CodebaseIndexer(this.configHandler, this.ide, ...);
    this.completionProvider = new CompletionProvider(this.configHandler, ide, ...);
    this.registerMessageHandlers();
  }
}
```

### Streaming Chat

**File:** `core/llm/streamChat.ts`

```typescript
export async function* llmStreamChat(
  configHandler, abortController, msg, ide, messenger,
): AsyncGenerator<ChatMessage, PromptLog> {
  const model = config.selectedModelByRole.chat;

  // Handle legacy slash commands
  if (legacySlashCommandData) {
    const gen = slashCommand.run({ input, history: messages, llm: model, ... });
    // Yield each chunk
  } else {
    // Standard streaming
    const gen = model.streamChat(messages, abortController.signal, completionOptions);
    let next = await gen.next();
    while (!next.done) {
      if (abortController.signal.aborted) break;
      yield next.value;
      next = await gen.next();
    }
    return next.value;  // PromptLog
  }
}
```

### Abort Handling

Each streaming message gets a unique `AbortController` tracked by message ID:

```typescript
private messageAbortControllers = new Map<string, AbortController>();

on("abort", (msg) => {
  this.abortById(msg.data ?? msg.messageId);
});
```

---

## 7. LLM Providers

### Base Class

**File:** `core/llm/index.ts` -- `BaseLLM` class (~800 lines)

All providers extend `BaseLLM`, which implements the `ILLM` interface:

```typescript
export interface ILLM {
  providerName: string;
  model: string;
  contextLength: number;

  // Core methods
  complete(prompt: string, signal: AbortSignal, options?): Promise<string>;
  streamComplete(prompt: string, signal: AbortSignal, options?): AsyncGenerator<string, PromptLog>;
  streamFim(prefix: string, suffix: string, signal: AbortSignal, options?): AsyncGenerator<string, PromptLog>;
  streamChat(messages: ChatMessage[], signal: AbortSignal, options?): AsyncGenerator<ChatMessage, PromptLog>;
  chat(messages: ChatMessage[], signal: AbortSignal, options?): Promise<ChatMessage>;

  // Embeddings and reranking
  embed(chunks: string[]): Promise<number[][]>;
  rerank(query: string, chunks: Chunk[]): Promise<number[]>;

  // Utilities
  countTokens(text: string): number;
  supportsImages(): boolean;
  supportsFim(): boolean;
  supportsCompletions(): boolean;
  supportsPrefill(): boolean;
  listModels(): Promise<string[]>;
  compileChatMessages(messages, options): CompiledChatMessagesReport;
}
```

### Provider Registry

**File:** `core/llm/llms/index.ts`

Providers are registered as a flat array of class references (`LLMClasses`). Resolution is by `providerName` string match:

```typescript
export const LLMClasses = [Anthropic, Cohere, Gemini, Ollama, OpenAI, ...];

export async function llmFromDescription(desc, ...): Promise<BaseLLM | undefined> {
  const cls = LLMClasses.find((llm) => llm.providerName === desc.provider);
  return new cls(options);
}
```

### All Providers (54 total)

| Provider | Class | Notes |
|---|---|---|
| Anthropic | `Anthropic.ts` | Direct API |
| OpenAI | `OpenAI.ts` | Direct API |
| Azure | `Azure.ts` | Azure OpenAI |
| Google Gemini | `Gemini.ts` | Google AI Studio |
| Ollama | `Ollama.ts` | Local models |
| Bedrock | `Bedrock.ts` | AWS Bedrock |
| SageMaker | `SageMaker.ts` | AWS SageMaker |
| VertexAI | `VertexAI.ts` | Google Cloud |
| OpenRouter | `OpenRouter.ts` | Multi-provider router |
| Together | `Together.ts` | Together AI |
| Fireworks | `Fireworks.ts` | Fireworks AI |
| Groq | `Groq.ts` | Groq |
| Mistral | `Mistral.ts` | Mistral AI |
| Deepseek | `Deepseek.ts` | DeepSeek |
| Cohere | `Cohere.ts` | Cohere |
| LMStudio | `LMStudio.ts` | Local models |
| Llamafile | `Llamafile.ts` | Local models |
| LlamaCpp | `LlamaCpp.ts` | Local models |
| HuggingFace TGI | `HuggingFaceTGI.ts` | HF Text Generation Inference |
| HuggingFace Inference | `HuggingFaceInferenceAPI.ts` | HF Inference API |
| HuggingFace TEI | `HuggingFaceTEI.ts` | Embeddings only |
| Replicate | `Replicate.ts` | Replicate |
| Cloudflare | `Cloudflare.ts` | Workers AI |
| Nvidia | `Nvidia.ts` | NVIDIA NIM |
| DeepInfra | `DeepInfra.ts` | DeepInfra |
| SambaNova | `SambaNova.ts` | SambaNova |
| Cerebras | `Cerebras.ts` | Cerebras |
| Nebius | `Nebius.ts` | Nebius |
| Nous | `Nous.ts` | Nous Research |
| xAI | `xAI.ts` | xAI/Grok |
| Venice | `Venice.ts` | Venice AI |
| Vllm | `Vllm.ts` | vLLM server |
| WatsonX | `WatsonX.ts` | IBM watsonx |
| TextGenWebUI | `TextGenWebUI.ts` | text-generation-webui |
| Kindo | `Kindo.ts` | Kindo |
| Moonshot | `Moonshot.ts` | Moonshot AI |
| Docker | `Docker.ts` | Docker Model Runner |
| Msty | `Msty.ts` | Msty |
| Flowise | `Flowise.ts` | Flowise |
| Scaleway | `Scaleway.ts` | Scaleway |
| SiliconFlow | `SiliconFlow.ts` | SiliconFlow |
| Novita | `Novita.ts` | Novita AI |
| NCompass | `NCompass.ts` | NCompass |
| OVHcloud | `OVHcloud.ts` | OVHcloud |
| Lemonade | `Lemonade.ts` | Local Qualcomm NPU |
| Inception | `Inception.ts` | Inception |
| Voyage | `Voyage.ts` | Embeddings/reranking |
| LlamaStack | `LlamaStack.ts` | Meta Llama Stack |
| Relace | `Relace.ts` | Fast apply model |
| TARS | `TARS.ts` | TARS |
| zAI | `zAI.ts` | zAI |
| Asksage | `Asksage.ts` | AskSage |
| ContinueProxy | `stubs/ContinueProxy.ts` | Continue's managed proxy |
| FunctionNetwork | `FunctionNetwork.ts` | Function Network |
| CometAPI | `CometAPI.ts` | Comet ML |

### OpenAI Adapter Layer

**File:** `packages/openai-adapters/`

Most providers use a shared OpenAI-compatible adapter layer rather than implementing raw HTTP calls:

```typescript
// In BaseLLM constructor:
this.openaiAdapter = constructLlmApi({
  provider: this.providerName,
  apiKey: this.apiKey,
  apiBase: this.apiBase,
  requestOptions: this.requestOptions,
});
```

### Model Roles

Models are assigned to roles (from `@continuedev/config-yaml`):

```typescript
type ModelRole = "chat" | "autocomplete" | "embed" | "rerank" | "edit" | "apply";
```

The config maintains both `modelsByRole` (all available) and `selectedModelByRole` (currently active).

---

## 8. Context Providers

### Architecture

**Path:** `core/context/`

Context providers are a key differentiating feature. They supply additional context to the LLM by responding to `@mentions` in the chat input (e.g., `@file`, `@codebase`, `@docs`).

### Interface

```typescript
export interface IContextProvider {
  description: ContextProviderDescription;
  getContextItems(query: string, extras: ContextProviderExtras): Promise<ContextItem[]>;
  loadSubmenuItems(args: LoadSubmenuItemsArgs): Promise<ContextSubmenuItem[]>;
  deprecationMessage: string | null;
}

export interface ContextProviderDescription {
  title: ContextProviderName;      // e.g., "file", "codebase", "docs"
  displayTitle: string;
  description: string;
  type: "normal" | "query" | "submenu";
  dependsOnIndexing?: ("chunk" | "embeddings" | "fullTextSearch" | "codeSnippets")[];
}

export interface ContextProviderExtras {
  config: ContinueConfig;
  fullInput: string;
  embeddingsProvider: ILLM | null;
  reranker: ILLM | null;
  llm: ILLM;
  ide: IDE;
  selectedCode: RangeInFile[];
  fetch: FetchFunction;
  isInAgentMode: boolean;
}
```

### All Context Providers (30)

**File:** `core/context/providers/index.ts`

| Provider | Class | `@` Trigger | Type | Description |
|---|---|---|---|---|
| FileContextProvider | `FileContextProvider.ts` | `@file` | submenu | Specific file contents |
| FolderContextProvider | `FolderContextProvider.ts` | `@folder` | submenu | Entire folder contents |
| CodebaseContextProvider | `CodebaseContextProvider.ts` | `@codebase` | query | Semantic codebase search (embeddings) |
| CodeContextProvider | `CodeContextProvider.ts` | `@code` | submenu | Code symbols / snippets |
| DocsContextProvider | `DocsContextProvider.ts` | `@docs` | submenu | Indexed documentation sites |
| DiffContextProvider | `DiffContextProvider.ts` | `@diff` | normal | Current git diff |
| CurrentFileContextProvider | `CurrentFileContextProvider.ts` | `@currentFile` | normal | Active file in editor |
| OpenFilesContextProvider | `OpenFilesContextProvider.ts` | `@open` | normal | All open editor tabs |
| FileTreeContextProvider | `FileTreeContextProvider.ts` | `@tree` | normal | Full file tree |
| TerminalContextProvider | `TerminalContextProvider.ts` | `@terminal` | normal | Terminal output |
| ProblemsContextProvider | `ProblemsContextProvider.ts` | `@problems` | normal | IDE diagnostic problems |
| SearchContextProvider | `SearchContextProvider.ts` | `@search` | query | IDE text search results |
| URLContextProvider | `URLContextProvider.ts` | `@url` | query | Fetch and parse URL |
| WebContextProvider | `WebContextProvider.ts` | `@web` | query | Web search results |
| GitHubIssuesContextProvider | `GitHubIssuesContextProvider.ts` | `@issue` | submenu | GitHub issues |
| GitLabMergeRequestContextProvider | `GitLabMergeRequestContextProvider.ts` | `@gitlab-mr` | submenu | GitLab merge requests |
| JiraIssuesContextProvider | `JiraIssuesContextProvider/` | `@jira` | submenu | Jira issues |
| GoogleContextProvider | `GoogleContextProvider.ts` | `@google` | query | Google search |
| DiscordContextProvider | `DiscordContextProvider.ts` | `@discord` | query | Discord messages |
| GreptileContextProvider | `GreptileContextProvider.ts` | `@greptile` | query | Greptile code search |
| PostgresContextProvider | `PostgresContextProvider.ts` | `@postgres` | query | PostgreSQL query results |
| DatabaseContextProvider | `DatabaseContextProvider.ts` | `@database` | query | Generic database query |
| DebugLocalsProvider | `DebugLocalsProvider.ts` | `@debugger` | normal | Debugger local variables |
| OSContextProvider | `OSContextProvider.ts` | `@os` | normal | Operating system info |
| RepoMapContextProvider | `RepoMapContextProvider.ts` | `@repo-map` | normal | Repository structure map |
| HttpContextProvider | `HttpContextProvider.ts` | (custom) | query | Custom HTTP endpoint |
| MCPContextProvider | `MCPContextProvider.ts` | (dynamic) | - | MCP server resources/prompts |
| ContinueProxyContextProvider | `ContinueProxyContextProvider.ts` | (proxy) | - | Continue Hub proxy |
| GitCommitContextProvider | `GitCommitContextProvider.ts` | `@commit` | submenu | Git commit details |
| ClipboardContextProvider | `ClipboardContextProvider.ts` | `@clipboard` | normal | Clipboard content |
| RulesContextProvider | `RulesContextProvider.ts` | `@rules` | submenu | Project rules |

### Custom Context Providers

Users can define custom providers via config:

```typescript
export interface CustomContextProvider {
  title: string;
  displayTitle?: string;
  type?: ContextProviderType;
  getContextItems(query: string, extras: ContextProviderExtras): Promise<ContextItem[]>;
  loadSubmenuItems?(args: LoadSubmenuItemsArgs): Promise<ContextSubmenuItem[]>;
}
```

---

## 9. Autocomplete System

**Path:** `core/autocomplete/`

Continue has a sophisticated inline code completion system with its own pipeline.

### Pipeline Architecture

```
core/autocomplete/
├── CompletionProvider.ts        # Main entry point
├── classification/              # Multiline vs single-line decision
├── constants/                   # Configuration constants
├── context/                     # Context retrieval for completions
│   └── ContextRetrievalService.ts
├── filtering/                   # Bracket matching, validation
│   └── BracketMatchingService.ts
├── generation/                  # Streaming completion generation
│   └── CompletionStreamer.ts
├── postprocessing/              # Clean up completion output
├── prefiltering/                # Should we even complete? (debounce, security)
├── snippets/                    # Code snippet extraction
│   └── gitDiffCache.ts
├── templating/                  # Prompt construction
│   └── renderPromptWithTokenLimit.ts
├── util/
│   ├── AutocompleteDebouncer.ts
│   ├── AutocompleteLoggingService.ts
│   ├── AutocompleteLruCache.ts
│   └── HelperVars.ts
└── types.ts
```

### Completion Flow

```typescript
// core/autocomplete/CompletionProvider.ts
async provideInlineCompletionItems(input, token, force?) {
  // 1. Prepare LLM (validate, set temperature to 0.01)
  const llm = await this._prepareLlm();

  // 2. Security check (ignore sensitive files)
  if (isSecurityConcern(input.filepath)) return undefined;

  // 3. Debounce
  if (await this.debouncer.delayAndShouldDebounce(options.debounceDelay)) return undefined;

  // 4. Prefiltering (should we even complete here?)
  if (await shouldPrefilter(helper, this.ide)) return undefined;

  // 5. Gather snippets (context from open files, LSP, git diff, etc.)
  const [snippetPayload, workspaceDirs] = await Promise.all([
    getAllSnippetsWithoutRace({ helper, ide, getDefinitionsFromLsp, contextRetrievalService }),
    this.ide.getWorkspaceDirs(),
  ]);

  // 6. Render prompt within token limit
  const { prompt, prefix, suffix, completionOptions } = renderPromptWithTokenLimit({ ... });

  // 7. Check LRU cache
  const cachedCompletion = await cache.get(helper.prunedPrefix);

  // 8. Stream completion (if not cached)
  const completionStream = this.completionStreamer.streamCompletionWithFilters(
    token, llm, prefix, suffix, prompt, multiline, completionOptions, helper
  );
  for await (const update of completionStream) { completion += update; }

  // 9. Postprocess
  completion = postprocessCompletion({ completion, prefix, suffix, llm });

  // 10. Cache and return
  return outcome;
}
```

### Key Design Decisions

- **LRU Cache**: Completions are cached by pruned prefix to avoid redundant LLM calls
- **Debouncing**: Configurable delay to avoid API spam during fast typing
- **FIM (Fill-in-the-Middle)**: Supports both FIM and chat-based completions depending on model
- **Security**: Files matching sensitive patterns (`.env`, credentials) are excluded
- **Bracket Matching**: Tracks bracket balance to improve completion quality

---

## 10. Context / Token Management

### Token Counting

**File:** `core/llm/countTokens.ts`

Continue uses dual tokenizers:
- **GPT tokenizer** (`js-tiktoken`) for OpenAI models
- **Llama tokenizer** (custom) for open-source models
- Model auto-detection chooses the right tokenizer

```typescript
function countTokens(content: MessageContent, modelName = "llama2"): number {
  const encoding = encodingForModel(modelName);
  // Handles text, images (flat 1024 tokens), multipart content
  return getAdjustedTokenCountFromModel(baseTokens, modelName);
}
```

### Tool Token Counting

Tools consume context tokens. Continue counts them explicitly:

```typescript
function countToolsTokens(tools: Tool[], modelName: string): number {
  let numTokens = 12;  // base overhead
  for (const tool of tools) {
    let functionTokens = count(tool.function.name);
    functionTokens += count(tool.function.description);
    // + parameters, enum values, etc.
    numTokens += functionTokens;
  }
  return numTokens + 12;
}
```

### Message Compilation (Context Window Fitting)

**File:** `core/llm/countTokens.ts` -- `compileChatMessages()`

This is the core context management function. It reconciles messages with available context:

```
Available = contextLength - maxTokens - toolTokens - bufferSafety
```

Algorithm:
1. Convert images to text if model doesn't support images
2. Extract and preserve the system message
3. Remove empty messages
4. **Extract the last tool sequence** (assistant + tool responses) -- these are never pruned
5. Calculate non-negotiable token requirements (system + tools + last sequence)
6. **Prune older messages from the front** until within budget
7. Flatten adjacent same-role messages
8. Reassemble with system message first

Key constants:
```typescript
const MAX_TOKEN_SAFETY_BUFFER = 1000;
const TOKEN_SAFETY_PROPORTION = 0.02;  // 2% of context length
const MIN_RESPONSE_TOKENS = 1000;
```

### Conversation Compaction

**File:** `core/util/conversationCompaction.ts`

When conversations get long, users can compact them:

```typescript
export async function compactConversation({ sessionId, index, historyManager, currentModel }) {
  // 1. Load conversation up to index
  // 2. Find any existing summary
  // 3. Generate new summary using the LLM with a detailed compaction prompt
  // 4. Store summary in the session history at the target message
}
```

The compaction prompt asks the LLM to produce a structured summary covering:
- Conversation overview
- Active development state
- Technical stack
- File operations performed
- Solutions and troubleshooting
- Outstanding work

---

## 11. Configuration

### Config System Overview

Continue uses a dual config system that is migrating from JSON to YAML:

| Format | Path | Status |
|---|---|---|
| `config.json` | `~/.continue/config.json` | Legacy (JSON with comments) |
| `config.yaml` | `~/.continue/config.yaml` | New (YAML with schema) |
| Workspace config | `.continue/*.yaml` | Per-workspace overrides |
| Rules | `.continue/rules/*.md` | Markdown rule files |

### Config Loading

**File:** `core/config/load.ts`

```
config.json / config.yaml
  → Parse + validate
  → Resolve model descriptions → BaseLLM instances
  → Load context providers
  → Load slash commands
  → Load MCP servers
  → Load rules
  → Load tools (base + config-dependent)
  → Apply tool overrides
  → Build ContinueConfig object
```

### ConfigHandler

**File:** `core/config/ConfigHandler.ts`

The `ConfigHandler` manages config lifecycle, profiles, and organizations:

```typescript
export class ConfigHandler {
  controlPlaneClient: ControlPlaneClient;
  currentProfile: ProfileLifecycleManager | null;
  currentOrg: OrgWithProfiles | null;

  constructor(ide: IDE, llmLogger: ILLMLogger, initialSessionInfoPromise: Promise<...>) {
    this.controlPlaneClient = new ControlPlaneClient(initialSessionInfoPromise, this.ide);
    this.globalLocalProfileManager = new ProfileLifecycleManager(
      new LocalProfileLoader(ide, this.controlPlaneClient, this.llmLogger),
      this.ide,
    );
  }

  async loadConfig(): Promise<ConfigResult<ContinueConfig>> { ... }
  onConfigUpdate(callback: ConfigUpdateFunction): void { ... }
  async reloadConfig(reason: string): Promise<void> { ... }
}
```

### Profile System

Continue supports multiple config profiles:
- **Local Profile** -- from local `config.yaml`/`config.json`
- **Platform Profile** -- from Continue Hub (org-managed)

### Key Config Types

```typescript
export interface ContinueConfig {
  tools: Tool[];
  rules: RuleWithSource[];
  contextProviders: IContextProvider[];
  slashCommands: SlashCommandWithSource[];
  mcpServerStatuses: MCPServerStatus[];
  modelsByRole: Record<ModelRole, ILLM[]>;
  selectedModelByRole: Record<ModelRole, ILLM | null>;
  tabAutocompleteOptions?: Partial<TabAutocompleteOptions>;
  docs?: SiteIndexingConfig[];
  disableIndexing?: boolean;
  experimental?: ExperimentalConfig;
  ui?: ContinueUIConfig;
}
```

### Shared Config

Per-workspace settings that persist across sessions (stored via `GlobalContext`):

```typescript
export interface SharedConfigSchema {
  // User preferences that override config defaults
}
```

---

## 12. Unique Features

### Rules System

**Path:** `core/llm/rules/`

Continue's rules system allows users to define coding standards that are injected into the system prompt:

| Rule Type | How Applied | Config |
|---|---|---|
| **Always** | Always included in context | `alwaysApply: true`, no globs |
| **Auto Attached** | Included when files match patterns | `globs` and/or `regex` |
| **Agent Requested** | AI decides when to apply | `alwaysApply: false`, has `description` |
| **Manual** | Only when explicitly mentioned (`@rules`) | `alwaysApply: false`, no description |

Rules are stored as `.md` files in `.continue/rules/` and can be created via the `create_rule_block` tool.

### Skills System

**Path:** `skills/`, `core/tools/definitions/readSkill.ts`

Skills are markdown files with detailed instructions for specific tasks. The `read_skill` tool lets the agent load skill content on demand.

### NextEdit (Predictive Editing)

**Path:** `core/nextEdit/`

A secondary prediction system that anticipates the user's next edit based on recent changes:

```
core/nextEdit/
├── NextEditProvider.ts          # Main provider
├── NextEditPrefetchQueue.ts     # Prefetch queue for predictions
├── context/
│   ├── aggregateEdits.ts        # Edit aggregation
│   ├── diffFormatting.ts        # Diff formatting
│   └── processSmallEdit.ts      # Small edit processing
└── types.ts
```

### Codebase Indexing

**Path:** `core/indexing/`

Continue indexes the entire codebase for semantic search:

| Index Type | Implementation | Purpose |
|---|---|---|
| LanceDB | `LanceDbIndex.ts` | Vector embeddings for semantic search |
| Full Text Search | `FullTextSearchCodebaseIndex.ts` | Keyword search |
| Code Snippets | `CodeSnippetsIndex.ts` | Function/class-level indexing |
| Docs | `docs/DocsService.ts` | External documentation indexing |

The `CodebaseIndexer` manages incremental indexing with file watching:

```
core/indexing/
├── CodebaseIndexer.ts           # Main orchestrator
├── walkDir.ts                   # Directory traversal with cache
├── shouldIgnore.ts              # .continueignore support
├── refreshIndex.ts              # Incremental refresh
├── chunk/                       # Text chunking strategies
└── docs/                        # Documentation site indexing
    └── DocsService.ts
```

### MCP Integration

**Path:** `core/context/mcp/`

Full MCP (Model Context Protocol) client with:
- `MCPManagerSingleton` -- Manages all MCP server connections
- `MCPConnection` -- Individual server connection lifecycle
- `MCPOauth` -- OAuth authentication for MCP servers
- Tools, resources, and prompts from MCP servers are automatically integrated
- MCP context providers appear as `@mention` options in the UI

### Tool Overrides

Users can customize any built-in tool's description, display title, action phrases, or disable it entirely via config:

```typescript
export function applyToolOverrides(tools: Tool[], overrides: ToolOverride[]): {
  tools: Tool[];
  errors: ConfigValidationError[];
}
```

### System Message Modes

**File:** `core/llm/defaultSystemMessages.ts`

Three distinct modes with tailored system prompts:

| Mode | Tools Available | Purpose |
|---|---|---|
| **Chat** | None (code blocks only) | Conversational, suggests Apply button |
| **Agent** | All tools | Autonomous coding with tool use |
| **Plan** | Read-only tools only | Understanding and planning |

### System Tool Messages

**Path:** `core/tools/systemMessageTools/`

For models that don't natively support tool calling, Continue can inject tool descriptions into the system message and parse tool calls from the response text:

```
systemMessageTools/
├── buildToolsSystemMessage.ts    # Inject tool schemas into system prompt
├── convertSystemTools.ts         # Convert to/from system message format
├── detectToolCallStart.ts        # Detect tool call patterns in output
├── interceptSystemToolCalls.ts   # Parse tool calls from text
├── toolCodeblocks/               # Tool call as code blocks
└── types.ts
```

### Terminal Security

**Package:** `packages/terminal-security/`

Terminal commands are evaluated for security before execution:

```typescript
export function evaluateTerminalCommandSecurity(
  basePolicy: ToolPolicy,
  command: string,
): ToolPolicy {
  // Analyzes command for dangerous patterns
  // Can escalate from "allowedWithPermission" to requiring explicit approval
}
```

### Data Logging and Analytics

Continue tracks usage data through:
- `DevDataSqliteDb` -- Local SQLite database for token usage stats
- `DataLogger` -- Structured event logging
- `Telemetry` -- PostHog analytics (opt-in)
- Stats endpoints: `stats/getTokensPerDay`, `stats/getTokensPerModel`

### Control Plane

Continue has a cloud control plane for:
- User authentication (WorkOS)
- Organization/team management
- Profile synchronization
- Remote sessions
- Credit tracking for managed API keys

---

## Summary of Key Architectural Patterns

| Pattern | Implementation |
|---|---|
| **IDE Abstraction** | `IDE` interface with message-based adapter (`MessageIde`) |
| **Type-Safe Protocol** | TypeScript tuple types `[RequestData, ResponseData]` for all messages |
| **In-Process Messenger** | Direct function calls in VS Code, binary IPC for JetBrains |
| **Message Pass-Through** | Extension routes messages between GUI and Core via explicit lists |
| **GUI-Driven Agent Loop** | No core agent loop -- GUI orchestrates tool call cycles |
| **Provider Registry** | Flat array of class references, matched by `providerName` string |
| **Context Providers** | Plugin-like `@mention` system with submenu support |
| **Token Management** | `compileChatMessages()` prunes from oldest, preserves last tool sequence |
| **Tool Policies** | 3-tier permission model with dynamic per-call evaluation |
| **Config Profiles** | Local + Platform profiles with per-workspace overrides |
| **Dual Tokenizer** | GPT (tiktoken) for OpenAI, Llama tokenizer for open-source models |

### Scale

| Metric | Count |
|---|---|
| Core TypeScript files (non-test) | ~464 |
| Core lines of code | ~70,000 |
| LLM Providers | 54 |
| Built-in Tools | 20 |
| Context Providers | 30 |
| Protocol Message Types | ~100+ |
| Config Types | JSON, YAML, workspace blocks, rules |
