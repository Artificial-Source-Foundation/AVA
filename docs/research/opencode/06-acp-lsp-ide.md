# OpenCode ACP, LSP, IDE & Skills Analysis

This document provides an in-depth analysis of OpenCode's ACP (Agent Communication Protocol), LSP (Language Server Protocol) integration, IDE integrations, Skill system, and Question/Clarification flow.

---

## ACP (Agent Communication Protocol)

### Overview

ACP (Agent Communication Protocol) is OpenCode's standardized interface for communication between AI coding agents and client applications. It's implemented using the `@agentclientprotocol/sdk` package and enables clients (like IDEs, CLIs, or web interfaces) to interact with OpenCode's AI agent backend through a well-defined protocol.

### Key Files

- `/packages/opencode/src/acp/types.ts` - Type definitions
- `/packages/opencode/src/acp/session.ts` - Session management
- `/packages/opencode/src/acp/agent.ts` - Main ACP agent implementation

### Core Types

```typescript
interface ACPSessionState {
  id: string
  cwd: string                    // Working directory
  mcpServers: McpServer[]        // Connected MCP servers
  createdAt: Date
  model?: {
    providerID: string
    modelID: string
  }
  variant?: string               // Model variant (e.g., "high", "low")
  modeId?: string                // Agent mode identifier
}

interface ACPConfig {
  sdk: OpencodeClient            // OpenCode SDK client
  defaultModel?: {
    providerID: string
    modelID: string
  }
}
```

### Session Management

The `ACPSessionManager` class manages ACP sessions:

1. **Session Creation** (`create`):
   - Creates a new session via the OpenCode SDK
   - Stores session state in memory with working directory, MCP servers, and model info
   - Returns session state for client use

2. **Session Loading** (`load`):
   - Loads an existing session by ID
   - Reconstructs session state from persisted data
   - Useful for resuming conversations

3. **Model Management**:
   - `getModel/setModel` - Get/set the model for a session
   - `getVariant/setVariant` - Get/set model variant
   - `setMode` - Set the agent mode for a session

### ACP Agent Implementation

The main `Agent` class implements the `ACPAgent` interface:

#### Initialization

```typescript
async initialize(params: InitializeRequest): Promise<InitializeResponse>
```

Returns agent capabilities:
- Protocol version: 1
- Session capabilities: fork, list, resume
- MCP capabilities: HTTP, SSE
- Prompt capabilities: embedded context, images
- Auth methods: `opencode auth login` command

#### Session Operations

1. **New Session** (`newSession`):
   - Creates session via session manager
   - Loads available modes and models
   - Returns session ID, models, and modes

2. **Load Session** (`loadSession`):
   - Loads existing session
   - Replays session history (messages)
   - Restores mode and model state

3. **Fork Session** (`unstable_forkSession`):
   - Creates a copy of an existing session
   - Preserves conversation history in the fork
   - Useful for branching conversations

4. **Resume Session** (`unstable_resumeSession`):
   - Resumes a session without replaying history
   - Faster than full load

5. **List Sessions** (`unstable_listSessions`):
   - Returns paginated list of sessions
   - Sorted by update time, most recent first

#### Event Handling

The agent subscribes to OpenCode events and translates them to ACP format:

1. **Permission Events** (`permission.asked`):
   - Queues permission requests per session
   - Forwards to ACP client via `requestPermission`
   - Handles allow once, always allow, and reject
   - For edit permissions, applies diffs to files

2. **Message Part Events** (`message.part.updated`):
   - Handles tool calls (pending, running, completed, error)
   - Handles text chunks (agent messages)
   - Handles reasoning/thinking chunks
   - Maps tool names to ACP tool kinds

#### Tool Kind Mapping

```typescript
function toToolKind(toolName: string): ToolKind {
  switch (tool) {
    case "bash":        return "execute"
    case "webfetch":    return "fetch"
    case "edit":
    case "patch":
    case "write":       return "edit"
    case "grep":
    case "glob":        return "search"
    case "list":
    case "read":        return "read"
    default:            return "other"
  }
}
```

#### Prompting

The `prompt` method handles user input:

1. Parses prompt parts (text, images, resource links, resources)
2. Detects slash commands (e.g., `/compact`)
3. Routes to appropriate handler:
   - Commands: Executes via SDK
   - Regular prompts: Sends to AI via session prompt
4. Handles audience annotations for message visibility

#### Permission Options

Default permission options provided to clients:
- `once` (allow_once) - Allow this action once
- `always` (allow_always) - Always allow this action
- `reject` (reject_once) - Reject this action

### Model Selection

The ACP supports model variants and intelligent model selection:

```typescript
function parseModelSelection(modelId: string, providers: Provider[]): {
  model: { providerID: string; modelID: string }
  variant?: string
}
```

- Parses model IDs like `anthropic/claude-sonnet-4/high`
- Extracts base model and variant
- Falls back to default model if specified model not found

---

## LSP Integration

### Overview

OpenCode integrates with Language Server Protocol to provide code intelligence features like diagnostics, hover information, go-to-definition, and symbol search. The LSP system automatically spawns and manages language servers for supported languages.

### Key Files

- `/packages/opencode/src/lsp/index.ts` - Main LSP coordinator
- `/packages/opencode/src/lsp/server.ts` - Server definitions (30+ language servers)
- `/packages/opencode/src/lsp/client.ts` - LSP client implementation
- `/packages/opencode/src/lsp/language.ts` - Language/extension mappings

### Architecture

```
                    LSP Namespace
                         |
            +------------+------------+
            |                         |
     LSPClient (per server)    LSPServer definitions
            |
    vscode-jsonrpc/node
            |
    Language Server Process
```

### Supported Languages (30+ servers)

| Server ID | Languages | Auto-Download |
|-----------|-----------|---------------|
| `typescript` | .ts, .tsx, .js, .jsx, .mjs, .cjs | No (uses project's TS) |
| `deno` | .ts, .tsx, .js, .jsx | No |
| `vue` | .vue | Yes |
| `eslint` | .ts, .tsx, .js, .jsx, .vue | Yes |
| `oxlint` | .ts, .tsx, .js, .jsx, .vue, .astro, .svelte | No |
| `biome` | .ts, .tsx, .json, .vue, .astro, .svelte, .css | No |
| `gopls` | .go | Yes |
| `rust` | .rs | No |
| `pyright` | .py, .pyi | Yes |
| `ty` | .py, .pyi | No (experimental) |
| `elixir-ls` | .ex, .exs | Yes |
| `zls` | .zig, .zon | Yes |
| `csharp` | .cs | Yes |
| `fsharp` | .fs, .fsi, .fsx | Yes |
| `sourcekit-lsp` | .swift | No |
| `clangd` | .c, .cpp, .h, .hpp | Yes |
| `svelte` | .svelte | Yes |
| `astro` | .astro | Yes |
| `jdtls` | .java | Yes |
| `kotlin-ls` | .kt, .kts | Yes |
| `yaml-ls` | .yaml, .yml | Yes |
| `lua-ls` | .lua | Yes |
| `php` (intelephense) | .php | Yes |
| `prisma` | .prisma | No |
| `dart` | .dart | No |
| `ocaml-lsp` | .ml, .mli | No |
| `bash` | .sh, .bash, .zsh | Yes |
| `terraform` | .tf, .tfvars | Yes |
| `texlab` | .tex, .bib | Yes |
| `dockerfile` | Dockerfile | Yes |
| `gleam` | .gleam | No |
| `clojure-lsp` | .clj, .cljs | No |
| `nixd` | .nix | No |
| `tinymist` | .typ (Typst) | Yes |
| `haskell-language-server` | .hs, .lhs | No |
| `ruby-lsp` (rubocop) | .rb, .rake | Yes |

### Root Detection

Each server has a `root` function that determines the workspace root:

```typescript
// Example: TypeScript uses nearest lockfile, excludes Deno projects
const NearestRoot = (includePatterns: string[], excludePatterns?: string[]): RootFunction
```

Pattern-based detection:
- TypeScript: `package-lock.json`, `bun.lockb`, `pnpm-lock.yaml`, `yarn.lock`
- Go: `go.mod`, `go.sum`, `go.work`
- Rust: `Cargo.toml`, workspace detection

### LSP Client

The `LSPClient` creates and manages connections to language servers:

```typescript
export async function create(input: {
  serverID: string
  server: LSPServer.Handle
  root: string
}): Promise<LSPClient.Info>
```

#### Initialization

1. Creates JSON-RPC message connection over stdio
2. Sends `initialize` request with capabilities:
   - `textDocument.synchronization`
   - `textDocument.publishDiagnostics`
   - `workspace.configuration`
   - `workspace.didChangeWatchedFiles`
3. Sends `initialized` notification
4. Applies server-specific initialization options

#### File Operations

```typescript
notify: {
  async open(input: { path: string }): Promise<void>
}
```

- Tracks file versions
- Sends `textDocument/didOpen` for new files
- Sends `textDocument/didChange` for modified files
- Sends `workspace/didChangeWatchedFiles` notifications

#### Diagnostics

- Listens for `textDocument/publishDiagnostics`
- Stores diagnostics per file path
- Publishes bus events for diagnostic updates
- Supports waiting for diagnostics with debouncing (150ms)

### LSP Operations

The main `LSP` namespace exposes:

```typescript
// File operations
touchFile(input: string, waitForDiagnostics?: boolean): Promise<void>
hasClients(file: string): Promise<boolean>

// Diagnostics
diagnostics(): Promise<Record<string, Diagnostic[]>>

// Code intelligence
hover(input: { file, line, character }): Promise<HoverResult>
definition(input: { file, line, character }): Promise<Location[]>
references(input: { file, line, character }): Promise<Location[]>
implementation(input: { file, line, character }): Promise<Location[]>

// Symbols
workspaceSymbol(query: string): Promise<Symbol[]>
documentSymbol(uri: string): Promise<DocumentSymbol[]>

// Call hierarchy
prepareCallHierarchy(input): Promise<CallHierarchyItem[]>
incomingCalls(input): Promise<CallHierarchyIncomingCall[]>
outgoingCalls(input): Promise<CallHierarchyOutgoingCall[]>
```

### Automatic Download

Many servers support automatic download from GitHub releases:
- `gopls`: `go install golang.org/x/tools/gopls@latest`
- `zls`: Downloads pre-built binary from GitHub
- `clangd`: Downloads from GitHub releases
- `lua-ls`: Downloads with supporting files
- Node-based servers: `bun install` in global bin directory

Can be disabled with `OPENCODE_DISABLE_LSP_DOWNLOAD` flag.

### Experimental Features

- `ty` server (Python type checker) requires `OPENCODE_EXPERIMENTAL_LSP_TY` flag
- When enabled, disables `pyright` automatically

---

## IDE Integrations

### Overview

OpenCode provides integration with VS Code-based editors through a dedicated VS Code extension (`sst-dev.opencode`). The IDE module handles detection, installation, and communication.

### Key File

- `/packages/opencode/src/ide/index.ts`

### Supported IDEs

| IDE | Command |
|-----|---------|
| Windsurf | `windsurf` |
| VS Code Insiders | `code-insiders` |
| VS Code | `code` |
| Cursor | `cursor` |
| VSCodium | `codium` |

### IDE Detection

```typescript
export function ide(): string
```

Detection via environment variables:
1. Checks `TERM_PROGRAM === "vscode"`
2. Parses `GIT_ASKPASS` to identify specific IDE
3. Returns IDE name or "unknown"

### Extension Installation

```typescript
export async function install(ide: IDEName): Promise<void>
```

Installs the OpenCode VS Code extension:
```bash
code --install-extension sst-dev.opencode
```

Error handling:
- `AlreadyInstalledError` - Extension already installed
- `InstallFailedError` - Installation failed (includes stderr)

### Caller Detection

```typescript
export function alreadyInstalled(): boolean
```

Checks `OPENCODE_CALLER` environment variable:
- `"vscode"` - Called from VS Code extension
- `"vscode-insiders"` - Called from VS Code Insiders extension

### Events

```typescript
Event.Installed: BusEvent<{ ide: string }>
```

Published when an IDE extension is successfully installed.

---

## Skill System

### Overview

Skills are reusable knowledge modules that extend the AI agent's capabilities. They're defined as Markdown files with YAML frontmatter and can be loaded from multiple locations.

### Key Files

- `/packages/opencode/src/skill/index.ts` - Re-export
- `/packages/opencode/src/skill/skill.ts` - Main implementation

### Skill Format

Skills are defined in `SKILL.md` files:

```markdown
---
name: typescript-patterns
description: Advanced TypeScript patterns and best practices
---

# TypeScript Patterns

[Skill content...]
```

### Schema

```typescript
const Info = z.object({
  name: z.string(),          // Unique skill identifier
  description: z.string(),   // Brief description
  location: z.string(),      // File path
})
```

### Skill Discovery

Skills are discovered from multiple locations in order:

1. **Claude Code Skills** (`.claude/skills/`):
   - Project-level: `.claude/skills/**/SKILL.md`
   - Global: `~/.claude/skills/**/SKILL.md`
   - Can be disabled with `OPENCODE_DISABLE_CLAUDE_CODE_SKILLS`

2. **OpenCode Skills** (`.opencode/skill/` or `.opencode/skills/`):
   - Scans all config directories
   - Pattern: `{skill,skills}/**/SKILL.md`

3. **Custom Skill Paths**:
   - Defined in config: `skills.paths`
   - Supports `~/` home directory expansion
   - Pattern: `**/SKILL.md`

### Skill Loading

```typescript
const addSkill = async (match: string) => {
  const md = await ConfigMarkdown.parse(match)
  const parsed = Info.pick({ name: true, description: true }).safeParse(md.data)

  if (skills[parsed.data.name]) {
    log.warn("duplicate skill name", { ... })
  }

  skills[parsed.data.name] = {
    name: parsed.data.name,
    description: parsed.data.description,
    location: match,
  }
}
```

- Parses Markdown with YAML frontmatter
- Validates against schema
- Warns on duplicate names (last wins)

### API

```typescript
// Get a specific skill by name
Skill.get(name: string): Promise<Info | undefined>

// Get all skills
Skill.all(): Promise<Info[]>
```

### Error Handling

```typescript
// Invalid skill file (parse error or schema mismatch)
SkillInvalidError: {
  path: string
  message?: string
  issues?: ZodIssue[]
}

// Skill name doesn't match filename (optional validation)
SkillNameMismatchError: {
  path: string
  expected: string
  actual: string
}
```

---

## Question Flow

### Overview

The Question system enables the AI agent to ask the user clarifying questions during a conversation. It supports multiple-choice questions with optional custom text input.

### Key File

- `/packages/opencode/src/question/index.ts`

### Question Schema

```typescript
const Option = z.object({
  label: z.string(),       // Display text (1-5 words)
  description: z.string(), // Explanation of choice
})

const Info = z.object({
  question: z.string(),    // Complete question text
  header: z.string(),      // Short label (max 30 chars)
  options: Option[],       // Available choices
  multiple?: boolean,      // Allow multiple selections
  custom?: boolean,        // Allow custom text input (default: true)
})

const Request = z.object({
  id: string,              // Question request ID
  sessionID: string,       // Associated session
  questions: Info[],       // Questions to ask
  tool?: {                 // Optional tool context
    messageID: string,
    callID: string,
  },
})
```

### Answer Format

```typescript
const Answer = z.array(z.string())  // Array of selected labels or custom text

const Reply = z.object({
  answers: Answer[]  // One Answer array per question
})
```

### Flow

```
Agent                    Question Module                    Client/UI
  |                            |                                |
  |--- ask(questions) -------->|                                |
  |                            |--- Event.Asked --------------->|
  |                            |                                |
  |                            |<-- reply(answers) -------------|
  |<-- Promise resolves -------|--- Event.Replied ------------->|
  |                            |                                |
  |                            |        OR                      |
  |                            |                                |
  |                            |<-- reject(requestID) ----------|
  |<-- RejectedError ----------|--- Event.Rejected ------------>|
```

### API

#### Asking Questions

```typescript
async function ask(input: {
  sessionID: string
  questions: Info[]
  tool?: { messageID: string; callID: string }
}): Promise<Answer[]>
```

- Creates a pending question request
- Publishes `Event.Asked` for UI to display
- Returns Promise that resolves when user replies

#### Replying

```typescript
async function reply(input: {
  requestID: string
  answers: Answer[]
}): Promise<void>
```

- Looks up pending request
- Resolves the ask() Promise with answers
- Publishes `Event.Replied`

#### Rejecting

```typescript
async function reject(requestID: string): Promise<void>
```

- Looks up pending request
- Rejects the ask() Promise with `RejectedError`
- Publishes `Event.Rejected`

#### Listing Pending Questions

```typescript
async function list(): Promise<Request[]>
```

Returns all pending question requests.

### Events

```typescript
Event.Asked: BusEvent<Request>
// Published when agent asks a question

Event.Replied: BusEvent<{
  sessionID: string
  requestID: string
  answers: Answer[]
}>
// Published when user replies

Event.Rejected: BusEvent<{
  sessionID: string
  requestID: string
}>
// Published when user dismisses/rejects
```

### Error Handling

```typescript
class RejectedError extends Error {
  constructor() {
    super("The user dismissed this question")
  }
}
```

Thrown when user dismisses a question without answering.

### State Management

Questions use `Instance.state` for singleton state per project instance:

```typescript
const state = Instance.state(async () => ({
  pending: {}  // Map of requestID -> { info, resolve, reject }
}))
```

This ensures question state is properly scoped to each OpenCode instance.

---

## Summary

### Integration Points

| Component | Purpose | Key Interface |
|-----------|---------|---------------|
| ACP | Agent-client communication | `AgentSideConnection` |
| LSP | Code intelligence | vscode-jsonrpc |
| IDE | Editor integration | CLI commands |
| Skills | Extensible knowledge | Markdown files |
| Questions | User clarification | Event bus |

### Key Patterns

1. **Event-Driven Architecture**: All components use the internal Bus for event publishing/subscribing
2. **Instance Scoping**: State is scoped to project instances via `Instance.state()`
3. **Automatic Resource Management**: LSP servers auto-spawn and auto-download
4. **Graceful Degradation**: Missing servers are marked as "broken" and skipped
5. **Schema Validation**: All data structures use Zod for runtime validation

### Configuration

| Feature | Config Key | Environment Variable |
|---------|------------|---------------------|
| LSP Servers | `lsp: { [name]: { ... } }` | `OPENCODE_DISABLE_LSP_DOWNLOAD` |
| Skills | `skills.paths` | `OPENCODE_DISABLE_CLAUDE_CODE_SKILLS` |
| Experimental LSP | - | `OPENCODE_EXPERIMENTAL_LSP_TY` |
