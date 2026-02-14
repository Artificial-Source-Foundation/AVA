# Cline Core API & Controller Architecture

> Comprehensive analysis of Cline's API abstraction layer and controller orchestration

---

## Overview

The Cline codebase demonstrates a sophisticated multi-provider LLM architecture with a gRPC-based communication pattern between VS Code extension and webview. The **API** layer abstracts 40+ language model providers through a unified `ApiHandler` interface, while the **Controller** layer provides a request-response and streaming architecture for VS Code extension features.

**Key Architectural Principles:**
- Provider-agnostic abstraction with adapter pattern
- Stream-based responses for real-time model output
- gRPC/protobuf communication for webview-extension messaging
- Decorator-based retry middleware for rate limiting
- Tool call normalization across provider formats
- Proxy-aware network requests for enterprise compatibility

---

## Directory Structure

```
src/core/api/
├── index.ts                      # Provider factory & unified ApiHandler interface
├── retry.ts                      # @withRetry decorator for rate limit handling
├── adapters/
│   ├── index.ts                  # Tool call message transformation
│   ├── diff-editors.ts           # Apply-patch ↔ write-to-file conversion
│   └── __tests__/                # Tests for adapter logic
├── providers/                    # 40+ provider implementations
│   ├── types.ts                  # Shared types (error handling, selectors)
│   ├── anthropic.ts              # Claude/Anthropic handler
│   ├── openai.ts                 # OpenAI & Azure OpenAI handler
│   ├── bedrock.ts                # AWS Bedrock handler
│   ├── gemini.ts                 # Google Gemini handler
│   ├── [30+ others]              # LiteLLM, Ollama, Mistral, etc.
│   └── __tests__/                # Provider-specific tests
├── transform/                    # Response format adapters
│   ├── stream.ts                 # Unified ApiStream type definitions
│   ├── anthropic-format.ts       # Claude message sanitization
│   ├── openai-format.ts          # OpenAI message conversion
│   ├── tool-call-processor.ts    # Native tool call accumulation
│   ├── openai-response-format.ts # Responses API handling
│   └── [6+ others]               # Provider-specific stream handlers
└── utils/
    └── responses_api_support.ts  # Responses API stream processing

src/core/controller/
├── index.ts                      # Controller class (orchestrator)
├── grpc-handler.ts               # gRPC request/response routing
├── grpc-service.ts               # Service registry for method handlers
├── grpc-request-registry.ts      # Request lifecycle tracking
├── account/                      # Auth & account management
├── mcp/                          # MCP server management
├── models/                       # Model configuration & discovery
├── file/                         # File operations from webview
├── checkpoints/                  # Conversation snapshots
├── browser/                      # Browser automation features
├── dictation/                    # Voice input handling
├── commands/                     # Slash command execution
├── grpc-recorder/                # gRPC request logging
├── task/                         # Task execution
├── state/                        # State management subscriptions
├── ui/                           # UI event handlers
└── web/                          # Web-related operations
```

---

## Provider Factory (`api/index.ts`)

**Purpose:** Factory for creating provider-specific API handlers based on configuration

**Key Functions:**
```typescript
buildApiHandler(configuration: ApiConfiguration, mode: Mode): ApiHandler
  // Creates handler with validation of thinking budget against model maxTokens

createHandlerForProvider(apiProvider: string, options, mode): ApiHandler
  // Factory function returning provider-specific implementation
```

**Supported Providers (40+):**
- Anthropic (Claude)
- OpenAI & Azure OpenAI
- AWS Bedrock
- Google Gemini & Vertex AI
- LiteLLM, Ollama, LMStudio
- Mistral, Together, DeepSeek
- OpenRouter, Groq, Cerebras
- HuggingFace, Fireworks
- Cline Cloud, Requesty, OCA
- And 25+ more...

---

## Retry Decorator (`api/retry.ts`)

**Purpose:** Decorator-based retry middleware for handling rate limits and transient errors

```typescript
@withRetry({ maxRetries: 3, baseDelay: 1000, maxDelay: 10000 })
async *createMessage(...): ApiStream
```

**Features:**
- Detects rate limits via HTTP 429 status or RetriableError class
- Respects `Retry-After`, `X-RateLimit-Reset`, `Ratelimit-Reset` headers
- Exponential backoff: `min(maxDelay, baseDelay * 2^attempt)`
- Handles both Unix timestamp and delta-seconds formats

---

## Stream Normalization (`transform/stream.ts`)

**Purpose:** Unified stream chunk definitions for all providers

```typescript
type ApiStream = AsyncGenerator<ApiStreamChunk> & { id?: string }

type ApiStreamChunk =
  | ApiStreamTextChunk        // model output text
  | ApiStreamThinkingChunk    // reasoning/thinking content
  | ApiStreamUsageChunk       // token counts & costs
  | ApiStreamToolCallsChunk   // native tool function calls
```

All provider implementations normalize to these chunk types, enabling:
- Switching providers mid-task without breaking UI
- Unified token counting and cost calculation
- Reasoning/thinking content handling (Claude, OpenAI o1/o3)
- Native tool call handling across formats

---

## Tool Call Format Translation (`adapters/index.ts`)

**Purpose:** Convert between `apply_patch` (git diff) and `write_to_file` formats

```typescript
transformToolCallMessages(messages, nativeTools) {
  1. Collect tools used in assistant messages (single pass)
  2. Determine which conversions to apply:
     - If provider has apply_patch but history uses write_to_file → convert
     - If provider has write_to_file but history uses apply_patch → convert
     - Otherwise, return unchanged
}
```

---

## Controller Architecture (`controller/index.ts`)

**Purpose:** Main extension controller orchestrating tasks, authentication, and state

**Key Properties:**
```typescript
task?: Task                                    // Current active task
mcpHub: McpHub                                // MCP server manager
accountService: ClineAccountService          // Cline account operations
authService: AuthService                     // Auth flow management
stateManager: StateManager                   // Global & workspace state
workspaceManager?: WorkspaceRootManager      // Multi-root workspace support
```

**Key Methods:**
```typescript
initTask(task?, images?, files?, historyItem?, taskSettings?)
  // Initialize new task with optional history resume

togglePlanActMode(modeToSwitchTo, chatContent?): Promise<boolean>
  // Switch between plan/act modes, rebuild API handler

cancelTask()
  // Gracefully abort task with state cleanup

getStateToPostToWebview(): Promise<ExtensionState>
  // Gather all state for UI rendering
```

---

## gRPC Communication Pattern

### Request Flow
```typescript
// Webview sends:
{
  type: "grpc_request",
  service: "TaskService",
  method: "createTask",
  message: { ... },
  is_streaming: boolean,
  request_id: string
}

// Extension responds:
{
  type: "grpc_response",
  grpc_response: {
    message: { ... },
    request_id: string,
    is_streaming: boolean,  // false for final chunk
    error?: string
  }
}
```

### Streaming Subscriptions
```typescript
activeMcpServersSubscriptions = new Set<StreamingResponseHandler>()

// Each handler can be called multiple times:
await responseStream(data, isLast=false)
await responseStream(finalData, isLast=true)
```

---

## Field Mask-Based Config Updates

```typescript
updateMask: ["options.ulid", "secrets.apiKey", "options.planModeOpenAiModelId"]

parseFieldMask(updateMask) → { options: Set<string>, secrets: Set<string> }
```

Supports:
- Partial updates without replacing entire config
- Validation that masked fields exist
- Mode-specific field alternates (planModeX ↔ actModeX)

---

## Notable Features for AVA

### 1. Multi-Provider Architecture at Scale
Factory pattern for 40+ providers with unified interface.

### 2. Stream Normalization Layer
All provider streams normalize to `ApiStreamChunk` types.

### 3. Rate Limit Handling with @withRetry Decorator
Respects HTTP 429 and multiple header formats.

### 4. gRPC-Based Extension-Webview Communication
Strict typing, streaming support, recording/debugging.

### 5. Thinking/Reasoning Content Support
Claude's extended thinking, OpenAI o1/o3 reasoning, Gemini thinking levels.

### 6. Tool Call Format Translation
Automatic conversion between apply_patch and write_to_file.

### 7. Multi-Root Workspace Management
Detection via pnpm-workspace.yaml, lerna.json, etc.

### 8. Request Lifecycle Tracking
`GrpcRequestRegistry` with cleanup callbacks.

---

## Integration Points

### API → Controller Flow
```
User action in webview
  → gRPC request to controller.method
    → Handler fetches current ApiConfiguration
    → Calls buildApiHandler() to get provider handler
    → Calls handler.createMessage()
    → Streams responses back via ApiStream generator
    → Controller posts messages to webview
```

### Provider Selection
```
UpdateApiConfigurationRequestNew { updates, updateMask }
  → updateApiConfiguration() validates mask
  → StateManager persists config
  → Next task uses new provider via buildApiHandler()
```
