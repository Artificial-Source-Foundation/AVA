# Cline Types & Utilities

> Analysis of Cline's type definitions and utility functions

---

## Overview

Cline's types and utilities system is organized into three key layers:

1. **Types** (`/types/`) - Type definitions and interfaces
2. **Shared** (`/shared/`) - Core abstractions and models
3. **Utils** (`/utils/`) - Platform-specific and operational utilities

---

## Key Type Definitions

### Message Types

**ClineMessage** (chat history item):
```typescript
type ClineMessage = {
  ts: number                                    // timestamp
  type: "ask" | "say"                          // message direction
  ask?: ClineAsk | ClineAskQuestion | ...      // user/system request
  say?: ClineSay | ClineSayTool | ...          // system responses
  text?: string                                 // message content
  reasoning?: string                            // LLM reasoning
  images?: string[]                             // image URIs
  files?: string[]                              // file paths
  partial?: boolean                             // streaming state
  modelInfo?: ClineMessageModelInfo            // model used
}
```

**ClineAsk Union** (25+ types):
- `followup`, `plan_mode_respond`, `act_mode_respond`
- `command`, `command_output`, `tool`
- `browser_action_launch`, `use_mcp_server`
- `api_req_failed`, `resume_task`
- `new_task`, `condense`, `summarize_task`

**ClineSay Union** (25+ types):
- `task`, `error`, `text`, `reasoning`
- `api_req_started`, `api_req_finished`
- `command`, `command_output`, `tool`
- `mcp_server_request_started`, `mcp_server_response`
- `browser_action`, `browser_action_result`
- `hook_status`, `hook_output_stream`

### API Provider Types

**ApiProvider** (50+ providers):
```typescript
type ApiProvider =
  | "anthropic" | "claude-code" | "openrouter" | "bedrock" | "vertex"
  | "openai" | "ollama" | "lmstudio" | "gemini" | "openai-native"
  | "openai-codex" | "requesty" | "together" | "deepseek" | "qwen"
  | "mistral" | "vscode-lm" | "cline" | "litellm" | "groq"
  // ... and 30+ more
```

**ModelInfo** (pricing, capabilities):
```typescript
interface ModelInfo {
  maxTokens?: number
  contextWindow?: number
  supportsImages?: boolean
  supportsPromptCache: boolean
  supportsReasoning?: boolean
  inputPrice?: number                        // per million tokens
  outputPrice?: number
  thinkingConfig?: { maxBudget?, outputPrice?, ... }
  tiers?: { contextWindow, inputPrice?, outputPrice? }[]
  apiFormat?: ApiFormat
}
```

### State Types

**ExtensionState** (118 fields):
- API configuration
- UI state (mode, clineMessages, taskHistory)
- User preferences
- Browser/MCP settings
- Workspace info
- Feature flags
- Rules toggles

**AutoApprovalSettings**:
```typescript
interface AutoApprovalSettings {
  version: number
  enabled: boolean
  actions: {
    readFiles: boolean
    readFilesExternally?: boolean
    editFiles: boolean
    editFilesExternally?: boolean
    executeSafeCommands?: boolean
    executeAllCommands?: boolean
    useBrowser: boolean
    useMcp: boolean
  }
  enableNotifications: boolean
}
```

### Tool Types

**ClineDefaultTool** (25+ built-in tools):
```typescript
enum ClineDefaultTool {
  ASK = "ask_followup_question"
  ATTEMPT = "attempt_completion"
  BASH = "execute_command"
  FILE_EDIT = "replace_in_file"
  FILE_READ = "read_file"
  FILE_NEW = "write_to_file"
  SEARCH = "search_files"
  BROWSER = "browser_action"
  MCP_USE = "use_mcp_tool"
  WEB_FETCH = "web_fetch"
  WEB_SEARCH = "web_search"
  // ...
}

// Read-only tools (safe for parallel execution)
const READ_ONLY_TOOLS = [
  LIST_FILES, FILE_READ, SEARCH, LIST_CODE_DEF,
  BROWSER, ASK, WEB_SEARCH, WEB_FETCH, USE_SKILL
]
```

---

## Utility Functions

### String Utilities (`string.ts`)

**Unicode Normalization:**
```typescript
canonicalize(s: string): string
  // Normalize Unicode to NFC form
  // Map visual punctuation variants:
  //   - Hyphens: various dashes → "-"
  //   - Quotes: all variants → "'" or '"'
  //   - Spaces: no-break space → " "
  // Unescape quotes: \`, \', \" → `, ', "
```

### Path Utilities (`path.ts`)

```typescript
toPosixPath(p: string): string
  // Convert backslashes to forward slashes

arePathsEqual(path1?, path2?): boolean
  // Case-insensitive on Windows, case-sensitive on Unix

getReadablePath(cwd, relPath?): string
  // Shows relative path if in workspace, absolute if outside

isLocatedInPath(dirPath, pathToCheck): boolean
  // Check if pathToCheck is inside dirPath

isLocatedInWorkspace(pathToCheck?): Promise<boolean>
  // Check if path is in any workspace root
```

### Filesystem Utilities (`fs.ts`)

```typescript
createDirectoriesForFile(filePath): Promise<string[]>
  // Recursively create directories, return newly created

fileExistsAtPath(filePath): Promise<boolean>

isDirectory(filePath): Promise<boolean>

getFileSizeInKB(filePath): Promise<number>
```

### Git Utilities (`git.ts`)

```typescript
searchCommits(query, cwd): Promise<GitCommit[]>
  // Search commits by message (grep) or hash
  // Returns up to 10 matches
```

### Shell Utilities (`shell.ts`)

```typescript
getShell(): string
  // Priority: VSCode config → userInfo().shell → env var → default

getAvailableTerminalProfiles(): TerminalProfile[]
  // Platform-specific shell profiles
```

### Cost Calculation (`cost.ts`)

```typescript
calculateApiCostAnthropic(modelInfo, inputTokens, outputTokens, ...)
  // Anthropic-style: inputTokens does NOT include cached tokens

calculateApiCostOpenAI(modelInfo, inputTokens, outputTokens, ...)
  // OpenAI-style: inputTokens INCLUDES cached tokens
```

**Features:**
- Tiered pricing support
- Prompt cache pricing
- Extended thinking output pricing override

### Networking Utilities (`net.ts`)

```typescript
fetch(url, options?): Promise<Response>
  // Proxy-aware fetch wrapper
  // VSCode: uses global fetch
  // JetBrains/CLI: undici fetch with ProxyAgent

getAxiosSettings(): object
  // Returns axios config with proxy agent if needed
```

---

## Shared Patterns & Abstractions

### 1. Storage Abstraction Layer

```typescript
abstract class ClineStorage {
  protected abstract _get(key): Promise<string | undefined>
  protected abstract _store(key, value): Promise<void>
  protected abstract _delete(key): Promise<void>

  public onDidChange(callback): () => void
    // Observer pattern for storage changes
}
```

**Implementations:**
- `ClineFileStorage` - Filesystem (CLI/JetBrains)
- VSCode Memento adapter - VSCode native storage
- `ClineSecretStorage` - Secret management

### 2. State Keys Central Registry

**118 fields** organized by category:
- Global state (announcements, task history)
- Remote config (providers, MCP servers, rules)
- API handler settings (per-provider configs)
- Settings per mode (plan mode, act mode)
- Per-tool overrides

### 3. Message Conversion & Compatibility

```typescript
convertClineStorageToAnthropicMessage(clineMessage, provider?)
  // Remove Cline-specific fields
  // Filter out invalid thinking blocks
  // Preserve reasoning_details for supported providers
```

### 4. Cross-Platform Path Handling

Strategy:
1. Present paths with forward slashes to AI/user
2. Use native path module for FS operations
3. Safe comparison with `arePathsEqual()`

### 5. Proxy-Aware Networking

- **VSCode**: Trust global fetch
- **JetBrains/CLI**: Undici + ProxyAgent
- **Testing**: Mock fetch replacement pattern

---

## Notable Features for AVA

### 1. Extended Thinking & Reasoning Support
`reasoning_details` field for extended thinking content.

### 2. Prompt Caching Integration
`cacheWrites` and `cacheReads` token tracking.

### 3. Multi-API-Style Cost Calculation
Anthropic vs OpenAI token counting conventions.

### 4. Comprehensive Auto-Approval System
8 action categories with fine-grained permissions.

### 5. Advanced Networking & Proxy Support
Platform-specific fetch implementations.

### 6. MCP OAuth & Server Management
OAuth authentication flow for MCP servers.

### 7. Hook Lifecycle System
Pre/Post tool execution hooks.

### 8. Workspace Root & Multi-Root Support
`workspaceRoots` array with root metadata.

### 9. Advanced Browser Automation
Viewport presets, remote browser support.

### 10. Shell Profile System
Available terminal profiles per platform.

### 11. Focus Chain (Todo List) Pattern
Flexible regex for checklist parsing.

### 12. Unicode Normalization for Patching
Comprehensive punctuation mapping.

### 13. Remote Configuration System
Remote rules, workflows, MCP servers.
