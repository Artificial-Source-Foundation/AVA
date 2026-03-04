# Gemini CLI -- Backend Architecture Analysis

> Google's open-source TypeScript CLI AI coding agent (~42k GitHub stars)
> Version analyzed: `0.33.0-nightly.20260228`
> Repository: `github.com/google-gemini/gemini-cli`

---

## 1. Project Structure

### Top-Level Layout

```
gemini-cli/
├── packages/
│   ├── core/               # Backend: API orchestration, tools, prompts, agents
│   ├── cli/                # Frontend: React/Ink terminal UI
│   ├── sdk/                # Programmatic SDK for embedding Gemini CLI
│   ├── a2a-server/         # Agent-to-Agent server (A2A protocol)
│   ├── devtools/           # Developer tools
│   ├── test-utils/         # Shared test utilities
│   └── vscode-ide-companion/  # VS Code companion extension
├── evals/                  # Evaluation suite
├── integration-tests/      # E2E integration tests
├── schemas/                # JSON schemas (settings, etc.)
├── scripts/                # Build/CI/generation scripts
├── sea/                    # Single Executable Application (Node.js SEA)
├── third_party/            # Vendored dependencies
├── docs/                   # Documentation
├── esbuild.config.js       # Bundle configuration
├── Makefile                # Build targets
└── GEMINI.md               # Project context file (like CLAUDE.md)
```

### Monorepo Organization

- **npm workspaces** with 7 packages
- **Node.js >= 20.0.0**, TypeScript, ESM modules
- **React/Ink** for the terminal UI (not a web framework -- Ink renders React components to the terminal)
- **Vitest** for testing, **esbuild** for bundling, **ESLint + Prettier** for linting

### Core Package Internal Structure

```
packages/core/src/
├── agents/              # Subagent system (local + remote executors, browser agent)
│   ├── browser/         # Chrome-based visual agent (CDP/MCP)
│   ├── auth-provider/   # A2A authentication
│   ├── local-executor.ts      # Local agent loop
│   ├── remote-invocation.ts   # A2A remote agent calls
│   ├── subagent-tool.ts       # Tool wrapper for spawning subagents
│   ├── registry.ts            # Agent registry
│   └── agent-scheduler.ts     # Parallel tool execution for agents
├── availability/        # Model health tracking + availability policies
├── billing/             # Usage/overage tracking
├── code_assist/         # Google Code Assist integration (OAuth)
├── commands/            # User commands (extensions, memory, restore)
├── config/              # Configuration system (Config class, Storage, models)
├── confirmation-bus/    # Message bus for tool confirmation flow
├── core/                # Agent loop nucleus (GeminiChat, Turn, ContentGenerator, etc.)
├── fallback/            # Model fallback handling (429s, quota)
├── hooks/               # Lifecycle hook system (BeforeTool, AfterTool, BeforeModel, etc.)
├── ide/                 # IDE integration (VS Code companion client)
├── mcp/                 # MCP OAuth providers
├── mocks/               # Test mocks
├── output/              # Output formatting types
├── policy/              # Permission/approval policy engine
├── prompts/             # System prompt construction
├── resources/           # Resource registry
├── routing/             # Model routing (classifier strategies)
├── safety/              # Safety checker system (external + in-process)
├── scheduler/           # Tool execution scheduler + state machine
├── services/            # Core services (compression, recording, context, git, shell, etc.)
├── skills/              # Skill system (YAML frontmatter, discovery, activation)
├── telemetry/           # Logging, tracing, metrics
├── tools/               # All tool implementations + registry
└── utils/               # Shared utilities
```

---

## 2. Tools

Gemini CLI has **17 built-in tools** plus dynamic tools discovered via MCP servers and custom discovery commands.

### Built-in Tools

| Tool Name | Display Name | Kind | Description |
|-----------|-------------|------|-------------|
| `read_file` | ReadFile | Read | Read file contents with optional line range (`start_line`, `end_line`) |
| `write_file` | WriteFile | Edit | Overwrite entire file content |
| `replace` | Edit | Edit | Find-and-replace with fuzzy matching, regex, Levenshtein recovery; supports `old_string`/`new_string` + `instruction` + `allow_multiple` |
| `run_shell_command` | Shell | Execute | Execute shell commands; supports `is_background` for background processes; has interactive shell mode |
| `glob` | FindFiles | Search | Find files by glob pattern with `.gitignore` and `.geminiignore` respect |
| `grep_search` | Grep | Search | Regex content search; separate ripgrep variant (`grep_search_ripgrep`) with extended params |
| `list_directory` | ListDirectory | Read | List directory contents |
| `read_many_files` | ReadManyFiles | Read | Batch-read files matching include/exclude patterns with recursion |
| `google_web_search` | GoogleSearch | Search | Web search via Gemini API grounding (see Section 6) |
| `web_fetch` | WebFetch | Fetch | Fetch and process web pages |
| `save_memory` | Memory | Other | Save a fact to persistent `GEMINI.md` memory file |
| `write_todos` | WriteTodos | Other | Manage a session todo list (pending/in_progress/completed/cancelled) |
| `get_internal_docs` | InternalDocs | Read | Read internal documentation files |
| `activate_skill` | ActivateSkill | Other | Dynamically activate a skill by name (see Section 10) |
| `ask_user` | AskUser | Communicate | Ask the user structured questions (text, select, multi-select) |
| `enter_plan_mode` | EnterPlanMode | Plan | Switch to plan mode (read-only, planning only) |
| `exit_plan_mode` | ExitPlanMode | Plan | Exit plan mode, submitting a plan file for approval |

### Tool Architecture

**DeclarativeTool pattern** -- every tool is a class extending `BaseDeclarativeTool<TParams, TResult>`:

```
packages/core/src/tools/tools.ts
```

```typescript
// The two-phase pattern: Build (validate) -> Execute
abstract class DeclarativeTool<TParams, TResult> implements ToolBuilder<TParams, TResult> {
  abstract build(params: TParams): ToolInvocation<TParams, TResult>;
}

abstract class BaseDeclarativeTool<TParams, TResult> extends DeclarativeTool<TParams, TResult> {
  build(params: TParams): ToolInvocation<TParams, TResult> {
    const validationError = this.validateToolParams(params);  // JSON Schema validation
    if (validationError) throw new Error(validationError);
    return this.createInvocation(params, this.messageBus, this.name, this.displayName);
  }
  protected abstract createInvocation(...): ToolInvocation<TParams, TResult>;
}
```

Key design decisions:
- **Separation of validation and execution** -- `build()` validates params and returns an `Invocation` object; `execute()` runs it
- **Schema per model family** -- tool definitions ship with different JSON schemas for `default-legacy` vs `gemini-3` model families (see `packages/core/src/tools/definitions/model-family-sets/`)
- **Kind classification** -- `Read`, `Edit`, `Delete`, `Move`, `Search`, `Execute`, `Think`, `Agent`, `Fetch`, `Communicate`, `Plan`, `SwitchMode`, `Other`; `MUTATOR_KINDS` (Edit, Delete, Move, Execute) require approval; `READ_ONLY_KINDS` (Read, Search, Fetch) can run freely
- **Tail tool calls** -- `ToolResult.tailToolCallRequest` allows a tool to chain into another tool transparently
- **Live output streaming** -- `canUpdateOutput: boolean` on the tool; shell tool streams output chunks at 1-second intervals
- **Modifiable tools** -- edit/write tools implement `ModifiableDeclarativeTool` which allows the user to modify content in an external editor before accepting

### Dynamic Tool Discovery

```
packages/core/src/tools/tool-registry.ts
```

Two discovery mechanisms:

1. **Command-line discovery** -- configurable `toolDiscoveryCommand` / `toolCallCommand` in settings; executes a subprocess that returns JSON `FunctionDeclaration[]`; tools are prefixed with `discovered_tool_`
2. **MCP server discovery** -- `McpClientManager` connects to configured MCP servers; tools are registered as `DiscoveredMCPTool` instances with fully-qualified names (`server__tool`)

### Tool Registry

```typescript
class ToolRegistry {
  private allKnownTools: Map<string, AnyDeclarativeTool>;
  registerTool(tool: AnyDeclarativeTool): void;
  getTool(name: string): AnyDeclarativeTool | undefined;  // supports legacy aliases
  getFunctionDeclarations(modelId?: string): FunctionDeclaration[];  // model-specific schemas
  getActiveTools(): AnyDeclarativeTool[];  // respects excludeTools config
}
```

---

## 3. Agent Loop

The agent loop is structured around three layers: **Turn** (single LLM call), **CoreToolScheduler** (tool execution), and **GeminiChat** (session state).

### Turn Lifecycle

```
packages/core/src/core/turn.ts
```

The `Turn` class is an async generator that yields `ServerGeminiStreamEvent` values:

```typescript
class Turn {
  async *run(modelConfigKey, req, signal, displayContent?, role?): AsyncGenerator<ServerGeminiStreamEvent> {
    const responseStream = await this.chat.sendMessageStream(...);
    for await (const streamEvent of responseStream) {
      // Handle: RETRY, AGENT_EXECUTION_STOPPED, AGENT_EXECUTION_BLOCKED
      // Extract: thoughts, text content, function calls, citations
      // Yield: Content, Thought, ToolCallRequest, Citation, Finished, Error events
    }
  }
}
```

Event types emitted by the loop:

| Event | Purpose |
|-------|---------|
| `Content` | Streamed text from model |
| `Thought` | Model's thinking/reasoning (parsed from `part.thought`) |
| `ToolCallRequest` | Model wants to call a tool |
| `ToolCallResponse` | Tool execution result |
| `ToolCallConfirmation` | User must approve tool |
| `Finished` | Turn complete with `finishReason` + `usageMetadata` |
| `ChatCompressed` | Context was compressed |
| `Retry` | Retrying failed request |
| `ContextWindowWillOverflow` | Warning: approaching context limit |
| `LoopDetected` | Stuck-in-loop detection fired |
| `InvalidStream` | Stream validation failed |
| `AgentExecutionStopped` | Hook stopped execution |
| `Citation` | Web search citations |

### GeminiChat -- Streaming and Retry

```
packages/core/src/core/geminiChat.ts
```

The `GeminiChat` class manages the full conversation session:

1. **Sequential message sending** -- `sendPromise` ensures only one message is in flight at a time
2. **Invalid content retry** -- up to 2 attempts (1 initial + 1 retry) with 500ms base delay for `InvalidStreamError` types (NO_FINISH_REASON, MALFORMED_FUNCTION_CALL, etc.)
3. **Network retry with backoff** -- `retryWithBackoff` handles transient errors, 429s, and SSL errors
4. **Stream validation** -- after collecting all chunks, validates the stream had either a tool call or (valid finish reason + non-empty text)
5. **History management** -- maintains both "curated" (valid turns only) and "comprehensive" (all turns) history
6. **Thought signature injection** -- for preview models, ensures first function call in each model turn has a `thoughtSignature` property to pass API validation

### CoreToolScheduler -- Sequential Execution with Queue

```
packages/core/src/core/coreToolScheduler.ts
```

**Key insight: tools execute ONE AT A TIME sequentially**, not in parallel (despite the model potentially requesting multiple tools simultaneously):

```typescript
class CoreToolScheduler {
  private toolCalls: ToolCall[];         // Currently active (max 1)
  private toolCallQueue: ToolCall[];     // Queued for sequential execution
  private completedToolCallsForBatch: CompletedToolCall[];

  async schedule(request: ToolCallRequestInfo | ToolCallRequestInfo[], signal): Promise<void> {
    // If already running, queue the request
    // Otherwise, process immediately via _schedule -> _processNextInQueue
  }
}
```

Tool call state machine:

```
Validating -> Scheduled -> Executing -> Success/Error/Cancelled
                 |
                 v
          AwaitingApproval -> (user confirms) -> Scheduled
                           -> (user cancels) -> Cancelled
```

The scheduler:
- Creates `ToolCall` objects with `Validating` status
- Checks the **PolicyEngine** for ALLOW/DENY/ASK_USER
- For ASK_USER, calls `invocation.shouldConfirmExecute()` which triggers the confirmation UI
- For ALLOW, transitions directly to `Scheduled` -> `Executing`
- Uses `ToolExecutor` for actual execution with hooks
- Fires `BeforeTool` and `AfterTool` hooks around execution
- Supports **tool modification** (user can edit file content in external editor before accepting)

### Subagent System

```
packages/core/src/agents/local-executor.ts
```

Gemini CLI has a full subagent system where the main agent can spawn specialized sub-agents:

```typescript
class LocalAgentExecutor<TOutput> {
  // Runs an isolated agent loop with:
  // - Its own GeminiChat instance
  // - Its own isolated ToolRegistry (subset of parent tools)
  // - A mandatory `complete_task` tool for termination
  // - Configurable max turns (default 15) and max time (default 5 min)
  // - Compression support for long-running agents
}
```

Agent types:
- **Local agents** (`kind: 'local'`) -- run in-process with their own tool registry
- **Remote agents** (`kind: 'remote'`) -- A2A protocol over HTTP
- **Browser agent** -- Chrome-based visual agent using CDP/MCP, with screenshot analysis
- **Codebase Investigator** -- specialized read-only agent for code exploration
- **CLI Help Agent** -- answers questions about Gemini CLI itself
- **Generalist Agent** -- general-purpose subagent

---

## 4. LLM Providers

### Gemini-Only Architecture

Gemini CLI is **single-provider** -- it only supports Google's Gemini models. There is no multi-provider abstraction.

### Authentication Types

```
packages/core/src/core/contentGenerator.ts
```

| AuthType | Method | Environment Variable |
|----------|--------|---------------------|
| `LOGIN_WITH_GOOGLE` | OAuth (Google accounts) | `GOOGLE_GENAI_USE_GCA=true` |
| `USE_GEMINI` | API key | `GEMINI_API_KEY` |
| `USE_VERTEX_AI` | Google Cloud | `GOOGLE_GENAI_USE_VERTEXAI=true` + `GOOGLE_CLOUD_PROJECT` |
| `COMPUTE_ADC` | Cloud Shell / Compute default | `CLOUD_SHELL=true` or `GEMINI_CLI_USE_COMPUTE_ADC=true` |

### ContentGenerator Interface

```typescript
interface ContentGenerator {
  generateContent(request, userPromptId, role): Promise<GenerateContentResponse>;
  generateContentStream(request, userPromptId, role): Promise<AsyncGenerator<GenerateContentResponse>>;
  countTokens(request): Promise<CountTokensResponse>;
  embedContent(request): Promise<EmbedContentResponse>;
  userTier?: UserTierId;
}
```

The `ContentGenerator` is wrapped with:
- `LoggingContentGenerator` -- logs all API calls for telemetry
- `RecordingContentGenerator` -- records responses to files for replay/testing
- `FakeContentGenerator` -- loads pre-recorded responses for testing

### Model Selection and Routing

```
packages/core/src/config/models.ts
packages/core/src/routing/modelRouterService.ts
```

**Supported models:**

| Model | Alias |
|-------|-------|
| `gemini-3-pro-preview` | `pro`, `auto` |
| `gemini-3.1-pro-preview` | (via feature flag) |
| `gemini-3-flash-preview` | `flash` |
| `gemini-2.5-pro` | (legacy) |
| `gemini-2.5-flash` | (legacy) |
| `gemini-2.5-flash-lite` | `flash-lite` |

**Model routing** -- a multi-strategy composite router:

```typescript
class ModelRouterService {
  // Strategy chain (order matters):
  // 1. FallbackStrategy -- respects active fallback state
  // 2. OverrideStrategy -- user/config overrides
  // 3. ApprovalModeStrategy -- plan mode may route differently
  // 4. GemmaClassifierStrategy -- local Gemma model classifies task complexity
  // 5. ClassifierStrategy -- generic text classification
  // 6. NumericalClassifierStrategy -- numerical features
  // 7. DefaultStrategy -- terminal fallback
}
```

**Model availability** -- `ModelAvailabilityService` tracks model health:
- `markTerminal(model, reason)` -- quota exhausted or capacity issues, permanent for session
- `markRetryOncePerTurn(model)` -- transient, gets one retry per conversation turn
- `markHealthy(model)` -- clears failure state

**Thinking budget** -- capped at 8192 tokens (`DEFAULT_THINKING_MODE = 8192`) to prevent runaway thinking loops.

---

## 5. Context / Token Management

### Token Limits

```
packages/core/src/core/tokenLimits.ts
```

All supported models share a **1,048,576 token (1M) context window**. This is the `DEFAULT_TOKEN_LIMIT`.

### Chat Compression

```
packages/core/src/services/chatCompressionService.ts
```

Compression triggers when history exceeds **50% of the token limit** (`DEFAULT_COMPRESSION_TOKEN_THRESHOLD = 0.5`), approximately 524K tokens:

1. **Split point detection** -- `findCompressSplitPoint()` finds a boundary where the oldest ~70% of history can be compressed, preserving the most recent 30% (`COMPRESSION_PRESERVE_THRESHOLD = 0.3`)
2. **LLM-based summarization** -- sends the older portion to a Flash model with a compression prompt, gets a summary
3. **Summary replacement** -- replaces the compressed portion with a single `[COMPRESSED CONTEXT]` message containing the summary
4. **Validation** -- if the new token count is higher than the original (inflation), compression is skipped

### Tool Output Masking

```
packages/core/src/services/toolOutputMaskingService.ts
```

A **"Hybrid Backward Scanned FIFO"** algorithm for managing bulky tool outputs:

1. **Protection window** -- the newest 50K tokens of tool output are always preserved (`DEFAULT_TOOL_PROTECTION_THRESHOLD = 50_000`)
2. **Latest turn protection** -- optionally skips the entire most recent turn
3. **Backward scan** -- scans backwards past the protection window to find maskable tool outputs
4. **Batch trigger** -- only masks if total prunable tokens exceed 30K (`DEFAULT_MIN_PRUNABLE_TOKENS_THRESHOLD = 30_000`)
5. **Masking** -- replaces content with a `<tool_output_masked>` indicator

Effectively, masking only starts when there are ~80K+ tokens of tool output in history.

### Tool Output Truncation During Compression

The `truncateHistoryToBudget` function implements a "Reverse Token Budget" strategy:
- Iterates history from newest to oldest
- Keeps a running tally of function response tokens
- After exceeding the budget (50K tokens), older large responses are truncated to their last 30 lines and saved to a temporary file

### Context Manager (Memory Discovery)

```
packages/core/src/services/contextManager.ts
```

Three-tier hierarchical memory:
1. **Global memory** -- `~/.gemini/GEMINI.md` and user-level context files
2. **Extension memory** -- context files contributed by extensions
3. **Project memory** -- workspace-level `GEMINI.md` files + MCP instructions (only loaded from trusted folders)

Plus **JIT (Just-In-Time) context** -- when a tool accesses a path, `discoverContext()` traverses upward from that path looking for subdirectory-level `GEMINI.md` files to inject into context on-demand.

---

## 6. Google Search Grounding

```
packages/core/src/tools/web-search.ts
```

Web search is implemented as a **Gemini API grounding call**, not a traditional search API:

```typescript
class WebSearchToolInvocation {
  async execute(signal: AbortSignal): Promise<WebSearchToolResult> {
    const response = await geminiClient.generateContent(
      { model: 'web-search' },          // special model identifier
      [{ role: 'user', parts: [{ text: this.params.query }] }],
      signal,
      LlmRole.UTILITY_TOOL,
    );
    // Extract: responseText, groundingMetadata, groundingChunks, groundingSupports
    // Insert inline citation markers [1], [2], etc. at byte positions
    // Append formatted source list
  }
}
```

Key details:
- Uses a special `'web-search'` model identifier passed to `GeminiClient`
- The API returns `groundingMetadata` with `groundingChunks` (source URLs/titles) and `groundingSupports` (which text segments map to which sources)
- Citation markers are inserted at UTF-8 byte positions using `TextEncoder`/`TextDecoder`
- The tool is classified as `Kind.Search` (read-only, no approval needed)
- Results include inline citations like `[1]` and a formatted `Sources:` section

---

## 7. Session Management

### Session Recording

```
packages/core/src/services/chatRecordingService.ts
```

Every conversation is recorded to disk as a JSON file:

```typescript
interface ConversationRecord {
  sessionId: string;
  messages: ConversationRecordExtra[];  // user, gemini, tool_calls, thoughts, info, error
  tokenUsage: TokensSummary[];
  metadata: { model: string; startTime: string; ... };
}
```

Session files are stored at:
- `{project_dir}/.gemini/tmp/chats/session-{first8chars}-{slug}.json`
- Project identification via hash of the target directory path

### Session Resume

The SDK (`packages/sdk/`) and CLI both support resuming sessions:
- `resumeSession(sessionId)` scans chat files for matching session ID
- Loads `ResumedSessionData` with the full conversation record + file path
- Reconstructs `GeminiChat` with the saved history

### Chat History Model

```typescript
// Two views of history:
getHistory(curated: true)   // Only valid user/model turns (sent to API)
getHistory(curated: false)  // All turns including invalid/empty model responses
```

The curated history strips out model responses that failed validation (empty content, safety filters, etc.) while preserving all user turns. This ensures the API always receives valid alternating user/model messages.

---

## 8. Permissions / Safety

### Policy Engine

```
packages/core/src/policy/policy-engine.ts
packages/core/src/policy/types.ts
```

The `PolicyEngine` is a rule-based system that evaluates every tool call:

```typescript
enum PolicyDecision { ALLOW, DENY, ASK_USER }

interface PolicyRule {
  name?: string;
  toolName?: string;            // Tool this applies to (* for wildcard)
  argsPattern?: RegExp;         // Regex match against serialized args
  toolAnnotations?: Record<string, unknown>;  // MCP annotations match
  decision: PolicyDecision;
  priority?: number;            // Higher = checked first
  modes?: ApprovalMode[];       // Only apply in specific modes
  allowRedirection?: boolean;   // Allow shell redirects even if policy says ALLOW
  denyMessage?: string;         // Custom denial message
}
```

### Approval Modes

```typescript
enum ApprovalMode {
  DEFAULT = 'default',    // Ask for mutating operations
  AUTO_EDIT = 'autoEdit', // Auto-approve edits, ask for shell
  YOLO = 'yolo',          // Auto-approve everything
  PLAN = 'plan',          // Read-only, planning only
}
```

In `PLAN` mode, `write_file` and `replace` tool schemas are dynamically modified to add "ONLY FOR PLANS" descriptions and restrict them to the plans directory.

### Confirmation Flow

The confirmation flow uses a **MessageBus** (pub/sub EventEmitter):

```
Tool.shouldConfirmExecute()
  -> MessageBus.publish(TOOL_CONFIRMATION_REQUEST)
  -> PolicyEngine.check(toolCall, serverName, toolAnnotations)
  -> MessageBus.publish(TOOL_CONFIRMATION_RESPONSE)
  -> Returns ALLOW / DENY / ASK_USER
```

When `ASK_USER`:
- The `CoreToolScheduler` creates a `ToolCallConfirmationDetails` object
- For edits: shows a diff preview, allows "Modify with Editor" option
- For shell: shows the command, root commands parsed
- For MCP tools: shows server name, tool args, description
- User can: ProceedOnce, ProceedAlways, ProceedAlwaysAndSave, ModifyWithEditor, Cancel

### Safety Checker System

```
packages/core/src/safety/
```

A **pluggable safety checker** framework separate from the policy engine:

```typescript
interface SafetyCheckInput {
  protocolVersion: '1.0.0';
  toolCall: FunctionCall;
  context: {
    environment: { cwd: string; workspaces: string[] };
    history?: { turns: ConversationTurn[] };
  };
  config?: unknown;
}

enum SafetyCheckDecision { ALLOW, DENY, ASK_USER }
```

Safety checker types:
- **External checkers** -- subprocess-based; receives `SafetyCheckInput` on stdin, returns `SafetyCheckResult` on stdout
- **In-process checkers**:
  - `allowed-path` -- validates tool arguments reference paths within allowed directories
  - `conseca` -- Google's Conseca safety checker

### Shell Command Safety

Shell commands receive special attention:
- Commands are parsed with `shell-quote` to extract root commands
- Redirection detection (`hasRedirection()`) -- if a shell command has `|`, `>`, `>>`, etc., policy may downgrade ALLOW to ASK_USER unless `allowRedirection: true`
- Shell parsers are initialized on-demand for command analysis

### Folder Trust

```
packages/core/src/services/FolderTrustDiscoveryService.ts
```

Workspaces must be explicitly trusted before project-level `GEMINI.md`, hooks, skills, and policies are loaded. Untrusted folders only get global-level configuration.

---

## 9. Configuration

### Config Class

```
packages/core/src/config/config.ts
```

The `Config` class is a **massive god object** (~800+ lines of constructor, 100+ private fields) that serves as the central dependency injection container. It holds references to:

- `ToolRegistry`, `PolicyEngine`, `HookSystem`
- `ContentGenerator`, `GeminiClient`, `BaseLlmClient`
- `ModelConfigService`, `ModelRouterService`, `ModelAvailabilityService`
- `SkillManager`, `AgentRegistry`, `ContextManager`
- `McpClientManager`, `FileDiscoveryService`, `GitService`
- `MessageBus`, `Storage`, `FileExclusions`

### Configuration Sources

Settings are loaded from multiple sources with increasing precedence:

1. **Default values** -- hardcoded in `Config` constructor
2. **Global settings** -- `~/.gemini/settings.json`
3. **Workspace settings** -- `.gemini/settings.json` in project root
4. **Environment variables** -- `GEMINI_API_KEY`, `GEMINI_MODEL`, `GOOGLE_GENAI_USE_VERTEXAI`, etc.
5. **CLI flags** -- command-line arguments override everything

### Storage Paths

```
packages/core/src/config/storage.ts
```

```
~/.gemini/                         # Global config dir
├── settings.json                  # Global settings
├── oauth_creds.json               # OAuth credentials
├── installation_id                # Anonymous installation ID
├── mcp-oauth-tokens.json          # MCP OAuth token cache
├── commands/                      # User-defined commands
├── skills/                        # User-level skills
├── extensions/                    # Installed extensions
└── policies/                      # User-level policy TOML files

{project}/.gemini/                 # Project config dir
├── settings.json                  # Project settings
├── GEMINI.md                      # Project context file
├── skills/                        # Project-level skills
├── policies/                      # Project-level policy TOML files
├── plans/                         # Plan mode output directory
└── tmp/                           # Session data
    ├── chats/                     # Recorded conversations
    └── tool-outputs/              # Truncated tool outputs
```

### Model Configuration Service

```
packages/core/src/services/modelConfigService.ts
```

Provides per-role model configs (main chat, compression, subagent, etc.) with:
- `generateContentConfig` (temperature, thinking mode, response schema)
- Model-specific overrides via `ModelConfigKey`
- Dynamic config resolution based on auth type and availability

---

## 10. Extension / Plugin System

### Extensions

```
packages/core/src/config/config.ts  (GeminiCLIExtension interface)
```

Extensions are loadable packages that can contribute:

```typescript
interface GeminiCLIExtension {
  name: string;
  version: string;
  isActive: boolean;
  path: string;
  mcpServers?: Record<string, MCPServerConfig>;   // MCP server definitions
  contextFiles: string[];                          // Memory files
  excludeTools?: string[];                         // Tools to disable
  hooks?: { [K in HookEventName]?: HookDefinition[] };  // Lifecycle hooks
  settings?: ExtensionSetting[];                   // Configurable env vars
  skills?: SkillDefinition[];                      // Skills
  agents?: AgentDefinition[];                      // Agent definitions
  themes?: CustomTheme[];                          // Terminal themes
  rules?: PolicyRule[];                            // Policy rules
  checkers?: SafetyCheckerRule[];                  // Safety checkers
}
```

Extension install metadata tracks source (git, local, link, github-release), auto-update settings, etc.

### Skills

```
packages/core/src/skills/skillManager.ts
packages/core/src/skills/skillLoader.ts
```

Skills are YAML-frontmatter markdown files that provide context-specific instructions:

```markdown
---
name: react-patterns
description: React component patterns and conventions
---

When editing .tsx files, follow these React patterns:
...
```

Discovery precedence (lowest to highest):
1. Built-in skills (`packages/core/src/skills/builtin/`)
2. Extension-contributed skills
3. User skills (`~/.gemini/skills/` or `~/.agents/skills/`)
4. Workspace skills (`.gemini/skills/` or `.agents/skills/`) -- only if folder is trusted

Skills are activated dynamically via the `activate_skill` tool -- the model calls it with a skill name, and the skill's body is injected into the system prompt context.

### Hook System

```
packages/core/src/hooks/
```

A comprehensive lifecycle hook system with 11 event types:

| Hook Event | Fires When | Can Do |
|------------|-----------|--------|
| `BeforeTool` | Before tool execution | Modify input, block, approve |
| `AfterTool` | After tool execution | Modify output, add context |
| `BeforeModel` | Before LLM API call | Modify config, contents, stop/block |
| `AfterModel` | After LLM response | Inspect, stop/block |
| `BeforeToolSelection` | Before tool config sent | Modify available tools, tool config |
| `BeforeAgent` | Before agent starts | Setup |
| `AfterAgent` | After agent completes | Cleanup |
| `SessionStart` | Session begins | Initialize |
| `SessionEnd` | Session ends | Cleanup |
| `PreCompress` | Before context compression | Pre-compression actions |
| `Notification` | Tool confirmation shown | Side effects |

Hooks can be:
- **Command hooks** (`type: 'command'`) -- execute a subprocess with JSON on stdin
- **Runtime hooks** (`type: 'runtime'`) -- in-process function callbacks

Hook configuration sources follow the same precedence as settings: System < Extensions < User < Project < Runtime.

### MCP Integration

MCP servers are configured in settings with support for multiple transports:

```typescript
class MCPServerConfig {
  command?: string;        // stdio transport
  args?: string[];
  url?: string;            // SSE or HTTP transport
  httpUrl?: string;        // Streamable HTTP (deprecated, use url + type)
  tcp?: string;            // WebSocket transport
  type?: 'sse' | 'http';  // Transport type hint
  timeout?: number;
  trust?: boolean;         // Trusted = auto-approve tools
  oauth?: MCPOAuthConfig;  // OAuth configuration
  includeTools?: string[]; // Tool allowlist
  excludeTools?: string[]; // Tool blocklist
}
```

---

## 11. Unique Features

### Loop Detection

```
packages/core/src/services/loopDetectionService.ts
```

A **dual-layer loop detection** system:

1. **Heuristic detection** -- hash-based matching of tool calls (threshold: 5 identical consecutive calls) and content chunks (threshold: 10 identical chunks of 50 chars)
2. **LLM-based detection** -- after 40 turns in a single prompt, an LLM evaluates the recent 20 turns for unproductive loops using a detailed system prompt that distinguishes debugging progress from actual loops. The check interval adapts based on confidence (7-15 turns). Uses a "double-check" with a separate model alias.

### Model-Family-Specific Tool Schemas

```
packages/core/src/tools/definitions/model-family-sets/
```

Tool declarations ship in two variants:
- `default-legacy` -- for Gemini 2.5 models
- `gemini-3` -- potentially different parameter names, descriptions, or schemas for Gemini 3 models

The `getToolSet(modelId)` function resolves the appropriate set, and `resolveToolDeclaration()` merges base definitions with model-specific overrides.

### Edit Tool with Fuzzy Matching

```
packages/core/src/tools/edit.ts
```

The `replace` (edit) tool supports multiple strategies:

| Strategy | Description |
|----------|-------------|
| `exact` | Direct string replacement |
| `flexible` | Whitespace-normalized matching |
| `regex` | Regex-based replacement |
| `fuzzy` | Levenshtein distance with threshold (10% weighted difference allowed, whitespace costs 10% of character difference) |

Plus **LLM-based correction** -- if all strategies fail, `FixLLMEditWithInstruction` uses a Flash model to attempt the edit based on the instruction field.

Also includes **omission placeholder detection** (`omissionPlaceholderDetector.ts`) -- detects when the model uses placeholders like `// ... rest of code ...` instead of providing full content.

### Sandbox Execution

```
packages/core/src/config/config.ts  (SandboxConfig)
```

Supports sandboxed shell execution via Docker, Podman, or macOS `sandbox-exec`:

```typescript
interface SandboxConfig {
  command: 'docker' | 'podman' | 'sandbox-exec';
  image: string;  // e.g. "us-docker.pkg.dev/gemini-code-dev/gemini-cli/sandbox:..."
}
```

### A2A Server (Agent-to-Agent Protocol)

```
packages/a2a-server/
```

An experimental server implementing the A2A protocol for agent-to-agent communication. The main agent can call remote agents, and external agents can call the Gemini CLI agent via HTTP.

### SDK for Programmatic Use

```
packages/sdk/
```

A clean SDK for embedding Gemini CLI as a library:

```typescript
const agent = new GeminiCliAgent({ model: 'pro', cwd: '/path/to/project' });
const session = agent.session();
const response = await session.send('Fix the TypeScript errors');
// Or: const toolResult = await session.tool('read_file', { file_path: 'src/index.ts' });
```

### Plan Mode

When `ApprovalMode.PLAN` is active:
- Mutating tools (`write_file`, `replace`) are restricted to only write `.md` files in the plans directory
- The model is prompted to create a detailed plan before executing
- Plan files can be reviewed and approved by the user
- On approval, the mode switches and the plan is injected as context for execution

### Browser Agent

```
packages/core/src/agents/browser/
```

A visual agent that launches Chrome via MCP:
- Session modes: `persistent` (profile survives), `isolated` (temp profile), `existing` (attach to running Chrome)
- Screenshot analysis with a specialized visual model
- Tool wrapping for the MCP browser tools

### Telemetry and Tracing

Comprehensive telemetry with:
- OpenTelemetry spans (`runInDevTraceSpan`)
- Event logging (tool calls, model routing, compression, loop detection)
- Token usage tracking per message
- Startup profiling (`startupProfiler`)

### Environment Sanitization

```
packages/core/src/services/environmentSanitization.ts
```

Allows allowlisting/blocklisting environment variables that are passed to shell commands, plus optional redaction of sensitive values.

---

## Architecture Summary

| Aspect | Gemini CLI Approach |
|--------|-------------------|
| **Provider** | Gemini-only (single provider) |
| **Tool pattern** | DeclarativeTool with build/execute separation + JSON Schema validation |
| **Tool execution** | Sequential (one at a time) via CoreToolScheduler queue |
| **Context management** | 1M token window, 50% compression threshold, LLM summarization, tool output masking |
| **Permissions** | PolicyEngine (rule-based) + SafetyChecker (pluggable) + ApprovalMode + FolderTrust |
| **Extension model** | YAML skills + lifecycle hooks + MCP servers + themes + policy rules |
| **Session persistence** | JSON files in `.gemini/tmp/chats/`, resumable |
| **Agent system** | Local subagents (isolated ToolRegistry) + A2A remote agents |
| **Config** | Monolithic Config god object, JSON settings files, 4-tier precedence |
| **Loop detection** | Dual: heuristic hash matching + LLM-based analysis after 40 turns |
| **Web search** | Gemini API grounding (not a search API), with inline citation injection |
| **Model routing** | Composite strategy chain: fallback -> override -> approval -> classifier -> default |
