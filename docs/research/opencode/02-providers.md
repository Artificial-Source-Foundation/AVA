# OpenCode Providers Analysis

This document analyzes OpenCode's provider/LLM handling system, which provides a sophisticated abstraction layer over multiple AI provider SDKs.

## Supported Providers

OpenCode supports **21 bundled providers** out of the box, with the ability to add custom providers:

| Provider | SDK Package | Auth Method |
|----------|-------------|-------------|
| **OpenAI** | `@ai-sdk/openai` | API Key |
| **Anthropic** | `@ai-sdk/anthropic` | API Key |
| **Google AI** | `@ai-sdk/google` | API Key |
| **Google Vertex AI** | `@ai-sdk/google-vertex` | GCP Credentials |
| **Google Vertex Anthropic** | `@ai-sdk/google-vertex/anthropic` | GCP Credentials |
| **Amazon Bedrock** | `@ai-sdk/amazon-bedrock` | AWS Credentials/Profile/Bearer Token |
| **Azure OpenAI** | `@ai-sdk/azure` | API Key |
| **Azure Cognitive Services** | `@ai-sdk/azure` | API Key + Resource Name |
| **OpenRouter** | `@openrouter/ai-sdk-provider` | API Key |
| **xAI (Grok)** | `@ai-sdk/xai` | API Key |
| **Mistral** | `@ai-sdk/mistral` | API Key |
| **Groq** | `@ai-sdk/groq` | API Key |
| **DeepInfra** | `@ai-sdk/deepinfra` | API Key |
| **Cerebras** | `@ai-sdk/cerebras` | API Key |
| **Cohere** | `@ai-sdk/cohere` | API Key |
| **TogetherAI** | `@ai-sdk/togetherai` | API Key |
| **Perplexity** | `@ai-sdk/perplexity` | API Key |
| **Vercel AI Gateway** | `@ai-sdk/gateway` | API Key |
| **Vercel** | `@ai-sdk/vercel` | API Key |
| **GitLab** | `@gitlab/gitlab-ai-provider` | OAuth/API Token |
| **GitHub Copilot** | Custom OpenAI-compatible | OAuth |
| **GitHub Copilot Enterprise** | Custom OpenAI-compatible | OAuth |
| **Cloudflare AI Gateway** | `@ai-sdk/openai-compatible` | API Token |
| **SAP AI Core** | Custom | Service Key |
| **OpenCode** (native) | `@ai-sdk/openai-compatible` | API Key/Public |

### Provider-Specific Features

#### Anthropic
- **Beta Headers**: `claude-code-20250219`, `interleaved-thinking-2025-05-14`, `fine-grained-tool-streaming-2025-05-14`
- **Prompt Caching**: Ephemeral cache control on system and final messages

#### Amazon Bedrock
- **Cross-Region Inference**: Auto-prefixes model IDs with region (e.g., `us.`, `eu.`, `jp.`, `apac.`, `global.`)
- **Credential Chain**: Supports profiles, access keys, IAM roles, web identity tokens, bearer tokens
- **Region Resolution**: Config > ENV > Default (`us-east-1`)

#### Azure
- **Responses API vs Chat API**: Configurable via `useCompletionUrls`
- **Model ID Stripping**: Strips item IDs unless `store=true`

#### GitHub Copilot
- **Model API Selection**: Uses Responses API for GPT-5+, Chat API for older models
- **Special handling for GPT-5-mini**: Uses Chat API

#### OpenRouter/Vercel
- **Custom Headers**: `HTTP-Referer`, `X-Title` for attribution

---

## Provider Architecture

### Core Components

```
Provider.ts
├── BUNDLED_PROVIDERS     # 21 pre-bundled SDK factories
├── CUSTOM_LOADERS        # Provider-specific initialization logic
├── Model schema          # Zod schema for model capabilities
├── Info schema           # Zod schema for provider configuration
└── state()               # Singleton state manager
```

### Initialization Flow

1. **Load models database** from `models.dev` (external API or local cache)
2. **Merge config providers** from user's `opencode.json`
3. **Load from environment** variables (e.g., `ANTHROPIC_API_KEY`)
4. **Load from auth store** (persisted API keys/OAuth tokens)
5. **Execute custom loaders** for provider-specific initialization
6. **Apply config overrides** (whitelist, blacklist, disabled providers)
7. **Filter models** by status (alpha requires flag, deprecated removed)

### Model Schema

```typescript
Model = {
  id: string
  providerID: string
  name: string
  family?: string
  api: {
    id: string         // Actual API model ID
    url: string        // Base URL
    npm: string        // SDK package name
  }
  capabilities: {
    temperature: boolean
    reasoning: boolean
    attachment: boolean
    toolcall: boolean
    input: { text, audio, image, video, pdf }
    output: { text, audio, image, video, pdf }
    interleaved: boolean | { field: "reasoning_content" | "reasoning_details" }
  }
  cost: {
    input: number      // $ per million tokens
    output: number
    cache: { read, write }
    experimentalOver200K?: { ... }
  }
  limit: {
    context: number
    input?: number
    output: number
  }
  status: "alpha" | "beta" | "deprecated" | "active"
  options: Record<string, any>
  headers: Record<string, string>
  variants: Record<string, Record<string, any>>
}
```

### Provider Loading Priority

1. **Environment Variables**: Highest priority for API keys
2. **Auth Store**: Persisted credentials
3. **Plugin OAuth**: For providers like GitHub Copilot
4. **Custom Loaders**: Provider-specific autoload logic
5. **Config File**: User configuration

---

## SDK Wrappers

### OpenAI-Compatible Provider

OpenCode maintains a custom fork of the OpenAI-compatible SDK specifically for **Responses API** support (used by GitHub Copilot):

```
provider/sdk/openai-compatible/
├── index.ts
├── openai-compatible-provider.ts
└── responses/
    ├── openai-responses-language-model.ts   # Main implementation
    ├── convert-to-openai-responses-input.ts # Prompt conversion
    ├── openai-responses-prepare-tools.ts    # Tool preparation
    ├── map-openai-responses-finish-reason.ts
    ├── openai-responses-api-types.ts
    ├── openai-error.ts
    ├── openai-config.ts
    └── tool/
        ├── code-interpreter.ts
        ├── file-search.ts
        ├── image-generation.ts
        ├── local-shell.ts
        ├── web-search.ts
        └── web-search-preview.ts
```

### Provider Factory Pattern

```typescript
interface OpenaiCompatibleProvider {
  (modelId: string): LanguageModelV2
  chat(modelId: string): LanguageModelV2      // Chat Completions API
  responses(modelId: string): LanguageModelV2 // Responses API
  languageModel(modelId: string): LanguageModelV2
}
```

### Custom Model Loaders

Some providers need custom model loading:

```typescript
// OpenAI uses Responses API
openai: async () => ({
  autoload: false,
  async getModel(sdk, modelID) {
    return sdk.responses(modelID)
  }
})

// GitHub Copilot switches API based on model
"github-copilot": async () => ({
  autoload: false,
  async getModel(sdk, modelID) {
    return isGpt5OrLater(modelID) && !modelID.startsWith("gpt-5-mini")
      ? sdk.responses(modelID)
      : sdk.chat(modelID)
  }
})

// Amazon Bedrock adds region prefix
"amazon-bedrock": async () => ({
  autoload: true,
  async getModel(sdk, modelID, options) {
    const region = options?.region ?? "us-east-1"
    // Add regional prefix for cross-region inference
    if (needsPrefix(modelID, region)) {
      modelID = `${getRegionPrefix(region)}.${modelID}`
    }
    return sdk.languageModel(modelID)
  }
})
```

---

## Streaming & Tool Calling

### Streaming Architecture

The `OpenAIResponsesLanguageModel` implements full streaming via the AI SDK v2 spec:

```typescript
async doStream(options): Promise<{
  stream: ReadableStream<LanguageModelV2StreamPart>
  request: { body: any }
  response: { headers: Headers }
}>
```

### Stream Event Types

The responses are processed through a `TransformStream`:

| Event Type | Description |
|------------|-------------|
| `stream-start` | Stream initialization with warnings |
| `response-metadata` | Response ID, timestamp, model ID |
| `text-start/delta/end` | Text content streaming |
| `reasoning-start/delta/end` | Reasoning/thinking content |
| `tool-input-start/delta/end` | Tool call arguments |
| `tool-call` | Complete tool invocation |
| `tool-result` | Tool execution result |
| `source` | URL/document citations |
| `finish` | Final usage stats, finish reason |
| `error` | Error handling |

### Supported Finish Reasons

```typescript
function mapOpenAIResponseFinishReason({ finishReason, hasFunctionCall }) {
  switch (finishReason) {
    case null:
    case undefined: return hasFunctionCall ? "tool-calls" : "stop"
    case "max_output_tokens": return "length"
    case "content_filter": return "content-filter"
    default: return hasFunctionCall ? "tool-calls" : "unknown"
  }
}
```

### Tool Calling Implementation

#### Function Tools

Standard tools are converted to OpenAI function format:

```typescript
{
  type: "function",
  name: tool.name,
  description: tool.description,
  parameters: tool.inputSchema,
  strict: strictJsonSchema
}
```

#### Provider-Defined Tools

OpenCode supports OpenAI's built-in tools:

| Tool ID | Type | Description |
|---------|------|-------------|
| `openai.web_search` | Web Search | Search with domain filters, location |
| `openai.web_search_preview` | Web Search Preview | Search with context size control |
| `openai.code_interpreter` | Code Interpreter | Run Python in sandbox |
| `openai.file_search` | File Search | Vector store search |
| `openai.image_generation` | Image Generation | Generate images via gpt-image-1 |
| `openai.local_shell` | Local Shell | Execute shell commands |

#### Tool Choice Options

```typescript
toolChoice:
  | "auto"
  | "none"
  | "required"
  | { type: "function"; name: string }
  | { type: "file_search" }
  | { type: "web_search" | "web_search_preview" }
  | { type: "code_interpreter" }
  | { type: "image_generation" }
```

### Input Conversion

The `convertToOpenAIResponsesInput` function handles:

1. **System messages**: `system` | `developer` | `remove` modes
2. **User messages**: Text, images (URL/base64/file_id), PDFs
3. **Assistant messages**: Text, tool calls, reasoning parts
4. **Tool messages**: Function call outputs, local shell outputs
5. **Item references**: For provider-executed tools when `store=true`

---

## Model Selection and Configuration

### Default Model Selection

```typescript
const priority = ["gpt-5", "claude-sonnet-4", "big-pickle", "gemini-3-pro"]

export async function defaultModel() {
  const cfg = await Config.get()
  if (cfg.model) return parseModel(cfg.model)

  const provider = await list().then(providers =>
    Object.values(providers).find(p =>
      !cfg.provider || Object.keys(cfg.provider).includes(p.id)
    )
  )
  const [model] = sort(Object.values(provider.models))
  return { providerID: provider.id, modelID: model.id }
}
```

### Small Model Selection

Used for fast, cheap operations:

```typescript
const priority = [
  "claude-haiku-4-5",
  "gemini-3-flash",
  "gemini-2.5-flash",
  "gpt-5-nano"
]

// GitHub Copilot prioritizes free models
if (providerID.startsWith("github-copilot")) {
  priority = ["gpt-5-mini", "claude-haiku-4.5", ...priority]
}
```

### Model Variants (Reasoning Effort)

Models with reasoning capabilities support variants:

```typescript
// Anthropic
{
  high: { thinking: { type: "enabled", budgetTokens: 16000 } },
  max: { thinking: { type: "enabled", budgetTokens: 31999 } }
}

// OpenAI
{
  none: { reasoningEffort: "none" },
  minimal: { reasoningEffort: "minimal" },
  low: { reasoningEffort: "low" },
  medium: { reasoningEffort: "medium" },
  high: { reasoningEffort: "high" },
  xhigh: { reasoningEffort: "xhigh" }
}

// Google
{
  high: { thinkingConfig: { includeThoughts: true, thinkingBudget: 16000 } },
  max: { thinkingConfig: { includeThoughts: true, thinkingBudget: 24576 } }
}
```

---

## Provider Transformations

### Message Normalization

Provider-specific message transformations in `transform.ts`:

1. **Anthropic**: Filter empty content, sanitize tool call IDs
2. **Mistral**: Normalize 9-char alphanumeric tool IDs, fix message sequence
3. **Interleaved Reasoning**: Convert reasoning parts to `reasoning_content`/`reasoning_details`

### Temperature Defaults

```typescript
function temperature(model) {
  if (model.id.includes("qwen")) return 0.55
  if (model.id.includes("claude")) return undefined  // Anthropic default
  if (model.id.includes("gemini")) return 1.0
  if (model.id.includes("kimi-k2-thinking")) return 1.0
  return undefined
}
```

### Prompt Caching

Applied to Anthropic/Claude models:

```typescript
function applyCaching(msgs, providerID) {
  const system = msgs.filter(m => m.role === "system").slice(0, 2)
  const final = msgs.filter(m => m.role !== "system").slice(-2)

  for (const msg of unique([...system, ...final])) {
    msg.providerOptions = {
      anthropic: { cacheControl: { type: "ephemeral" } },
      bedrock: { cachePoint: { type: "ephemeral" } },
      openaiCompatible: { cache_control: { type: "ephemeral" } }
    }
  }
}
```

### Schema Sanitization

For Google/Gemini models:

```typescript
// Convert integer enums to strings
// Filter required fields to match properties
// Ensure array items schema exists
```

---

## Caching and Rate Limiting

### SDK Caching

SDKs are cached by hash of provider + options:

```typescript
const key = Bun.hash.xxHash32(JSON.stringify({
  providerID: model.providerID,
  npm: model.api.npm,
  options
}))

const existing = state.sdk.get(key)
if (existing) return existing
```

### Model Database Refresh

```typescript
// Fetch models.dev on startup
ModelsDev.refresh()

// Refresh every hour
setInterval(() => ModelsDev.refresh(), 60 * 1000 * 60)
```

### Request Timeout Handling

Custom fetch wrapper with configurable timeout:

```typescript
options["fetch"] = async (input, init) => {
  const opts = init ?? {}

  if (options["timeout"] !== undefined) {
    const signals = []
    if (opts.signal) signals.push(opts.signal)
    if (options["timeout"] !== false)
      signals.push(AbortSignal.timeout(options["timeout"]))

    opts.signal = signals.length > 1
      ? AbortSignal.any(signals)
      : signals[0]
  }

  return fetch(input, { ...opts, timeout: false })
}
```

---

## Error Handling

### Provider Errors

```typescript
export const ModelNotFoundError = NamedError.create(
  "ProviderModelNotFoundError",
  z.object({
    providerID: z.string(),
    modelID: z.string(),
    suggestions: z.array(z.string()).optional()  // Fuzzy match suggestions
  })
)

export const InitError = NamedError.create(
  "ProviderInitError",
  z.object({ providerID: z.string() })
)
```

### Fuzzy Model Matching

```typescript
const matches = fuzzysort.go(modelID, availableModels, {
  limit: 3,
  threshold: -10000
})
const suggestions = matches.map(m => m.target)
throw new ModelNotFoundError({ providerID, modelID, suggestions })
```

### Provider-Specific Error Messages

```typescript
function error(providerID, error) {
  if (providerID.includes("github-copilot") && error.statusCode === 403) {
    return "Please reauthenticate with the copilot provider..."
  }
  if (providerID.includes("github-copilot") &&
      error.message.includes("not supported")) {
    return message + "\n\nMake sure the model is enabled in copilot settings..."
  }
  return error.message
}
```

---

## Authentication System

### Auth Methods

```typescript
type Method = {
  type: "oauth" | "api"
  label: string
}
```

### OAuth Flow

```typescript
// 1. Start authorization
const result = await method.authorize()
// Returns: { url, method: "auto" | "code", instructions }

// 2. Handle callback
if (method === "code") {
  result = await match.callback(code)  // Manual code entry
} else {
  result = await match.callback()       // Auto (browser redirect)
}

// 3. Store credentials
if ("key" in result) {
  await Auth.set(providerID, { type: "api", key: result.key })
}
if ("refresh" in result) {
  await Auth.set(providerID, {
    type: "oauth",
    access: result.access,
    refresh: result.refresh,
    expires: result.expires,
    accountId: result.accountId
  })
}
```

---

## Configuration Options

### Provider Config Schema

```typescript
// opencode.json
{
  "provider": {
    "anthropic": {
      "name": "Anthropic",
      "env": ["ANTHROPIC_API_KEY"],
      "api": "https://api.anthropic.com",
      "npm": "@ai-sdk/anthropic",
      "options": { ... },
      "whitelist": ["claude-sonnet-4"],
      "blacklist": ["claude-2"],
      "models": {
        "claude-custom": {
          "id": "claude-sonnet-4-20250514",
          "name": "Custom Claude",
          "cost": { "input": 3, "output": 15 },
          "limit": { "context": 200000, "output": 8192 },
          "options": { ... },
          "variants": {
            "high": { "thinking": { ... } }
          }
        }
      }
    }
  },
  "disabled_providers": ["cohere"],
  "enabled_providers": ["anthropic", "openai"],
  "model": "anthropic/claude-sonnet-4",
  "small_model": "anthropic/claude-haiku-4-5"
}
```

---

## Key Takeaways for AVA

1. **Use AI SDK abstractions**: OpenCode builds on Vercel AI SDK, providing a clean abstraction layer

2. **Centralized model database**: External `models.dev` API with local caching provides up-to-date model info

3. **Custom SDK wrappers**: Maintain custom wrappers (like OpenAI-compatible) for advanced features

4. **Provider-specific transformations**: Handle quirks per-provider (message normalization, caching, tool IDs)

5. **Flexible auth system**: Support both API keys and OAuth with plugin-based extensibility

6. **Fuzzy matching**: Provide suggestions for typos in model/provider names

7. **Variants for reasoning**: Expose reasoning effort/budget as model variants

8. **SDK caching**: Cache SDK instances by configuration hash to avoid recreation

9. **Streaming architecture**: Use TransformStream for clean event processing

10. **Provider-defined tools**: Support OpenAI's built-in tools (web search, code interpreter, etc.)
