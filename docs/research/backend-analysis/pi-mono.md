# Pi Mono -- Backend Architecture Analysis

> TypeScript coding agent monorepo by Mario Zechner (badlogic).
> Repository: `github.com/badlogic/pi-mono`

---

## 1. Project Structure

Pi mono is a classic npm workspaces monorepo with 7 packages in a layered dependency chain.

```
pi-mono/
├── packages/
│   ├── ai/             # Unified multi-provider LLM streaming API
│   ├── agent/          # Generic agent loop + state machine (5 files)
│   ├── coding-agent/   # Full coding agent CLI (the "pi" product)
│   ├── tui/            # Terminal UI library (differential rendering)
│   ├── web-ui/         # Web components for chat interfaces
│   ├── mom/            # Slack bot delegating to coding-agent
│   └── pods/           # CLI for managing vLLM deployments on GPU pods
├── .pi/                # Project-local extensions, prompts, git hooks
├── scripts/            # Release and version management
├── AGENTS.md           # Coding rules for both humans and AI agents
├── biome.json          # Formatter + linter config
└── tsconfig.base.json  # Shared TypeScript config
```

### Package Dependency Chain

```
ai  <--  agent  <--  coding-agent  <--  mom
                          |
                          +--  tui (UI rendering)
                          +--  web-ui (web components)
```

- `@mariozechner/pi-ai` -- Zero-dependency LLM abstraction (usable standalone)
- `@mariozechner/pi-agent-core` -- Generic agent loop on top of pi-ai
- `@mariozechner/pi-coding-agent` -- Full product: tools, sessions, extensions, TUI
- `@mariozechner/pi-mom` -- Slack bot that wraps the coding agent
- `@mariozechner/pi-tui` -- Custom terminal rendering engine
- `@mariozechner/pi-web-ui` -- Web components for AI chat
- `@mariozechner/pi-pods` -- GPU pod management CLI (vLLM)

### Build and Runtime

- **Build**: `npm run build` (sequential: tui -> ai -> agent -> coding-agent -> mom -> web-ui -> pods)
- **Runtime**: Node.js >= 20 or Bun (compiled binary support via `isBunBinary` detection)
- **Tooling**: Biome (format + lint), TypeScript strict, TypeBox for schemas, Vitest for tests
- **Type checking**: Uses `@typescript/native-preview` (tsgo) for fast checking

---

## 2. Tools

Pi has 7 built-in tools. Extensions can register unlimited additional tools.

### Built-in Tools (coding-agent/src/core/tools/)

| Tool | File | Description | Key Details |
|------|------|-------------|-------------|
| **read** | `read.ts` | Read file contents | Supports text + images (jpg, png, gif, webp). Output truncated to 500 lines or 64KB. Has `offset`/`limit` for pagination. Images auto-resized to 2000x2000. |
| **bash** | `bash.ts` | Execute shell commands | Spawns via configured shell. Optional timeout. Output captured to temp file, truncated tail-first. Supports `BashOperations` interface for remote execution (SSH). |
| **edit** | `edit.ts` | Surgical text replacement | Find-and-replace with fuzzy matching. Uses `diff` library for unified diff generation. Normalizes line endings, smart quotes, Unicode. Pluggable via `EditOperations`. |
| **write** | `write.ts` | Create/overwrite files | Auto-creates parent directories. Pluggable via `WriteOperations`. |
| **grep** | `grep.ts` | Search file contents | Uses `ripgrep` (rg) under the hood. Regex or literal, case-insensitive option, glob filtering, context lines. Respects .gitignore. Default limit: 100 matches. |
| **find** | `find.ts` | Find files by glob | Uses `fd` when available, falls back to `glob` library. Respects .gitignore. Default limit: 1000 results. |
| **ls** | `ls.ts` | List directory contents | Alphabetical sort, directories suffixed with `/`. Default limit: 500 entries. |

### Tool Architecture

Every tool follows the same pattern:

```typescript
// Tool schema defined with TypeBox
const schema = Type.Object({
  path: Type.String({ description: "..." }),
  // ...
});

// Factory function for custom working directory
export function createTool(cwd: string, options?: ToolOptions): AgentTool<typeof schema> {
  return {
    name: "tool_name",
    label: "tool_name",
    description: "...",
    parameters: schema,
    execute: async (toolCallId, params, signal, onUpdate) => { ... }
  };
}
```

Key design decisions:
- **Pluggable operations**: Each tool has an `XxxOperations` interface (e.g., `BashOperations`, `ReadOperations`) with default local-filesystem implementations. Override to run tools remotely (SSH, Docker).
- **Truncation system**: Shared `truncate.ts` with `truncateHead` and `truncateTail` functions. Tools report truncation metadata in their `details` field.
- **Two tool sets**: `codingTools` = [read, bash, edit, write] (default), `readOnlyTools` = [read, grep, find, ls] (exploration).
- **Extension tools**: Registered via `ExtensionAPI.registerTool()` with the same `AgentTool` interface.

### Mom (Slack Bot) Tools

The `mom` package has its own tool variants (bash, read, edit, write, attach) adapted for sandboxed Docker execution and Slack file uploads.

---

## 3. Agent Loop

The agent loop is in `packages/agent/` (5 files, ~600 lines). It is a clean, generic turn-based loop separated from all coding-specific concerns.

### Core Files

| File | Lines | Purpose |
|------|-------|---------|
| `agent-loop.ts` | 418 | The actual loop logic |
| `agent.ts` | 559 | `Agent` class wrapping the loop with state management |
| `types.ts` | 195 | Type definitions (AgentState, AgentEvent, etc.) |
| `proxy.ts` | 341 | Proxy stream function for server-mediated LLM calls |
| `index.ts` | 5 | Re-exports |

### Loop Mechanics

```
agentLoop(prompts, context, config, signal, streamFn)
  |
  +-- runLoop()  <-- outer while(true) loop
       |
       +-- Inner loop: while hasMoreToolCalls || pendingMessages
       |    |
       |    +-- Inject pending messages (steering/follow-up)
       |    +-- streamAssistantResponse()
       |    |    +-- config.transformContext(messages)    // AgentMessage[] -> AgentMessage[]
       |    |    +-- config.convertToLlm(messages)       // AgentMessage[] -> Message[]
       |    |    +-- streamFn(model, llmContext, options) // Actual LLM call
       |    |    +-- Emit message_start/update/end events
       |    |
       |    +-- If tool calls: executeToolCalls()
       |    |    +-- For each tool call (sequential):
       |    |    |    +-- Validate args with TypeBox
       |    |    |    +-- Execute tool
       |    |    |    +-- Check for steering messages (skip remaining if interrupted)
       |    |    +-- Emit tool_execution_start/update/end events
       |    |
       |    +-- Get steering messages after turn
       |
       +-- No more tool calls -> check getFollowUpMessages()
       |    +-- If follow-up messages: set as pending, continue outer loop
       |    +-- Otherwise: break
       |
       +-- Emit agent_end, stream.end()
```

### Key Design Patterns

1. **Two-phase message conversion**: The loop works with `AgentMessage[]` (extensible union type) throughout. Only at the LLM call boundary does it convert to `Message[]` via `convertToLlm()`. This allows apps to inject custom message types.

2. **Steering and follow-up queues**: Two separate message queues with configurable delivery modes:
   - **Steering**: Interrupts mid-run. After each tool execution, the loop polls for steering messages. If found, remaining tool calls are skipped.
   - **Follow-up**: Waits until the agent would naturally stop. Checked only when there are no more tool calls.
   - Both support `"all"` (deliver all at once) or `"one-at-a-time"` (deliver one per turn) modes.

3. **Context transform pipeline**: Before each LLM call:
   - `transformContext()` -- operates on AgentMessage[] (pruning, injection)
   - `convertToLlm()` -- converts to LLM-compatible Message[]

4. **Custom stream function**: The `streamFn` parameter allows plugging in custom LLM backends. Default is `streamSimple` from pi-ai. The `proxy.ts` module provides `streamProxy` for server-mediated calls.

5. **EventStream pattern**: The loop returns an `EventStream<AgentEvent, AgentMessage[]>` -- an async iterable that pushes events as they occur. The `Agent` class subscribes and updates its internal state.

6. **Declaration merging for custom messages**: Apps extend `AgentMessage` via TypeScript declaration merging:

```typescript
declare module "@mariozechner/agent" {
  interface CustomAgentMessages {
    artifact: ArtifactMessage;
    notification: NotificationMessage;
  }
}
```

### Agent Class (agent.ts)

The `Agent` class provides a stateful wrapper around the loop:

- **State**: system prompt, model, thinking level, tools, messages, streaming state, pending tool calls, error
- **Event system**: `subscribe(fn)` returns unsubscribe function
- **API**: `prompt(input)`, `continue()`, `steer(msg)`, `followUp(msg)`, `abort()`, `waitForIdle()`
- **Default model**: `gemini-2.5-flash-lite-preview-06-17` (Google)

---

## 4. LLM Providers

### Provider Architecture (packages/ai/)

Pi uses an **API registry pattern** where providers register stream functions keyed by API protocol, not by provider name. This is a key distinction: multiple providers can share the same API protocol.

```typescript
// API protocols (how to talk to the LLM)
type KnownApi =
  | "openai-completions"     // OpenAI Chat Completions API format
  | "openai-responses"       // OpenAI Responses API format
  | "azure-openai-responses" // Azure-hosted Responses API
  | "openai-codex-responses" // Codex-specific Responses API
  | "anthropic-messages"     // Anthropic Messages API
  | "bedrock-converse-stream"// AWS Bedrock Converse Stream
  | "google-generative-ai"   // Google Gemini API
  | "google-gemini-cli"      // Google Gemini CLI API (different auth)
  | "google-vertex"          // Google Vertex AI

// Providers (who serves the model)
type KnownProvider =
  | "anthropic" | "openai" | "google" | "google-vertex"
  | "google-gemini-cli" | "google-antigravity"
  | "azure-openai-responses" | "openai-codex"
  | "amazon-bedrock" | "github-copilot"
  | "xai" | "groq" | "cerebras" | "openrouter"
  | "vercel-ai-gateway" | "zai" | "mistral"
  | "minimax" | "minimax-cn" | "huggingface"
  | "opencode" | "opencode-go" | "kimi-coding"
```

**9 API protocol implementations, 22 known providers.**

### Provider Implementations (ai/src/providers/)

| File | API Protocol | Key Details |
|------|-------------|-------------|
| `anthropic.ts` (27KB) | `anthropic-messages` | Full Anthropic Messages API. Thinking blocks, tool use, cache control (ephemeral breakpoints). |
| `openai-completions.ts` (28KB) | `openai-completions` | OpenAI Chat Completions. Extensive `OpenAICompletionsCompat` for compatibility with 12+ providers (xAI, Groq, Cerebras, OpenRouter, Mistral, etc.). |
| `openai-responses.ts` (8KB) | `openai-responses` | OpenAI Responses API. Shares logic with `openai-responses-shared.ts` (17KB). |
| `azure-openai-responses.ts` (8KB) | `azure-openai-responses` | Azure-hosted OpenAI Responses API. |
| `openai-codex-responses.ts` (27KB) | `openai-codex-responses` | OpenAI Codex Responses API with WebSocket support. |
| `google.ts` (13KB) | `google-generative-ai` | Google Gemini. Shares logic with `google-shared.ts` (12KB). |
| `google-gemini-cli.ts` (29KB) | `google-gemini-cli` | Google Gemini CLI with OAuth and session-based caching. |
| `google-vertex.ts` (14KB) | `google-vertex` | Google Vertex AI (ADC auth). |
| `amazon-bedrock.ts` (23KB) | `bedrock-converse-stream` | AWS Bedrock Converse Stream API. Custom SigV4 signing. |

### Unified Event Stream

All providers emit the same `AssistantMessageEvent` stream:

```typescript
type AssistantMessageEvent =
  | { type: "start"; partial }
  | { type: "text_start" | "text_delta" | "text_end"; ... }
  | { type: "thinking_start" | "thinking_delta" | "thinking_end"; ... }
  | { type: "toolcall_start" | "toolcall_delta" | "toolcall_end"; ... }
  | { type: "done"; reason; message }
  | { type: "error"; reason; error }
```

### Compatibility Layer

The `OpenAICompletionsCompat` interface is particularly notable -- it has 15+ configuration knobs to handle quirks across providers that nominally speak the "OpenAI Completions" protocol:

- `supportsStore`, `supportsDeveloperRole`, `supportsReasoningEffort`
- `thinkingFormat`: "openai" | "zai" | "qwen"
- `requiresToolResultName`, `requiresAssistantAfterToolResult`
- `requiresThinkingAsText`, `requiresMistralToolIds`
- `openRouterRouting`, `vercelGatewayRouting`

### Cross-Provider Message Handling

`transform-messages.ts` handles cross-model message replay:
- Strips redacted thinking blocks when switching models
- Converts thinking blocks to text for cross-model compatibility
- Normalizes tool call IDs (OpenAI Responses generates 450+ char IDs; Anthropic requires <= 64 chars)
- Strips text signatures when switching providers

### Model Registry

Models are auto-generated from provider APIs via `scripts/generate-models.ts` into `models.generated.ts` (329KB). Each model carries:

```typescript
interface Model<TApi> {
  id: string;           // e.g., "claude-opus-4-6"
  name: string;         // human-readable
  api: TApi;            // which API protocol
  provider: Provider;   // who serves it
  baseUrl: string;      // endpoint URL
  reasoning: boolean;   // supports thinking
  input: ("text" | "image")[];
  cost: { input, output, cacheRead, cacheWrite };  // $/million tokens
  contextWindow: number;
  maxTokens: number;
  headers?: Record<string, string>;
  compat?: OpenAICompletionsCompat | OpenAIResponsesCompat;
}
```

### API Key Resolution

`env-api-keys.ts` maps providers to environment variables:

| Provider | Env Var |
|----------|---------|
| anthropic | `ANTHROPIC_OAUTH_TOKEN` or `ANTHROPIC_API_KEY` |
| openai | `OPENAI_API_KEY` |
| google | `GEMINI_API_KEY` |
| google-vertex | ADC credentials (`gcloud auth application-default login`) |
| amazon-bedrock | `AWS_PROFILE` or `AWS_ACCESS_KEY_ID`+`AWS_SECRET_ACCESS_KEY` or `AWS_BEARER_TOKEN_BEDROCK` |
| github-copilot | `COPILOT_GITHUB_TOKEN` or `GH_TOKEN` |
| xai | `XAI_API_KEY` |
| groq | `GROQ_API_KEY` |
| openrouter | `OPENROUTER_API_KEY` |
| And 10+ more... | |

The coding-agent's `ModelRegistry` (`model-registry.ts`, 24KB) adds user-configurable models on top via `settings.json`:
- Custom providers with custom models and base URLs
- Per-model overrides (cost, context window, compat flags)
- API key resolution via `AuthStorage` (auth.json) + env vars
- Custom stream functions via extensions

---

## 5. Context/Token Management

### Compaction System (coding-agent/src/core/compaction/)

Pi uses a **summarization-based compaction** system, not sliding window or truncation.

| File | Lines | Purpose |
|------|-------|---------|
| `compaction.ts` | ~600 | Core compaction logic |
| `branch-summarization.ts` | ~300 | Branch summary generation |
| `utils.ts` | ~150 | File operation tracking, serialization |

**How it works:**

1. **Token estimation**: Uses `chars/4` heuristic, anchored by the last assistant message's actual `usage.totalTokens`.

2. **Trigger conditions**:
   - Auto-compact when `contextTokens > contextWindow - reserveTokens` (default reserve: 16,384 tokens)
   - Context overflow recovery: when LLM returns overflow error, compaction runs immediately
   - Manual: user types `/compact`

3. **Compaction process**:
   - Find a "cut point" -- messages to summarize vs. messages to keep
   - Keep at least `keepRecentTokens` (default: 20,000) of recent messages
   - Call `completeSimple()` with `SUMMARIZATION_SYSTEM_PROMPT` to generate a summary
   - Track file operations (reads, edits, writes) across compaction boundaries
   - Store as a `CompactionEntry` in the session with `summary`, `firstKeptEntryId`, `tokensBefore`

4. **File operation tracking**: Compaction preserves knowledge of which files were read/modified by extracting file paths from tool call arguments across the compacted messages.

5. **Extension hooks**: Extensions can override compaction via the `session_before_compact` event, returning a custom `CompactionResult` instead of using the built-in summarizer.

### Context Overflow Detection

`packages/ai/src/utils/overflow.ts` detects overflow across all providers:
- 14 regex patterns for provider-specific error messages (Anthropic, OpenAI, Google, xAI, Groq, etc.)
- Silent overflow detection for providers that accept oversized input (z.ai)
- Status code detection for providers that return empty bodies (Cerebras, Mistral)

### Auto-Retry

The `AgentSession` has built-in retry logic:
- Configurable: `enabled`, `maxRetries` (default: 3), `baseDelayMs` (default: 2000, exponential backoff)
- On retryable errors (overflow triggers compaction first, then retry)
- Events: `auto_retry_start`, `auto_retry_end`

---

## 6. Session Management

### Session Storage Format

Sessions are stored as **JSONL files** (one JSON object per line) in `~/.pi/agent/sessions/<encoded-cwd>/`.

```
~/.pi/agent/sessions/--home-user-project--/
  ├── a1b2c3d4.jsonl
  ├── e5f6g7h8.jsonl
  └── ...
```

Each JSONL file contains a header followed by entries:

```jsonl
{"type":"session","version":3,"id":"a1b2c3d4","timestamp":"2026-01-15T...","cwd":"/home/user/project"}
{"type":"message","id":"00a1","parentId":null,"timestamp":"...","message":{"role":"user","content":"hello","timestamp":1234}}
{"type":"message","id":"00b2","parentId":"00a1","timestamp":"...","message":{"role":"assistant","content":[...],...}}
{"type":"thinking_level_change","id":"00c3","parentId":"00b2","thinkingLevel":"high"}
{"type":"model_change","id":"00d4","parentId":"00c3","provider":"anthropic","modelId":"claude-opus-4-6"}
{"type":"compaction","id":"00e5","parentId":"00d4","summary":"...","firstKeptEntryId":"00c3","tokensBefore":150000}
```

### Entry Types

| Entry Type | Purpose |
|------------|---------|
| `session` | Header with version, ID, cwd, parent session |
| `message` | User, assistant, or tool result message |
| `thinking_level_change` | Records thinking level changes |
| `model_change` | Records model switches |
| `compaction` | Summarization checkpoint |
| `branch_summary` | Summary when navigating branches |
| `custom` | Extension-specific data (not sent to LLM) |
| `custom_message` | Extension-injected messages (sent to LLM) |
| `label` | User-defined bookmarks on entries |
| `session_info` | Session metadata (display name) |

### Tree Structure

Sessions use a **DAG/tree structure** via `id`/`parentId` fields on every entry. This enables:

- **Branching**: Fork from any point in history
- **Branch navigation**: Walk the tree, generate branch summaries
- **Context building**: `buildSessionContext()` walks from leaf to root, collecting messages along the path
- **Compaction-aware replay**: When a compaction entry is on the path, emit summary first, then kept messages, then post-compaction messages

### Session Manager (42KB)

The `SessionManager` class provides:

- `create(cwd)` -- create or resume most recent session
- `newSession()` -- start fresh
- `fork(entryId)` -- fork from a point
- `switchSession(path)` -- switch to different session file
- `getTree()` -- get full tree structure
- `getBranch(leafId)` -- get entries from root to leaf
- `appendEntry()` -- append-only writes (JSONL)
- Session migrations (v1 -> v2 -> v3)
- Session info discovery (list all sessions, search text, first message preview)

### Migrations

```
v1 -> v2: Add id/parentId tree structure (linear -> tree)
v2 -> v3: Rename "hookMessage" role to "custom"
```

---

## 7. Extension/Plugin System

### Overview

Pi has a rich extension system in `coding-agent/src/core/extensions/` (4 files, ~90KB total):

| File | Lines | Purpose |
|------|-------|---------|
| `types.ts` | ~1100 | All type definitions |
| `loader.ts` | ~400 | TypeScript module loading via jiti |
| `runner.ts` | ~700 | Extension lifecycle, event dispatch |
| `wrapper.ts` | ~120 | Tool wrapping for extension interception |

### Extension Loading

Extensions are TypeScript files loaded via `@mariozechner/jiti` (a fork with virtualModules support for compiled Bun binaries):

- **Sources**: project-local (`.pi/extensions/`), user-global (`~/.pi/agent/extensions/`), npm packages, settings paths
- **Virtual modules**: When running as a compiled Bun binary, pi-ai, pi-agent-core, pi-tui, pi-coding-agent, and @sinclair/typebox are made available via virtualModules
- **Aliases**: In Node.js mode, jiti aliases resolve to the correct package paths

### Extension API

Extensions are factory functions that receive an `ExtensionAPI`:

```typescript
// Example extension
import { Type } from "@sinclair/typebox";

export default function(pi: ExtensionAPI) {
  // Register event handlers
  pi.on("session_start", async (event, ctx) => { ... });
  pi.on("tool_call", async (event, ctx) => { ... });
  pi.on("context", async (event, ctx) => { ... });

  // Register tools
  pi.registerTool({
    name: "my_tool",
    label: "My Tool",
    description: "Does something",
    parameters: Type.Object({ input: Type.String() }),
    execute: async (toolCallId, params, signal, onUpdate, ctx) => {
      return { content: [{ type: "text", text: "result" }], details: {} };
    },
  });

  // Register commands
  pi.registerCommand("mycommand", {
    description: "My command",
    handler: async (args, ctx) => { ... },
  });

  // Register keyboard shortcuts
  pi.registerShortcut("ctrl+shift+m", {
    description: "My shortcut",
    handler: async (ctx) => { ... },
  });

  // Register CLI flags
  pi.registerFlag("my-flag", {
    type: "boolean",
    default: false,
    description: "Enable my feature",
  });

  // Register custom message renderer
  pi.registerMessageRenderer("my_type", (message, options, theme) => { ... });

  // Register custom LLM provider
  pi.registerProvider("my-provider", {
    models: [{ id: "my-model", ... }],
    baseUrl: "https://api.example.com",
  });

  // Actions
  pi.sendMessage({ customType: "note", content: "...", display: true });
  pi.sendUserMessage("Do something");
  pi.appendEntry("my_state", { key: "value" });
  pi.setActiveTools(["read", "bash", "my_tool"]);
  pi.setModel(model);
  pi.setThinkingLevel("high");
}
```

### Event System

Extensions can hook into 25+ event types across 5 categories:

**Session lifecycle:**
- `session_start`, `session_shutdown`
- `session_before_switch` / `session_switch`
- `session_before_fork` / `session_fork`
- `session_before_compact` / `session_compact`
- `session_before_tree` / `session_tree`

**Agent lifecycle:**
- `before_agent_start` -- can inject messages and modify system prompt
- `agent_start` / `agent_end`
- `turn_start` / `turn_end`
- `message_start` / `message_update` / `message_end`

**Tool interception:**
- `tool_call` -- fires before tool execution, can **block** execution
- `tool_result` -- fires after tool execution, can **modify** result
- `tool_execution_start` / `tool_execution_update` / `tool_execution_end`

**Context manipulation:**
- `context` -- fires before each LLM call, can modify the message array

**Input/Model:**
- `input` -- fires on user input, can transform or handle entirely
- `model_select` -- fires on model change
- `user_bash` -- fires when user runs `!command`
- `resources_discover` -- fires to discover additional skill/prompt/theme paths

### Tool Wrapping

Extensions intercept all tool executions via `wrapToolWithExtensions()`:

```
Tool call from LLM
  -> emit tool_call event (extensions can block)
  -> execute actual tool
  -> emit tool_result event (extensions can modify result)
  -> return final result
```

### UI Context

Extensions have access to rich TUI primitives via `ExtensionUIContext`:

- `select()`, `confirm()`, `input()` -- dialogs
- `notify()` -- notifications
- `setStatus()` -- footer status text
- `setWidget()` -- custom widgets above/below editor
- `setFooter()`, `setHeader()` -- replace entire footer/header
- `setEditorComponent()` -- replace the input editor (e.g., VimEditor)
- `custom()` -- show arbitrary TUI components with keyboard focus
- `editor()` -- multi-line text editor dialog
- Theme access and switching

### Example Extensions

The repository includes 70+ example extensions demonstrating:
- Permission gates, destructive command confirmation, protected paths
- Git checkpoints (auto-commit before edits)
- File watchers and triggers
- Custom compaction strategies
- Subagent delegation (handoff)
- Interactive shell
- SSH remote execution
- Snake and Space Invaders games (TUI demos)
- Custom editors (rainbow-editor, modal-editor)
- Custom providers (Anthropic, GitLab Duo, Qwen CLI)

### Package Management

Extensions can be distributed as npm packages:

```bash
pi install <npm-package>     # Install extension package
pi install <git-url>         # Install from git
pi remove <source>           # Remove extension package
pi list                      # List installed packages
```

Packages are configured in `settings.json`:

```json
{
  "packages": [
    "my-extension-package",
    { "source": "git+https://...", "extensions": ["ext1.ts"], "skills": ["skill.md"] }
  ]
}
```

---

## 8. Unique Features

### 8.1 Skills System

Skills are Markdown files with frontmatter that inject context into the system prompt:

```markdown
---
name: react-patterns
description: React component patterns and best practices
---

When editing React components:
- Use functional components with hooks
- Prefer composition over inheritance
...
```

Skills are loaded from:
- Project-local: `.pi/skills/`
- User-global: `~/.pi/agent/skills/`
- Extension-provided paths (via `resources_discover` event)

Skills with `disable-model-invocation: true` are always included; others can be invoked by the model.

### 8.2 Prompt Templates

Prompt templates are Markdown files that expand `{{file:path}}` markers:

```markdown
Review the following code:

{{file:src/main.ts}}

Focus on error handling and edge cases.
```

### 8.3 Proxy Stream Architecture

`packages/agent/src/proxy.ts` provides `streamProxy()` -- a drop-in replacement for `streamSimple` that routes LLM calls through a server. The proxy protocol strips the `partial` field from delta events to reduce bandwidth, reconstructing the partial message client-side.

### 8.4 RPC Mode

Pi supports headless operation via JSON-over-stdin/stdout RPC (`modes/rpc/`):

```jsonl
{"type":"prompt","message":"Fix the bug in main.ts"}
{"type":"set_model","provider":"anthropic","modelId":"claude-opus-4-6"}
{"type":"compact","customInstructions":"Focus on code changes"}
{"type":"abort"}
```

This enables embedding Pi in other tools, IDEs, or web UIs.

### 8.5 Branch-Based Session Navigation

The tree-structured session model allows:
- Forking from any message to explore alternatives
- Navigating between branches with automatic summarization
- Labels (bookmarks) on any entry
- Branch summaries that compress abandoned branches

### 8.6 Cross-Provider Message Transformation

`transform-messages.ts` handles seamless model switching mid-conversation:
- Thinking blocks from model A become text blocks for model B
- Redacted thinking (encrypted by provider) is dropped for cross-model replay
- Tool call ID normalization across providers
- Text signatures stripped when switching providers

### 8.7 Context Overflow Recovery

When any provider returns a context overflow error, Pi automatically:
1. Detects it via 14 provider-specific regex patterns
2. Triggers compaction
3. Retries the failed request with the compacted context

### 8.8 Thinking Budget Management

Fine-grained thinking/reasoning control:
- 6 levels: off, minimal, low, medium, high, xhigh
- Per-level token budgets configurable via settings
- `xhigh` only for specific models (GPT-5.x, Claude Opus 4.6)
- Auto-adjustment of max tokens to accommodate thinking budget

### 8.9 Session-Based Prompt Caching

The `sessionId` field flows through the entire stack to LLM providers that support session-based caching (e.g., OpenAI Codex). When sessions are forked or resumed, the session ID is updated.

### 8.10 Dynamic API Key Resolution

The `getApiKey` callback in `AgentLoopConfig` resolves API keys on every LLM call, not just at startup. This handles:
- OAuth tokens that expire during long tool execution phases (GitHub Copilot)
- Key rotation
- Per-request authentication

### 8.11 Fuzzy Edit Matching

The edit tool (`edit-diff.ts`) performs fuzzy text matching:
- Strips trailing whitespace per line
- Normalizes smart quotes to ASCII
- Normalizes Unicode dashes/hyphens
- Normalizes special Unicode spaces

This makes edits resilient to LLM-introduced Unicode artifacts.

### 8.12 Mom (Slack Bot)

The `mom` package is a Slack bot that:
- Receives messages from Slack channels
- Delegates to the coding agent with sandbox support (Docker)
- Maintains per-channel and workspace-level MEMORY.md files
- Supports file attachments and image uploads
- Has its own tool variants adapted for sandboxed execution

---

## 9. Architecture Comparison Notes

### Strengths

1. **Clean layering**: ai -> agent -> coding-agent is a well-separated dependency chain. The agent loop is genuinely generic.
2. **Provider compatibility**: The `OpenAICompletionsCompat` system is the most thorough provider compatibility layer observed -- 15+ config knobs per provider.
3. **Extension system**: The event-based extension API with tool interception (block, modify) is powerful and well-typed.
4. **Session tree**: DAG-based sessions with branch summaries is more sophisticated than linear history.
5. **Pluggable operations**: Every tool has an operations interface for remote execution, not just the bash tool.
6. **RPC mode**: First-class headless operation for embedding.

### Design Choices

1. **No platform abstraction**: Unlike AVA's `getPlatform()` pattern, Pi uses Node.js APIs directly. The `Operations` interfaces on tools serve a similar purpose for remote execution.
2. **TypeBox over Zod**: Uses `@sinclair/typebox` for schemas instead of Zod. This generates JSON Schema directly, which some LLM APIs prefer.
3. **JSONL sessions**: Append-only JSONL is simpler than SQLite but requires file-level operations for tree navigation.
4. **Monolithic coding-agent**: At ~100KB for `agent-session.ts` alone, the coding-agent package is not as decomposed as an extension-first architecture. Tools, sessions, and extensions are tightly coupled.
5. **Sequential tool execution**: Tool calls are executed one at a time (not parallel), with steering checks between each.
6. **No built-in permission system**: Safety is handled via extensions (e.g., `confirm-destructive.ts`, `protected-paths.ts`, `permission-gate.ts`).

---

## 10. File Size Reference

Key files by size (for understanding where complexity lives):

| File | Size | Purpose |
|------|------|---------|
| `coding-agent/src/modes/interactive/interactive-mode.ts` | 146KB (4,401 lines) | Full interactive TUI mode |
| `coding-agent/src/core/agent-session.ts` | 100KB (3,003 lines) | Session lifecycle, compaction, retry |
| `coding-agent/src/core/package-manager.ts` | 54KB | npm/git package management |
| `coding-agent/src/core/extensions/types.ts` | 46KB (~1,100 lines) | Extension type definitions |
| `coding-agent/src/core/session-manager.ts` | 42KB | Session CRUD, tree, migrations |
| `tui/src/tui.ts` | 40KB | Terminal rendering engine |
| `coding-agent/src/core/settings-manager.ts` | 28KB | Settings with file watching |
| `ai/src/providers/openai-completions.ts` | 28KB | OpenAI Completions provider |
| `ai/src/providers/anthropic.ts` | 27KB | Anthropic provider |
| `ai/src/providers/google-gemini-cli.ts` | 29KB | Google Gemini CLI provider |
| `ai/src/models.generated.ts` | 329KB | Auto-generated model definitions |
