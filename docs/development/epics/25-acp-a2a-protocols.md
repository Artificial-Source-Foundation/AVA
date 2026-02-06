# Epic 25: ACP & A2A Protocol Support

> Make Estela work in any editor (ACP) and communicate with other agents (A2A)

**Research:** [`docs/analysis/gemini-cli/A2A-ACP-RESEARCH.md`](../../analysis/gemini-cli/A2A-ACP-RESEARCH.md)

---

## Overview

Two protocols that make Estela interoperable:

1. **ACP (Agent Client Protocol)** - Estela works inside Zed, JetBrains, Neovim, etc.
2. **A2A (Agent-to-Agent Protocol)** - Estela talks to other AI agents over HTTP

```
┌──────────┐  ACP   ┌──────────┐  A2A   ┌──────────────┐
│  Editor   │◄─────►│  Estela  │◄─────►│ Remote Agent  │
│(Zed/IDEA) │ stdio  │  Agent   │ HTTP   │(GPT/Gemini/..)│
└──────────┘        └────┬─────┘        └──────────────┘
                         │ MCP
                    ┌────▼─────┐
                    │MCP Server│
                    └──────────┘
```

---

## Sprint 1: ACP Agent (Editor Integration)

**Goal:** Estela runs inside any ACP-compatible editor

**Estimated scope:** ~1,200 lines

### 1.1 ACP Transport Layer (~300 lines)

**File:** `packages/core/src/acp/transport.ts`

JSON-RPC 2.0 over stdin/stdout:

```typescript
import { createInterface } from 'node:readline';

export class AcpTransport {
  private rl: ReturnType<typeof createInterface>;
  private pendingRequests: Map<string | number, {
    resolve: (result: unknown) => void;
    reject: (error: unknown) => void;
  }>;

  constructor(
    private input: NodeJS.ReadableStream = process.stdin,
    private output: NodeJS.WritableStream = process.stdout,
  ) {}

  /** Start listening for messages */
  start(): void;

  /** Send a JSON-RPC request and wait for response */
  async request(method: string, params?: unknown): Promise<unknown>;

  /** Send a JSON-RPC notification (no response expected) */
  notify(method: string, params?: unknown): void;

  /** Register handler for incoming requests */
  onRequest(method: string, handler: (params: unknown) => Promise<unknown>): void;

  /** Register handler for incoming notifications */
  onNotification(method: string, handler: (params: unknown) => void): void;
}
```

### 1.2 ACP Agent Core (~400 lines)

**File:** `packages/core/src/acp/agent.ts`

```typescript
export class AcpAgent {
  private transport: AcpTransport;
  private sessions: Map<string, AcpSession>;

  /** Handle initialize request - negotiate capabilities */
  async handleInitialize(params: InitializeRequest): Promise<InitializeResponse> {
    return {
      protocolVersion: 1,
      agentName: 'Estela',
      agentVersion: '1.0.0',
      capabilities: {
        streaming: true,
        sessionLoad: true,
        modes: ['agent', 'plan'],
      },
    };
  }

  /** Handle session/new - create conversation */
  async handleNewSession(params: NewSessionRequest): Promise<NewSessionResponse>;

  /** Handle session/prompt - process user message */
  async handlePrompt(params: PromptRequest): Promise<PromptResponse>;

  /** Handle session/cancel - abort processing */
  async handleCancel(params: CancelNotification): void;

  /** Handle session/set_mode - switch agent/plan mode */
  async handleSetMode(params: SetModeRequest): Promise<void>;
}
```

### 1.3 ACP Session (~300 lines)

**File:** `packages/core/src/acp/session.ts`

Bridge between ACP protocol and Estela's AgentExecutor:

```typescript
export class AcpSession {
  private agentExecutor: AgentExecutor;
  private transport: AcpTransport;

  /** Process a prompt through the agent */
  async prompt(params: PromptRequest): Promise<PromptResponse> {
    // 1. Parse prompt content (text, @path references)
    // 2. Run through AgentExecutor
    // 3. Stream updates via transport.notify('session/update', ...)
    // 4. Handle tool calls with permission requests
    // 5. Return stop reason
  }

  /** Request permission from editor for tool execution */
  async requestPermission(tool: string, args: Record<string, unknown>): Promise<boolean> {
    const result = await this.transport.request('session/request_permission', {
      tool,
      arguments: args,
      description: `Execute ${tool}`,
    });
    return result.granted;
  }

  /** Read file through editor's VFS */
  async readFile(path: string): Promise<string> {
    return await this.transport.request('fs/read_text_file', { path });
  }

  /** Write file through editor's VFS */
  async writeFile(path: string, content: string): Promise<void> {
    await this.transport.request('fs/write_text_file', { path, content });
  }
}
```

### 1.4 CLI Integration (~100 lines)

**File:** Update `cli/src/index.ts`

```typescript
// Add --acp flag
if (args.acp) {
  const { AcpAgent } = await import('@estela/core/acp');
  const agent = new AcpAgent(config);
  await agent.start();  // Blocks until stdin closes
  return;
}
```

### 1.5 ACP File System Bridge (~100 lines)

**File:** `packages/core/src/acp/file-system.ts`

```typescript
/** File system that routes through editor when available */
export class AcpFileSystem implements IFileSystem {
  constructor(
    private transport: AcpTransport,
    private clientCapabilities: ClientCapabilities,
    private fallback: IFileSystem,
  ) {}

  async readFile(path: string): Promise<string> {
    if (this.clientCapabilities.fs?.readTextFile) {
      return this.transport.request('fs/read_text_file', { path });
    }
    return this.fallback.readFile(path);
  }

  async writeFile(path: string, content: string): Promise<void> {
    if (this.clientCapabilities.fs?.writeTextFile) {
      return this.transport.request('fs/write_text_file', { path, content });
    }
    return this.fallback.writeFile(path, content);
  }
}
```

### Sprint 1 Acceptance Criteria

- [ ] `estela --acp` starts in ACP mode on stdin/stdout
- [ ] Handles `initialize` → `session/new` → `session/prompt` flow
- [ ] Streams `session/update` notifications during processing
- [ ] Routes tool approvals through `request_permission`
- [ ] Uses editor's filesystem when available (fallback to local)
- [ ] Supports `session/cancel` for aborting
- [ ] Works in Zed (primary test editor)
- [ ] Unit tests for transport, agent, session (~20 tests)

---

## Sprint 2: ACP Polish & IDE Features

**Goal:** Production-quality ACP with full editor integration

**Estimated scope:** ~800 lines

### 2.1 Session Persistence (~200 lines)

**File:** `packages/core/src/acp/session-store.ts`

```typescript
export class AcpSessionStore {
  /** Save session for later resume */
  async save(sessionId: string, state: SessionState): Promise<void>;

  /** Load previous session */
  async load(sessionId: string): Promise<SessionState | null>;

  /** List available sessions */
  async list(): Promise<SessionInfo[]>;
}
```

Enables `session/load` capability for resuming conversations.

### 2.2 Terminal Bridge (~200 lines)

**File:** `packages/core/src/acp/terminal.ts`

```typescript
export class AcpTerminal {
  /** Create terminal in editor */
  async create(name: string, cwd: string): Promise<string>;

  /** Write to terminal */
  async output(terminalId: string, data: string): Promise<void>;

  /** Wait for process exit */
  async waitForExit(terminalId: string): Promise<number>;

  /** Kill terminal process */
  async kill(terminalId: string): Promise<void>;
}
```

Routes bash tool execution through editor's integrated terminal.

### 2.3 Mode Switching (~100 lines)

**File:** Update `packages/core/src/acp/agent.ts`

Support `session/set_mode` for agent/plan mode switching from editor UI.

### 2.4 MCP Server Forwarding (~150 lines)

**File:** `packages/core/src/acp/mcp-bridge.ts`

Allow editor to pass MCP server configs to Estela sessions:

```typescript
// Editor sends MCP configs in session/new
{
  "mcpServers": [
    { "name": "github", "transport": "stdio", "command": "npx", "args": ["@github/mcp"] }
  ]
}
```

### 2.5 Error Handling & Reconnection (~150 lines)

- Graceful handling of editor disconnects
- Session state preservation on crash
- Error reporting via ACP error format

### Sprint 2 Acceptance Criteria

- [ ] Session resume works (`session/load`)
- [ ] Bash commands run in editor's terminal
- [ ] Plan/Agent mode switching from editor
- [ ] MCP servers from editor config work
- [ ] Graceful disconnect handling
- [ ] Works in JetBrains (secondary test editor)

---

## Sprint 3: A2A Server (Expose Estela as Agent)

**Goal:** Other agents can connect to Estela over HTTP

**Estimated scope:** ~1,500 lines

### 3.1 Agent Card (~100 lines)

**File:** `packages/core/src/a2a/agent-card.ts`

```typescript
export function createAgentCard(config: EstelaConfig): AgentCard {
  return {
    name: 'Estela',
    description: 'Multi-agent AI coding assistant with browser automation, fuzzy edits, and parallel execution',
    url: `http://localhost:${config.a2aPort}/`,
    protocolVersion: '0.3.0',
    capabilities: {
      streaming: true,
      pushNotifications: false,
      stateTransitionHistory: true,
    },
    skills: [
      {
        id: 'code-generation',
        name: 'Code Generation & Editing',
        description: 'Generate, edit, and refactor code with 8 fuzzy edit strategies',
        tags: ['code', 'edit', 'refactor'],
      },
      {
        id: 'browser-automation',
        name: 'Browser Automation',
        description: 'Automate web tasks with Puppeteer (click, type, screenshot)',
        tags: ['browser', 'testing', 'web'],
      },
      {
        id: 'code-search',
        name: 'Code & Documentation Search',
        description: 'Search codebases and documentation with Exa API',
        tags: ['search', 'docs', 'api'],
      },
    ],
    defaultInputModes: ['text'],
    defaultOutputModes: ['text'],
    authentication: {
      schemes: [{ scheme: 'bearer' }],
    },
  };
}
```

### 3.2 A2A HTTP Server (~400 lines)

**File:** `packages/core/src/a2a/server.ts`

```typescript
import express from 'express';

export class A2AServer {
  private app: express.Application;
  private executor: A2AExecutor;

  constructor(config: EstelaConfig) {
    this.app = express();
    this.setupRoutes();
  }

  private setupRoutes(): void {
    // Agent Card discovery
    this.app.get('/.well-known/agent.json', (req, res) => {
      res.json(createAgentCard(this.config));
    });

    // Core A2A endpoints
    this.app.post('/messages', this.handleSendMessage);
    this.app.post('/messages/stream', this.handleStreamMessage);
    this.app.get('/tasks/:id', this.handleGetTask);
    this.app.get('/tasks', this.handleListTasks);
    this.app.post('/tasks/:id\\:cancel', this.handleCancelTask);
    this.app.post('/tasks/:id/subscribe', this.handleSubscribe);
  }

  async start(port: number): Promise<void>;
  async stop(): Promise<void>;
}
```

### 3.3 A2A Executor (~400 lines)

**File:** `packages/core/src/a2a/executor.ts`

Bridge between A2A protocol and Estela's AgentExecutor:

```typescript
export class A2AExecutor {
  private tasks: Map<string, A2ATask>;

  /** Handle incoming message - create or update task */
  async execute(message: A2AMessage, eventBus: EventBus): Promise<void> {
    // 1. Create or retrieve task
    // 2. Run through AgentExecutor
    // 3. Stream events to client
    // 4. Update task state
  }

  /** Create new task */
  async createTask(contextId: string): Promise<A2ATask>;

  /** Cancel running task */
  async cancelTask(taskId: string): Promise<void>;
}
```

### 3.4 A2A Task Management (~300 lines)

**File:** `packages/core/src/a2a/task.ts`

```typescript
export class A2ATask {
  id: string;
  contextId: string;
  state: TaskState;
  messages: A2AMessage[];
  artifacts: Artifact[];

  /** Process user message through agent */
  async *processMessage(message: A2AMessage, signal: AbortSignal): AsyncGenerator<TaskEvent>;

  /** Update state and emit event */
  setState(state: TaskState, message?: A2AMessage): void;
}

export type TaskState =
  | 'submitted'
  | 'working'
  | 'input_required'
  | 'completed'
  | 'failed'
  | 'canceled'
  | 'rejected';
```

### 3.5 SSE Streaming (~200 lines)

**File:** `packages/core/src/a2a/streaming.ts`

```typescript
export class A2AStreamWriter {
  constructor(private res: express.Response) {
    res.writeHead(200, {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-cache',
      'Connection': 'keep-alive',
    });
  }

  /** Send task status update */
  sendStatusUpdate(taskId: string, state: TaskState, message?: A2AMessage): void {
    this.res.write(`event: task-status-update\ndata: ${JSON.stringify({
      taskId,
      status: { state, message, timestamp: new Date().toISOString() },
    })}\n\n`);
  }

  /** Send artifact update */
  sendArtifactUpdate(taskId: string, artifact: Artifact): void;

  /** Close stream */
  close(): void;
}
```

### 3.6 Authentication (~100 lines)

**File:** `packages/core/src/a2a/auth.ts`

Bearer token authentication for A2A endpoints.

### Sprint 3 Acceptance Criteria

- [ ] `estela --a2a-server` starts HTTP server
- [ ] Agent Card at `/.well-known/agent.json`
- [ ] `POST /messages` creates and executes tasks
- [ ] `POST /messages/stream` returns SSE stream
- [ ] `GET /tasks/{id}` returns task state
- [ ] `POST /tasks/{id}:cancel` cancels running tasks
- [ ] Bearer token auth works
- [ ] Unit tests (~25 tests)

---

## Sprint 4: A2A Client (Connect to Remote Agents)

**Goal:** Estela can delegate tasks to other AI agents

**Estimated scope:** ~800 lines

### 4.1 A2A Client Manager (~300 lines)

**File:** `packages/core/src/a2a/client.ts`

```typescript
export class A2AClientManager {
  private clients: Map<string, A2AClient>;
  private agentCards: Map<string, AgentCard>;

  /** Discover and cache remote agent */
  async loadAgent(name: string, url: string): Promise<AgentCard>;

  /** Send message to remote agent */
  async sendMessage(
    agentName: string,
    message: string,
    options?: { contextId?: string; taskId?: string },
  ): Promise<A2AResponse>;

  /** Stream message to remote agent */
  async *streamMessage(
    agentName: string,
    message: string,
    options?: { contextId?: string; taskId?: string },
  ): AsyncGenerator<TaskEvent>;

  /** Get task status */
  async getTask(agentName: string, taskId: string): Promise<A2ATask>;

  /** Cancel remote task */
  async cancelTask(agentName: string, taskId: string): Promise<void>;
}
```

### 4.2 Remote Agent Tool (~200 lines)

**File:** `packages/core/src/tools/remote-agent.ts`

Wrap remote A2A agents as Estela tools:

```typescript
export const remoteAgentTool = defineTool({
  name: 'delegate_remote',
  description: 'Delegate task to a remote AI agent via A2A protocol',
  parameters: z.object({
    agent: z.string().describe('Agent name or URL'),
    message: z.string().describe('Task description'),
    contextId: z.string().optional().describe('Continue existing conversation'),
  }),
  async execute({ agent, message, contextId }) {
    const clientManager = A2AClientManager.getInstance();
    const response = await clientManager.sendMessage(agent, message, { contextId });
    return formatA2AResponse(response);
  },
});
```

### 4.3 Agent Registry (~200 lines)

**File:** `packages/core/src/a2a/registry.ts`

Discover and manage available remote agents:

```typescript
export class A2AAgentRegistry {
  private agents: Map<string, RegisteredAgent>;

  /** Register agent by URL */
  async register(name: string, url: string): Promise<AgentCard>;

  /** Discover agents from config */
  async discoverFromConfig(config: EstelaConfig): Promise<void>;

  /** List registered agents */
  list(): RegisteredAgent[];

  /** Get agent card */
  getCard(name: string): AgentCard | undefined;
}
```

### 4.4 Response Parsing (~100 lines)

**File:** `packages/core/src/a2a/utils.ts`

Parse A2A response parts into tool-friendly format.

### Sprint 4 Acceptance Criteria

- [ ] `delegate_remote` tool available in agent mode
- [ ] Connects to any A2A-compliant agent
- [ ] Maintains conversation context (contextId/taskId)
- [ ] Handles streaming responses
- [ ] Agent registry with config-based discovery
- [ ] Unit tests (~15 tests)

---

## Sprint 5: Integration & Polish

**Goal:** End-to-end testing, documentation, and configuration

**Estimated scope:** ~500 lines

### 5.1 Configuration (~150 lines)

Update `packages/core/src/config/schema.ts`:

```typescript
// ACP settings
acp: {
  enabled: boolean;
},

// A2A settings
a2a: {
  server: {
    enabled: boolean;
    port: number;  // default: 41242
    authToken: string;
  },
  agents: Record<string, {
    url: string;
    authToken?: string;
  }>,
},
```

### 5.2 Settings UI (~150 lines)

Update settings panel to show ACP/A2A configuration.

### 5.3 Integration Tests (~200 lines)

- ACP: Simulate editor ↔ agent JSON-RPC conversation
- A2A: Test agent card, message sending, streaming
- Cross-protocol: ACP agent delegates to A2A remote agent

### Sprint 5 Acceptance Criteria

- [ ] ACP/A2A settings configurable
- [ ] End-to-end test: Editor → Estela (ACP) → Remote Agent (A2A)
- [ ] Documentation updated
- [ ] Memory bank updated

---

## Summary

| Sprint | Focus | Lines | Dependencies |
|--------|-------|-------|--------------|
| 1 | ACP Agent (core) | ~1,200 | `@agentclientprotocol/sdk` |
| 2 | ACP Polish | ~800 | Sprint 1 |
| 3 | A2A Server | ~1,500 | `@a2a-js/sdk`, `express` |
| 4 | A2A Client | ~800 | Sprint 3 |
| 5 | Integration | ~500 | Sprint 2 + 4 |
| **Total** | | **~4,800** | |

### New Dependencies

| Package | Version | Purpose |
|---------|---------|---------|
| `@agentclientprotocol/sdk` | latest | ACP protocol types + helpers |
| `@a2a-js/sdk` | ^0.3.8 | A2A protocol types + helpers |
| `express` | ^5.1.0 | HTTP server for A2A |

### New Files (~25)

```
packages/core/src/acp/
├── transport.ts        # JSON-RPC over stdio
├── agent.ts            # ACP agent handler
├── session.ts          # ACP session (bridges to AgentExecutor)
├── session-store.ts    # Session persistence
├── file-system.ts      # Editor VFS bridge
├── terminal.ts         # Editor terminal bridge
└── mcp-bridge.ts       # MCP server forwarding

packages/core/src/a2a/
├── agent-card.ts       # Agent Card definition
├── server.ts           # HTTP server
├── executor.ts         # Task execution bridge
├── task.ts             # Task lifecycle
├── streaming.ts        # SSE streaming
├── auth.ts             # Bearer auth
├── client.ts           # A2A client manager
├── registry.ts         # Agent discovery
└── utils.ts            # Response parsing

packages/core/src/tools/
└── remote-agent.ts     # delegate_remote tool
```

### Modified Files (~5)

```
cli/src/index.ts                    # Add --acp flag
packages/core/src/tools/index.ts    # Register remote-agent tool
packages/core/src/config/schema.ts  # ACP/A2A settings
```

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| ACP SDK breaking changes | Medium | Pin version, test against Zed |
| A2A spec evolving (v0.3 → v1.0) | Medium | Abstract behind interface |
| Editor compatibility issues | Low | Test with Zed first, then JetBrains |
| Auth complexity (A2A) | Low | Start with bearer token only |

---

*Created: 2026-02-05*
