# Gemini CLI Core Package Analysis

**Analysis Date**: 2026-02-04
**Package Path**: `docs/reference-code/gemini-cli/packages/core/src/`
**Purpose**: Comprehensive analysis of Gemini CLI's core architecture for AVA reference

---

## Overview

The Gemini CLI core package is a well-structured, production-grade implementation of an AI coding assistant. It features:

- **Event-driven architecture** with message bus for tool confirmations
- **Declarative tool system** with policy-based approval
- **Hook system** for lifecycle events (BeforeTool, AfterTool, BeforeModel, etc.)
- **Agent system** supporting local and remote agents (A2A protocol)
- **MCP integration** with OAuth support for server authentication
- **Chat compression** with intelligent context management
- **Model availability service** for fallback handling

The codebase is approximately 100+ TypeScript files across 20+ directories, with comprehensive test coverage.

---

## Directory Structure

```
src/
├── agents/           # Agent system (local, remote, A2A)
├── availability/     # Model health tracking and fallback
├── code_assist/      # Code Assist integration (Google Cloud)
├── commands/         # CLI commands (init, memory, restore)
├── config/           # Configuration management
├── confirmation-bus/ # Message bus for tool confirmations
├── core/             # Core chat/LLM logic
├── fallback/         # Fallback handling types
├── hooks/            # Lifecycle hook system
├── ide/              # IDE integrations
├── mcp/              # MCP OAuth and authentication
├── output/           # Output formatting (JSON, streams)
├── policy/           # Policy engine for tool approval
├── prompts/          # MCP prompts
├── resources/        # Resource registry
├── routing/          # Model routing
├── safety/           # Safety checking
├── scheduler/        # Tool execution scheduler
├── services/         # Services (compression, git, shell, etc.)
├── skills/           # Skill system
├── telemetry/        # Logging and telemetry
├── tools/            # Tool implementations
└── utils/            # Utilities
```

---

## File-by-File Analysis

### 1. Agent System (`agents/`)

#### `types.ts` - Agent Type Definitions

```typescript
// Key types for agent configuration
export interface AgentDefinition {
  name: string;
  version?: string;
  displayName: string;
  description: string;
  trigger?: AgentTrigger;  // How agent is invoked
  visibility?: AgentVisibility;
}

export interface LocalAgentDefinition extends AgentDefinition {
  instructions: string;
  prompt?: PromptConfig;
  tools?: ToolConfig;
  input?: InputConfig;
  output?: OutputConfig;
  run?: RunConfig;
}

export enum AgentTerminateMode {
  ERROR = 'error',
  TIMEOUT = 'timeout',
  GOAL = 'goal',
  MAX_TURNS = 'max_turns',
  ABORTED = 'aborted',
}
```

**Key Insight**: Agents support configurable tools, inputs, outputs, and run configurations. The `trigger` field allows agents to auto-activate based on patterns.

#### `registry.ts` - Agent Registry

```typescript
export class AgentRegistry {
  private userAgentsDir: string;
  private projectAgentsDir: string;

  // Discovers agents from user and project directories
  async discoverAgents(): Promise<void>;

  // Gets agent directory context (phone book format)
  getDirectoryContext(): string;

  // Security: Acknowledges agent before first use
  acknowledgeAgent(agentName: string): void;
}
```

**Key Pattern**: Agents are loaded from `~/.gemini/agents/` (user) and `.gemini/agents/` (project) with security acknowledgment before first use.

#### `local-executor.ts` - Agent Execution Loop

```typescript
export class LocalAgentExecutor {
  async execute(
    agent: LocalAgentDefinition,
    initialInput: string,
    signal: AbortSignal,
  ): Promise<LocalAgentExecutionResult> {
    // Runs agent loop until complete_task tool is called
    // Handles timeouts with grace period
    // Emits activity events for observability
  }
}
```

**Key Pattern**: Agents have isolated tool registries to prevent recursion. The executor runs until `complete_task` is called.

#### `a2a-client-manager.ts` - Agent-to-Agent Protocol

```typescript
// Singleton manager for A2A protocol
export class A2AClientManager {
  private clients: Map<string, A2AClient> = new Map();

  async sendMessage(
    serverUrl: string,
    taskId: string,
    message: string,
  ): Promise<A2AResponse>;
}
```

**Key Insight**: Supports remote agent communication via A2A protocol for distributed agent architectures.

---

### 2. Core Chat Logic (`core/`)

#### `geminiChat.ts` - Chat Session Management

```typescript
export class GeminiChat {
  private comprehensiveHistory: Content[] = [];
  private curatedHistory: Content[] = [];

  async sendMessageStream(
    message: string | Content[],
    signal: AbortSignal,
  ): AsyncGenerator<ServerGeminiStreamEvent>;

  // Returns chat history (curated for compression, comprehensive for logging)
  getHistory(curated?: boolean): Content[];

  // Compression integration
  async compress(force?: boolean): Promise<ChatCompressionInfo>;
}
```

**Key Pattern**: Dual history tracking - curated for LLM context, comprehensive for session recording.

#### `turn.ts` - Turn Management

```typescript
export enum GeminiEventType {
  CHUNK = 'chunk',
  FUNCTION_CALL = 'function_call',
  FUNCTION_CALL_CANCELLED = 'function_call_cancelled',
  THOUGHT = 'thought',
  CONTENT = 'content',
  CITATION = 'citation',
  // ... 17 total event types
}

export class Turn {
  async *execute(): AsyncGenerator<ServerGeminiStreamEvent> {
    // Streams events from model response
  }
}
```

**Key Pattern**: Rich event types for streaming responses including thoughts, citations, and function calls.

#### `coreToolScheduler.ts` - Legacy Tool Scheduler

```typescript
export class CoreToolScheduler {
  // Status flow: validating -> scheduled -> executing -> success/error/cancelled
  async executeToolCalls(
    toolCalls: ToolCallRequestInfo[],
    signal: AbortSignal,
  ): Promise<CompletedToolCall[]>;
}
```

---

### 3. Tool System (`tools/`)

#### `tools.ts` - Base Tool Definitions

```typescript
// Tool result contract
export interface ToolResult {
  llmContent: unknown;        // What the LLM sees
  returnDisplay: unknown;     // What the user sees
  error?: { message: string; type?: ToolErrorType };
}

// Tool categories for policy decisions
export enum Kind {
  Read = 'read',
  Edit = 'edit',
  Delete = 'delete',
  Move = 'move',
  Search = 'search',
  Execute = 'execute',
  Think = 'think',
  Fetch = 'fetch',
  Communicate = 'communicate',
  Other = 'other',
}

// Confirmation outcome options
export enum ToolConfirmationOutcome {
  ProceedOnce = 'proceed_once',
  ProceedAlways = 'proceed_always',
  ProceedAlwaysAndSave = 'proceed_always_and_save',
  ModifyWithEditor = 'modify_with_editor',
  Cancel = 'cancel',
}
```

**Key Pattern**: Tools return separate content for LLM and user display. The `Kind` enum enables policy-based approval.

#### `tools.ts` - Declarative Tool Pattern

```typescript
export abstract class BaseDeclarativeTool<TParams, TResult> {
  constructor(
    name: string,
    displayName: string,
    description: string,
    kind: Kind,
    paramsSchema: Schema,
    messageBus: MessageBus,
  ) {}

  // Validates and creates invocation
  build(params: TParams): ToolInvocation<TParams, TResult>;

  // Override for custom validation
  protected validateToolParamValues(params: TParams): string | null;

  // Creates the invocation instance
  protected abstract createInvocation(
    params: TParams,
    messageBus: MessageBus,
  ): ToolInvocation<TParams, TResult>;
}

export abstract class BaseToolInvocation<TParams, TResult> {
  // Gets confirmation details for user approval
  protected abstract getConfirmationDetails(
    signal: AbortSignal,
  ): Promise<ToolCallConfirmationDetails | false>;

  // Executes the tool
  abstract execute(
    signal: AbortSignal,
    updateOutput?: (output: unknown) => void,
  ): Promise<TResult>;
}
```

**Key Pattern**: Tools are declarative with schema validation. Invocations handle confirmation and execution separately.

#### `tool-registry.ts` - Tool Registry

```typescript
export class ToolRegistry {
  private builtInTools: AnyDeclarativeTool[] = [];
  private discoveredTools: AnyDeclarativeTool[] = [];
  private mcpTools: DiscoveredMCPTool[] = [];

  registerTool(tool: AnyDeclarativeTool): void;
  getTool(name: string): AnyDeclarativeTool | undefined;
  getAllToolNames(): string[];

  // MCP tool support
  registerMCPTool(tool: DiscoveredMCPTool): void;
}
```

#### `shell.ts` - Shell Tool Implementation

```typescript
export class ShellTool extends BaseDeclarativeTool<ShellToolParams, ToolResult> {
  static readonly Name = 'shell';

  constructor(config: Config, messageBus: MessageBus) {
    super(
      ShellTool.Name,
      'Shell',
      getShellToolDescription(),  // Platform-specific
      Kind.Execute,
      paramsSchema,
      messageBus,
    );
  }
}

export class ShellToolInvocation extends BaseToolInvocation<ShellToolParams, ToolResult> {
  async execute(
    signal: AbortSignal,
    updateOutput?: (output: string | AnsiOutput) => void,
    shellExecutionConfig?: ShellExecutionConfig,
    setPidCallback?: (pid: number) => void,
  ): Promise<ToolResult> {
    // Uses ShellExecutionService for PTY support
    // Handles timeout, background processes, binary detection
  }

  protected override getPolicyUpdateOptions(
    outcome: ToolConfirmationOutcome,
  ): PolicyUpdateOptions | undefined {
    // Returns command prefix for policy rules
    return { commandPrefix: rootCommands };
  }
}
```

**Key Pattern**: Shell tool uses PTY for interactive commands with ANSI color support. Policy updates are based on command root (e.g., `git`, `npm`).

---

### 4. Scheduler (`scheduler/`)

#### `scheduler.ts` - Event-Driven Tool Orchestrator

```typescript
export class Scheduler {
  private readonly state: SchedulerStateManager;
  private readonly executor: ToolExecutor;
  private readonly modifier: ToolModificationHandler;

  async schedule(
    request: ToolCallRequestInfo | ToolCallRequestInfo[],
    signal: AbortSignal,
  ): Promise<CompletedToolCall[]> {
    // Phase 1: Ingestion & Resolution
    // Phase 2: Processing Loop
    // Phase 3: Single Call Orchestration
  }

  private async _processToolCall(
    toolCall: ValidatingToolCall,
    signal: AbortSignal,
  ): Promise<void> {
    // 1. Check policy
    const { decision, rule } = await checkPolicy(toolCall, this.config);

    // 2. Handle DENY
    if (decision === PolicyDecision.DENY) {
      this.state.updateStatus(callId, 'error', /* ... */);
      return;
    }

    // 3. Handle ASK_USER - confirmation loop
    if (decision === PolicyDecision.ASK_USER) {
      const result = await resolveConfirmation(toolCall, signal, /* ... */);
      // Handle outcome
    }

    // 4. Execute
    await this._execute(callId, signal);
  }
}
```

**Key Pattern**: Three-phase execution with policy checking before confirmation. The scheduler manages queuing and state transitions.

---

### 5. Policy Engine (`policy/`)

#### `types.ts` - Policy Types

```typescript
export enum PolicyDecision {
  ALLOW = 'allow',
  DENY = 'deny',
  ASK_USER = 'ask_user',
}

export enum ApprovalMode {
  DEFAULT = 'default',
  AUTO_EDIT = 'autoEdit',
  YOLO = 'yolo',
  PLAN = 'plan',
}

export interface PolicyRule {
  name?: string;
  toolName?: string;
  argsPattern?: RegExp;
  decision: PolicyDecision;
  priority?: number;
  modes?: ApprovalMode[];
  allowRedirection?: boolean;
  denyMessage?: string;
}
```

#### `policy-engine.ts` - Policy Enforcement

```typescript
export class PolicyEngine {
  private rules: PolicyRule[];
  private checkers: SafetyCheckerRule[];
  private approvalMode: ApprovalMode;

  async check(
    toolCall: FunctionCall,
    serverName: string | undefined,
  ): Promise<CheckResult> {
    // 1. Check rules by priority
    // 2. For shell commands, check subcommands recursively
    // 3. Run safety checkers
    // 4. Apply non-interactive mode
  }

  private async checkShellCommand(
    toolName: string,
    command: string | undefined,
    ruleDecision: PolicyDecision,
    serverName: string | undefined,
    dir_path: string | undefined,
    allowRedirection?: boolean,
    rule?: PolicyRule,
  ): Promise<CheckResult> {
    // Parses compound commands (pipelines, chains)
    // Recursively checks each subcommand
    // Downgrades ALLOW to ASK_USER for redirections
  }
}
```

**Key Pattern**: Policy rules support wildcards (`serverName__*`), regex patterns on arguments, and priority-based ordering. Shell commands are parsed and checked recursively.

---

### 6. Confirmation Bus (`confirmation-bus/`)

#### `message-bus.ts` - Event-Based Confirmation

```typescript
export class MessageBus extends EventEmitter {
  async publish(message: Message): Promise<void> {
    if (message.type === MessageBusType.TOOL_CONFIRMATION_REQUEST) {
      const { decision } = await this.policyEngine.check(
        message.toolCall,
        message.serverName,
      );

      switch (decision) {
        case PolicyDecision.ALLOW:
          this.emitMessage({
            type: MessageBusType.TOOL_CONFIRMATION_RESPONSE,
            correlationId: message.correlationId,
            confirmed: true,
          });
          break;
        case PolicyDecision.DENY:
          // Emit rejection and response
          break;
        case PolicyDecision.ASK_USER:
          // Pass through to UI
          this.emitMessage(message);
          break;
      }
    }
  }

  // Request-response pattern with correlation IDs
  async request<TRequest, TResponse>(
    request: Omit<TRequest, 'correlationId'>,
    responseType: TResponse['type'],
    timeoutMs: number = 60000,
  ): Promise<TResponse>;
}
```

#### `types.ts` - Message Types

```typescript
export enum MessageBusType {
  TOOL_CONFIRMATION_REQUEST = 'tool-confirmation-request',
  TOOL_CONFIRMATION_RESPONSE = 'tool-confirmation-response',
  TOOL_POLICY_REJECTION = 'tool-policy-rejection',
  TOOL_EXECUTION_SUCCESS = 'tool-execution-success',
  TOOL_EXECUTION_FAILURE = 'tool-execution-failure',
  UPDATE_POLICY = 'update-policy',
  TOOL_CALLS_UPDATE = 'tool-calls-update',
  ASK_USER_REQUEST = 'ask-user-request',
  ASK_USER_RESPONSE = 'ask-user-response',
}

// Rich confirmation details for different tool types
export type SerializableConfirmationDetails =
  | { type: 'info'; title: string; prompt: string; urls?: string[] }
  | { type: 'edit'; fileName: string; fileDiff: string; /* ... */ }
  | { type: 'exec'; command: string; rootCommand: string; /* ... */ }
  | { type: 'mcp'; serverName: string; toolName: string; /* ... */ };
```

**Key Pattern**: Message bus decouples tool execution from UI. Correlation IDs enable request-response patterns with timeouts.

---

### 7. Hook System (`hooks/`)

#### `types.ts` - Hook Event Types

```typescript
export enum HookEventName {
  BeforeTool = 'BeforeTool',
  AfterTool = 'AfterTool',
  BeforeAgent = 'BeforeAgent',
  AfterAgent = 'AfterAgent',
  SessionStart = 'SessionStart',
  SessionEnd = 'SessionEnd',
  PreCompress = 'PreCompress',
  BeforeModel = 'BeforeModel',
  AfterModel = 'AfterModel',
  BeforeToolSelection = 'BeforeToolSelection',
  Notification = 'Notification',
}

export interface HookConfig {
  type: 'command';
  command: string;
  name?: string;
  timeout?: number;
  env?: Record<string, string>;
}

export interface HookOutput {
  continue?: boolean;
  stopReason?: string;
  systemMessage?: string;
  decision?: HookDecision;  // 'allow' | 'deny' | 'block' | 'ask'
  reason?: string;
  hookSpecificOutput?: Record<string, unknown>;
}
```

#### `hookRunner.ts` - Hook Execution

```typescript
export class HookRunner {
  async executeHook(
    hookConfig: HookConfig,
    eventName: HookEventName,
    input: HookInput,
  ): Promise<HookExecutionResult> {
    // Security: Blocks project hooks in untrusted folders
    // Executes as shell command with JSON input on stdin
    // Parses JSON or plain text output
    // Handles exit codes: 0=success, 1=warning, 2=blocking error
  }

  async executeHooksSequential(
    hookConfigs: HookConfig[],
    eventName: HookEventName,
    input: HookInput,
  ): Promise<HookExecutionResult[]> {
    // Chains hooks, passing modified input to next hook
  }
}
```

#### `hookSystem.ts` - Hook Orchestration

```typescript
export class HookSystem {
  async fireBeforeModelEvent(
    llmRequest: GenerateContentParameters,
  ): Promise<BeforeModelHookResult> {
    // Can block model call with synthetic response
    // Can modify request config and contents
  }

  async fireBeforeToolEvent(
    toolName: string,
    toolInput: Record<string, unknown>,
    mcpContext?: McpToolContext,
  ): Promise<DefaultHookOutput | undefined>;

  async fireAfterToolEvent(
    toolName: string,
    toolInput: Record<string, unknown>,
    toolResponse: { llmContent: unknown; returnDisplay: unknown; error: unknown },
  ): Promise<DefaultHookOutput | undefined>;
}
```

**Key Pattern**: Hooks are command-based (shell scripts) with JSON I/O. Exit codes control blocking behavior. The BeforeModel hook can intercept and modify LLM requests.

---

### 8. Services (`services/`)

#### `chatCompressionService.ts` - Context Management

```typescript
export const DEFAULT_COMPRESSION_TOKEN_THRESHOLD = 0.5;  // 50% of limit
export const COMPRESSION_PRESERVE_THRESHOLD = 0.3;       // Keep last 30%
export const COMPRESSION_FUNCTION_RESPONSE_TOKEN_BUDGET = 50_000;

export class ChatCompressionService {
  async compress(
    chat: GeminiChat,
    promptId: string,
    force: boolean,
    model: string,
    config: Config,
  ): Promise<{ newHistory: Content[] | null; info: ChatCompressionInfo }> {
    // 1. Fire PreCompress hook
    // 2. Check if compression needed
    // 3. Truncate large tool outputs (reverse token budget)
    // 4. Find safe split point (at user turn boundary)
    // 5. Summarize old history with LLM
    // 6. Verify summary with self-correction probe
    // 7. Return new history with <state_snapshot>
  }
}
```

**Key Pattern**: Compression uses a three-phase approach: truncation, summarization, and verification. Tool outputs are truncated with "Reverse Token Budget" strategy (recent outputs preserved).

#### `contextManager.ts` - Memory Loading

```typescript
export class ContextManager {
  private globalMemory: string = '';      // ~/.gemini/GEMINI.md
  private environmentMemory: string = ''; // .gemini/GEMINI.md + MCP instructions

  async refresh(): Promise<void>;

  // JIT context discovery for accessed paths
  async discoverContext(
    accessedPath: string,
    trustedRoots: string[],
  ): Promise<string>;
}
```

**Key Pattern**: Three-tier memory: global (user), environment (project), and JIT (path-specific). MCP instructions are merged into environment memory.

#### `shellExecutionService.ts` - PTY Execution

```typescript
export class ShellExecutionService {
  static async execute(
    commandToExecute: string,
    cwd: string,
    onOutputEvent: (event: ShellOutputEvent) => void,
    abortSignal: AbortSignal,
    shouldUseNodePty: boolean,
    shellExecutionConfig: ShellExecutionConfig,
  ): Promise<ShellExecutionHandle> {
    if (shouldUseNodePty) {
      // Use @lydell/node-pty with headless xterm terminal
      // Supports ANSI colors, scrolling, resizing
      // 300,000 line scrollback buffer
    } else {
      // Fallback to child_process.spawn
      // 16MB buffer limit
    }
  }

  // Interactive PTY control
  static writeToPty(pid: number, input: string): void;
  static resizePty(pid: number, cols: number, rows: number): void;
  static scrollPty(pid: number, lines: number): void;
}
```

**Key Pattern**: PTY support with headless xterm for rich terminal rendering. Binary stream detection with progress tracking. Process group management for clean termination.

---

### 9. MCP Integration (`mcp/`)

#### `oauth-provider.ts` - MCP OAuth

```typescript
export class MCPOAuthProvider {
  async authenticate(
    serverName: string,
    config: MCPOAuthConfig,
    mcpServerUrl?: string,
  ): Promise<OAuthToken> {
    // 1. Discover OAuth config from MCP server
    // 2. Generate PKCE parameters
    // 3. Start local callback server
    // 4. Dynamic client registration (RFC 7591)
    // 5. Build authorization URL with resource parameter
    // 6. Open browser securely
    // 7. Exchange code for tokens
    // 8. Save token with refresh support
  }

  async getValidToken(
    serverName: string,
    config: MCPOAuthConfig,
  ): Promise<string | null> {
    // Auto-refresh expired tokens
  }
}
```

**Key Pattern**: Full OAuth 2.0 with PKCE support. Automatic discovery via RFC 8414 and WWW-Authenticate headers. Dynamic client registration for zero-config setup.

---

### 10. Availability Service (`availability/`)

#### `modelAvailabilityService.ts` - Model Health Tracking

```typescript
export class ModelAvailabilityService {
  // Health states: terminal (quota/capacity) or sticky_retry (once per turn)

  markTerminal(model: ModelId, reason: 'quota' | 'capacity'): void;
  markRetryOncePerTurn(model: ModelId): void;
  markHealthy(model: ModelId): void;

  selectFirstAvailable(models: ModelId[]): ModelSelectionResult {
    // Returns first available model with skip reasons
  }

  resetTurn(): void {
    // Resets sticky retry flags for new turn
  }
}
```

**Key Pattern**: Two-tier failure handling: terminal (permanent) and sticky (per-turn). Enables graceful degradation across model tiers.

---

### 11. Skills System (`skills/`)

#### `skillManager.ts` - Skill Discovery

```typescript
export class SkillManager {
  async discoverSkills(
    storage: Storage,
    extensions: GeminiCLIExtension[] = [],
  ): Promise<void> {
    // Precedence: Built-in -> Extensions -> User -> Workspace
    await this.discoverBuiltinSkills();
    // ... load from ~/.gemini/skills/ and .gemini/skills/
  }

  activateSkill(name: string): void;
  isSkillActive(name: string): boolean;
}
```

**Key Pattern**: Skills are loaded from multiple sources with precedence. Conflicts are detected and warned. Skills can be disabled per-session.

---

### 12. Configuration (`config/`)

#### `config.ts` - Main Configuration Class

```typescript
export class Config {
  private toolRegistry: ToolRegistry;
  private agentRegistry: AgentRegistry;
  private hookSystem: HookSystem;
  private policyEngine: PolicyEngine;
  private skillManager: SkillManager;
  private mcpClientManager: MCPClientManager;

  async initialize(): Promise<void> {
    // Initialize all subsystems
    await this.hookSystem.initialize();
    await this.agentRegistry.discoverAgents();
    await this.skillManager.discoverSkills();
    await this.initializeMCPServers();
  }

  createToolRegistry(): ToolRegistry {
    // Register core tools: shell, read_file, write_file, edit, glob, grep, etc.
  }

  validatePathAccess(path: string): string | null;
  isTrustedFolder(): boolean;
}
```

---

## Key Architectural Patterns

### 1. Declarative Tool Pattern
Tools are defined declaratively with JSON Schema for parameters. The `build()` method validates and creates invocations, separating configuration from execution.

### 2. Message Bus for Decoupling
The confirmation bus decouples tool execution from UI. Policy decisions flow through the bus, enabling different UI implementations (CLI, IDE, web).

### 3. Policy-Based Approval
Every tool call goes through the policy engine. Rules are priority-ordered with support for wildcards, regex patterns, and approval modes.

### 4. Hook System for Extensibility
Lifecycle hooks enable external scripts to:
- Block/modify model calls
- Intercept tool executions
- Add context before agent runs
- Clean up after sessions

### 5. Multi-Tier Memory
- **Global**: `~/.gemini/GEMINI.md`
- **Environment**: `.gemini/GEMINI.md` + MCP instructions
- **JIT**: Path-specific context discovered on-demand

### 6. Compression with Verification
Chat compression uses:
1. Token budget for tool outputs
2. LLM summarization with system snapshot format
3. Self-correction verification pass

---

## Key Takeaways for AVA

### Features AVA Should Consider

1. **Hook System**
   - Gemini CLI's command-based hooks (shell scripts with JSON I/O) are simple yet powerful
   - Exit codes control blocking behavior (0=allow, 1=warn, 2=block)
   - Hooks can modify tool inputs and model requests

2. **Policy Engine**
   - Priority-based rules with wildcards and regex
   - Recursive shell command parsing for compound commands
   - Separate approval modes (default, autoEdit, yolo, plan)

3. **Message Bus**
   - Decouples tool execution from UI
   - Correlation IDs for request-response patterns
   - Rich confirmation details per tool type (edit, exec, mcp)

4. **Chat Compression**
   - Reverse token budget (preserve recent tool outputs)
   - Split at user turn boundaries
   - Self-correction verification pass

5. **PTY Support**
   - Headless xterm for ANSI rendering
   - Large scrollback buffer (300,000 lines)
   - Interactive write/resize/scroll

6. **MCP OAuth**
   - Full RFC 7591 dynamic client registration
   - PKCE flow with automatic discovery
   - Token refresh with persistent storage

7. **Model Availability Service**
   - Terminal vs. sticky failure states
   - Automatic fallback selection
   - Per-turn retry resets

### Architecture Differences

| Aspect | Gemini CLI | AVA (Current) |
|--------|------------|------------------|
| Tool Approval | Message Bus + Policy Engine | Direct confirmation |
| Hooks | Command-based (shell) | TypeScript functions |
| Compression | LLM summarization | TBD |
| Shell | PTY with xterm | Basic spawn |
| MCP Auth | Full OAuth | Basic token |

### Recommended Priorities

1. **High Priority**
   - Policy engine with rules
   - Message bus for UI decoupling
   - Hook system for extensibility

2. **Medium Priority**
   - Chat compression with verification
   - PTY shell execution
   - Model availability service

3. **Lower Priority**
   - MCP OAuth (complex, niche use case)
   - A2A protocol (future consideration)

---

## Code Snippets Reference

### Tool Declaration Pattern
```typescript
export class MyTool extends BaseDeclarativeTool<MyParams, ToolResult> {
  constructor(config: Config, messageBus: MessageBus) {
    super(
      'my_tool',
      'My Tool',
      'Description for LLM',
      Kind.Edit,
      { type: 'object', properties: { /* schema */ } },
      messageBus,
    );
  }

  protected createInvocation(params: MyParams): ToolInvocation<MyParams, ToolResult> {
    return new MyToolInvocation(this.config, params, this.messageBus);
  }
}
```

### Policy Rule Example
```typescript
const rule: PolicyRule = {
  name: 'allow-git-commands',
  toolName: 'shell',
  argsPattern: /^git\s+(status|diff|log)/,
  decision: PolicyDecision.ALLOW,
  priority: 10,
  modes: [ApprovalMode.DEFAULT, ApprovalMode.AUTO_EDIT],
};
```

### Hook Configuration
```json
{
  "hooks": {
    "BeforeTool": [{
      "hooks": [{
        "type": "command",
        "name": "security-check",
        "command": "python3 ~/.gemini/hooks/security.py",
        "timeout": 5000
      }]
    }]
  }
}
```

### Message Bus Usage
```typescript
// Request confirmation
const response = await messageBus.request<ToolConfirmationRequest, ToolConfirmationResponse>(
  {
    type: MessageBusType.TOOL_CONFIRMATION_REQUEST,
    toolCall: { name: 'shell', args: { command: 'rm -rf /' } },
    details: { type: 'exec', title: 'Confirm', command: 'rm -rf /', rootCommand: 'rm' },
  },
  MessageBusType.TOOL_CONFIRMATION_RESPONSE,
  60000,
);
```

---

## Files Summary

| Directory | Files | Key Exports |
|-----------|-------|-------------|
| `agents/` | 15 | `AgentRegistry`, `LocalAgentExecutor`, `A2AClientManager` |
| `core/` | 12 | `GeminiChat`, `Turn`, `CoreToolScheduler` |
| `tools/` | 20+ | `ToolRegistry`, `BaseDeclarativeTool`, `ShellTool` |
| `scheduler/` | 8 | `Scheduler`, `SchedulerStateManager` |
| `policy/` | 6 | `PolicyEngine`, `PolicyRule`, `ApprovalMode` |
| `confirmation-bus/` | 3 | `MessageBus`, `MessageBusType` |
| `hooks/` | 10 | `HookSystem`, `HookRunner`, `HookRegistry` |
| `services/` | 14 | `ChatCompressionService`, `ShellExecutionService`, `ContextManager` |
| `mcp/` | 8 | `MCPOAuthProvider`, `MCPOAuthTokenStorage` |
| `availability/` | 4 | `ModelAvailabilityService` |
| `skills/` | 3 | `SkillManager`, `loadSkillsFromDir` |
| `config/` | 6 | `Config`, `Storage` |

---

*This analysis covers the primary architecture. For implementation details, refer to the source files in `docs/reference-code/gemini-cli/packages/core/src/`.*
