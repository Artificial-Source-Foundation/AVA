# A2A & ACP Protocol Research

> Deep research into Agent-to-Agent and Agent Client Protocol for AVA implementation

---

## Protocol Landscape (2026)

The AI agent ecosystem has converged on three complementary protocols:

| Protocol | Focus | Transport | Status |
|----------|-------|-----------|--------|
| **MCP** (Anthropic) | Agent ↔ Tool | JSON-RPC over stdio/HTTP | AVA has this |
| **ACP** (Zed/JetBrains) | Agent ↔ Editor | JSON-RPC over stdio | New for AVA |
| **A2A** (Google/Linux Foundation) | Agent ↔ Agent | HTTP/SSE/gRPC | New for AVA |

```
                    ┌──────────────┐
                    │  Code Editor  │  (Zed, JetBrains, VS Code, Neovim)
                    │  (ACP Client) │
                    └──────┬───────┘
                           │ ACP (JSON-RPC over stdio)
                           │
                    ┌──────▼───────┐
                    │   AVA     │
                    │  (AI Agent)  │
                    └──┬───────┬───┘
                       │       │
          MCP          │       │  A2A
     (Tools/Context)   │       │  (Agent-to-Agent)
                       │       │
              ┌────────▼──┐  ┌─▼───────────────┐
              │ MCP Server │  │  Remote Agents   │
              │ (Tools)    │  │  (Other AI)      │
              └────────────┘  └──────────────────┘
```

---

## Part 1: A2A (Agent-to-Agent Protocol)

### What It Is

A2A is an open protocol by Google (now Linux Foundation) for multi-agent communication. It allows AI agents to discover each other, delegate tasks, and collaborate without exposing internal state.

**Key principle:** Agents are **opaque** - they collaborate without sharing memory, tools, or internal logic.

### Protocol Specification (v0.3.0)

#### Agent Card Discovery

Every A2A agent publishes a JSON file at `/.well-known/agent.json`:

```json
{
  "name": "AVA Coding Agent",
  "description": "AI coding assistant with browser automation, fuzzy edits, and parallel execution",
  "url": "http://localhost:41242/",
  "protocolVersion": "0.3.0",
  "capabilities": {
    "streaming": true,
    "pushNotifications": false,
    "stateTransitionHistory": true
  },
  "skills": [
    {
      "id": "code-generation",
      "name": "Code Generation",
      "description": "Generate, edit, and refactor code across multiple languages",
      "tags": ["code", "typescript", "python", "rust"],
      "examples": [
        "Fix the authentication bug in login.ts",
        "Add unit tests for the user service"
      ]
    },
    {
      "id": "browser-automation",
      "name": "Browser Automation",
      "description": "Automate web browser tasks with Puppeteer",
      "tags": ["browser", "testing", "screenshots"]
    }
  ],
  "defaultInputModes": ["text"],
  "defaultOutputModes": ["text"],
  "authentication": {
    "schemes": [
      { "scheme": "bearer", "description": "API key authentication" }
    ]
  }
}
```

#### Task Lifecycle

```
submitted → working → input_required → completed
                  ↓         ↓              ↑
                  └─────→ failed ←─────────┘
                  └─────→ canceled
                  └─────→ rejected
```

#### Core API Endpoints (HTTP/REST binding)

| Method | Endpoint | Purpose |
|--------|----------|---------|
| POST | `/messages` | Send message to agent |
| POST | `/messages/stream` | Send with SSE streaming |
| GET | `/tasks/{id}` | Get task state |
| GET | `/tasks` | List tasks (with filters) |
| POST | `/tasks/{id}:cancel` | Cancel task |
| POST | `/tasks/{id}/subscribe` | Subscribe to updates (SSE) |

#### Message Format

```json
{
  "role": "user",
  "parts": [
    { "type": "text", "text": "Fix the login bug" },
    { "type": "file", "name": "error.log", "mimeType": "text/plain", "data": "..." }
  ],
  "contextId": "ctx-123",
  "taskId": "task-456",
  "metadata": { "priority": "high" }
}
```

#### Streaming (SSE)

```
event: task-status-update
data: {"taskId":"t1","status":{"state":"working","message":{"role":"agent","parts":[{"type":"text","text":"Analyzing code..."}]}}}

event: task-artifact-update
data: {"taskId":"t1","artifact":{"name":"fix.patch","mimeType":"text/x-diff","data":"..."}}

event: task-status-update
data: {"taskId":"t1","status":{"state":"completed","message":{"role":"agent","parts":[{"type":"text","text":"Fixed the login bug"}]}}}
```

### How Gemini CLI Implements A2A

**Server-side (`packages/a2a-server/`):**

1. **CoderAgentExecutor** - Implements `AgentExecutor` from `@a2a-js/sdk/server`
   - Manages `TaskWrapper` instances (wraps internal Task + settings)
   - Handles: `execute()`, `createTask()`, `reconstruct()`, `cancelTask()`
   - Uses `ExecutionEventBus` for streaming results

2. **Task** - Session state with tool execution
   - States: submitted → working → input_required → completed/failed
   - Batched tool execution with checkpointing
   - Promise-based tool completion tracking
   - Auto-approval (YOLO) mode per-task

3. **Persistence** - GCS with gzip compression
   - Metadata: `gs://bucket/tasks/{taskId}/metadata.tar.gz`
   - Workspace: `gs://bucket/tasks/{taskId}/workspace.tar.gz`
   - In-memory fallback for development

**Client-side (`packages/core/src/agents/`):**

1. **A2AClientManager** - Singleton for remote agent connections
   - Caches agent cards and clients
   - Supports REST and JSON-RPC transports
   - Authentication handlers

2. **RemoteAgentInvocation** - Wraps remote agents as tools
   - Maintains contextId/taskId for continuity
   - Extracts text from parts for LLM consumption

### What AVA Needs

**A2A Server** (expose AVA as an agent):
- Agent Card at `/.well-known/agent.json`
- HTTP/REST endpoints for messages, tasks, streaming
- Task lifecycle management
- SSE streaming for real-time updates

**A2A Client** (connect to remote agents):
- Agent discovery from URLs
- Message sending with context/task tracking
- Streaming response handling
- Tool wrapper for remote agents

---

## Part 2: ACP (Agent Client Protocol)

### What It Is

ACP is a protocol for connecting AI coding agents to code editors. Think of it as **LSP for AI agents** - any agent can work in any supporting editor.

**Key participants:**
- **Agent** (AVA): Processes prompts, executes tools, modifies code
- **Client** (Editor): Manages UI, file access, terminal, permissions

### Protocol Specification

#### Transport

JSON-RPC 2.0 over **stdin/stdout** (subprocess model):
```
Editor spawns: ava --experimental-acp
                ↕ stdin/stdout (JSON-RPC)
```

#### Session Lifecycle

```
Client                          Agent
  │                               │
  ├── initialize ──────────────►  │  Negotiate capabilities
  │  ◄──────────── response ──── │
  │                               │
  ├── authenticate ────────────► │  (optional)
  │  ◄──────────── response ──── │
  │                               │
  ├── session/new ─────────────► │  Create session
  │  ◄──────────── response ──── │
  │                               │
  ├── session/prompt ──────────► │  Send user prompt
  │                               │
  │  ◄─── session/update ─────── │  (notification: streaming)
  │  ◄─── session/update ─────── │  (notification: tool call)
  │                               │
  │  ◄─── request_permission ──── │  Agent asks for permission
  ├── permission response ─────► │
  │                               │
  │  ◄─── fs/read_text_file ───── │  Agent reads file via editor
  ├── file content ────────────► │
  │                               │
  │  ◄─── session/update ─────── │  (notification: complete)
  │  ◄──────────── response ──── │  Prompt response (stop reason)
  │                               │
```

#### Agent-Exposed Methods

| Method | Purpose |
|--------|---------|
| `initialize` | Negotiate protocol version & capabilities |
| `authenticate` | Credential verification |
| `session/new` | Create new conversation session |
| `session/prompt` | Process user prompt |
| `session/load` | Resume previous session (optional) |
| `session/cancel` | Cancel current processing |
| `session/set_mode` | Adjust operating mode (optional) |

#### Client-Exposed Methods

| Method | Purpose |
|--------|---------|
| `session/request_permission` | Agent asks editor for tool approval |
| `fs/read_text_file` | Read file through editor's VFS |
| `fs/write_text_file` | Write file through editor's VFS |
| `terminal/create` | Create terminal in editor |
| `terminal/output` | Write to terminal |
| `terminal/release` | Release terminal |
| `terminal/wait_for_exit` | Wait for terminal process to exit |
| `terminal/kill` | Kill terminal process |

#### Capabilities Negotiation

```json
// Agent → Client (initialize response)
{
  "protocolVersion": 1,
  "agentName": "AVA",
  "agentVersion": "1.0.0",
  "capabilities": {
    "streaming": true,
    "sessionLoad": true,
    "modes": ["agent", "plan"]
  }
}
```

### How Gemini CLI Implements ACP

**GeminiAgent class** (`packages/cli/src/zed-integration/zedIntegration.ts`):

```typescript
class GeminiAgent {
  sessions: Map<string, Session>;
  clientCapabilities: acp.ClientCapabilities;

  async initialize(args: acp.InitializeRequest): Promise<acp.InitializeResponse>;
  async authenticate({ methodId }: acp.AuthenticateRequest): Promise<void>;
  async newSession({ cwd, mcpServers }: acp.NewSessionRequest): Promise<acp.NewSessionResponse>;
  async cancel(params: acp.CancelNotification): Promise<void>;
  async prompt(params: acp.PromptRequest): Promise<acp.PromptResponse>;
}
```

**Session class** - Per-conversation state:
- Processes `@path` references in prompts (file injection)
- Streams LLM responses via `sessionUpdate` notifications
- Handles tool calls with permission flow via `requestPermission`
- Supports MCP servers passed from editor

**AcpFileSystemService** - Bridges ACP to file operations:
```typescript
class AcpFileSystemService implements FileSystemService {
  async readTextFile(filePath: string): Promise<string>;
  async writeTextFile(filePath: string, content: string): Promise<void>;
}
```

### Supported Editors (January 2026)

| Editor | Status | Notes |
|--------|--------|-------|
| **Zed** | Full support | Co-created ACP |
| **JetBrains** | Full support | ACP Agent Registry |
| **Neovim** | Supported | Community plugin |
| **Marimo** | Supported | Notebook editor |

### Supported Agents

| Agent | Status |
|-------|--------|
| Claude Code | Supported |
| Codex CLI | Supported |
| Gemini CLI | Supported |
| goose | Supported |
| StackPack | Supported |

### What AVA Needs

1. **ACP Agent Implementation** - Handle JSON-RPC over stdin/stdout
2. **Session Management** - Create, prompt, cancel sessions
3. **Permission Bridge** - Route tool approvals through editor
4. **File System Bridge** - Use editor's VFS for file operations
5. **CLI flag** - `--acp` to start in ACP mode

---

## Part 3: Implementation Priority

### Why ACP First

1. **Immediate user value** - AVA works in Zed, JetBrains, Neovim
2. **Simpler protocol** - JSON-RPC over stdio, no HTTP server needed
3. **Complementary to existing CLI** - Just a new mode
4. **SDK available** - `@agentclientprotocol/sdk` (TypeScript)

### Why A2A Second

1. **More complex** - Requires HTTP server, task persistence, streaming
2. **Longer-term value** - Multi-agent orchestration
3. **Building on ACP** - Same session/tool patterns
4. **SDK available** - `@a2a-js/sdk` (TypeScript)

---

## Sources

- [A2A Protocol Specification](https://a2a-protocol.org/latest/specification/)
- [A2A GitHub Repository](https://github.com/a2aproject/A2A)
- [Google Developers Blog - Announcing A2A](https://developers.googleblog.com/en/a2a-a-new-era-of-agent-interoperability/)
- [A2A Protocol Upgrade (v0.3)](https://cloud.google.com/blog/products/ai-machine-learning/agent2agent-protocol-is-getting-an-upgrade)
- [ACP Protocol Overview](https://agentclientprotocol.com/protocol/overview)
- [ACP GitHub Repository](https://github.com/agentclientprotocol/agent-client-protocol)
- [Intro to ACP (goose blog)](https://block.github.io/goose/blog/2025/10/24/intro-to-agent-client-protocol-acp/)
- [JetBrains ACP Agent Registry](https://blog.jetbrains.com/ai/2026/01/acp-agent-registry/)
- [JetBrains ACP Documentation](https://www.jetbrains.com/help/ai-assistant/acp.html)
- [AI Agent Protocols 2026 Guide](https://www.ruh.ai/blogs/ai-agent-protocols-2026-complete-guide)
- [Linux Foundation A2A Project](https://www.linuxfoundation.org/press/linux-foundation-launches-the-agent2agent-protocol-project-to-enable-secure-intelligent-communication-between-ai-agents)

---

*Research completed: 2026-02-05*
