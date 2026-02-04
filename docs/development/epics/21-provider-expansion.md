# Epic 21: Provider & Intelligence Expansion

> More providers, model-specific prompts, and code intelligence features

**Status**: Planning
**Estimated Lines**: ~1,500
**Dependencies**: Epic 19-20

---

## Goals

1. **Provider Expansion** - Support 15+ LLM providers
2. **Model-Specific Prompts** - Variants for different model families
3. **Code Intelligence** - Tree-sitter parsing, basic LSP diagnostics

---

## Analysis: Key Features from Cline & OpenCode

### From Cline
- 43 API providers supported
- 11 system prompt variants (Claude, GPT, Gemini, etc.)
- list_code_definition_names tool (tree-sitter)

### From OpenCode
- 20+ providers via AI SDK
- Provider-specific message transforms
- 43+ LSP language servers
- Diagnostics in edit tool output
- Tree-sitter bash parsing for command analysis

---

## Sprint Plan

### Sprint 21.1: Provider Expansion (~500 lines)

**Goal**: Add support for more LLM providers.

**Files to create/modify**:
- `packages/core/src/llm/providers/` - New provider implementations
- `packages/core/src/llm/registry.ts` - Provider registration

**New Providers to Add**:
```typescript
// Priority 1: Major providers
- Mistral
- Groq
- DeepSeek
- xAI (Grok)
- Cohere

// Priority 2: Cloud platforms
- AWS Bedrock
- Azure OpenAI
- Google Vertex AI

// Priority 3: Local/OSS
- Ollama
- LM Studio
- Together AI
```

**Provider Interface**:
```typescript
interface LLMProvider {
  id: string
  name: string
  models: ModelInfo[]

  // Authentication
  authType: 'api_key' | 'oauth' | 'none'

  // Capabilities
  supportsStreaming: boolean
  supportsTools: boolean
  supportsVision: boolean

  // Create client
  createClient(config: ProviderConfig): LLMClient
}
```

---

### Sprint 21.2: Model-Specific System Prompts (~400 lines)

**Goal**: Different prompt variants for model families.

**Files to create**:
- `packages/core/src/agent/prompts/variants/` (~300 lines)
- `packages/core/src/agent/prompts/matcher.ts` (~100 lines)

**Model Families**:
```typescript
enum ModelFamily {
  CLAUDE,      // Anthropic Claude models
  GPT,         // OpenAI GPT models
  GEMINI,      // Google Gemini models
  GENERIC,     // Default fallback
}

interface PromptVariant {
  family: ModelFamily
  components: PromptComponent[]
  toolFormat: 'xml' | 'native'
  rules: string
  capabilities: string
}
```

**Variant Differences**:

| Aspect | Claude | GPT | Gemini | Generic |
|--------|--------|-----|--------|---------|
| Tool format | XML | Native | Native | XML |
| Rules length | Full | Concise | Medium | Full |
| Markdown | Backticks selective | Standard | Standard | Standard |
| Artifacts | Supported | Limited | Limited | None |

**Matcher Logic**:
```typescript
function getPromptVariant(modelId: string): PromptVariant {
  if (modelId.includes('claude')) return CLAUDE_VARIANT
  if (modelId.includes('gpt')) return GPT_VARIANT
  if (modelId.includes('gemini')) return GEMINI_VARIANT
  return GENERIC_VARIANT
}
```

---

### Sprint 21.3: Tree-Sitter Integration (~350 lines)

**Goal**: Code parsing for bash commands and symbol extraction.

**Files to create**:
- `packages/core/src/codebase/treesitter/parser.ts` (~200 lines)
- `packages/core/src/codebase/treesitter/bash.ts` (~150 lines)

**Bash Command Analysis**:
```typescript
interface BashAnalysis {
  commands: string[]        // Individual commands
  directories: string[]     // Directories accessed
  files: string[]          // Files accessed
  hasRedirects: boolean    // Uses >, >>, <
  hasPipes: boolean        // Uses |
  hasBackticks: boolean    // Uses ` or $()
  isDestructive: boolean   // rm, mv, etc.
}

async function analyzeBashCommand(command: string): Promise<BashAnalysis> {
  const tree = await parser.parse(command)
  // Extract command info from AST
  return analysis
}
```

**Symbol Extraction**:
```typescript
interface CodeDefinition {
  name: string
  kind: 'function' | 'class' | 'interface' | 'type' | 'variable'
  line: number
  signature?: string
}

async function extractDefinitions(
  filePath: string,
  language: string
): Promise<CodeDefinition[]> {
  const tree = await parser.parse(content, language)
  // Walk tree and extract definitions
  return definitions
}
```

---

### Sprint 21.4: Basic LSP Integration (~250 lines)

**Goal**: Get diagnostics from language servers for edit feedback.

**Files to create**:
- `packages/core/src/lsp/client.ts` (~150 lines)
- `packages/core/src/lsp/diagnostics.ts` (~100 lines)

**Supported Languages (Initial)**:
- TypeScript/JavaScript (via typescript-language-server)
- Python (via pyright)
- Go (via gopls)

**LSP Client**:
```typescript
class LSPClient {
  private connection: MessageConnection

  async initialize(workspaceRoot: string): Promise<void>
  async didOpen(filePath: string, content: string): Promise<void>
  async didChange(filePath: string, content: string): Promise<void>
  async getDiagnostics(): Promise<Map<string, Diagnostic[]>>
}
```

**Diagnostics in Edit Output**:
```typescript
// After edit completes
await lspClient.didChange(filePath, newContent)
const diagnostics = await lspClient.getDiagnostics()
const errors = diagnostics.get(filePath)?.filter(d => d.severity === 1)

if (errors?.length > 0) {
  output += '\n\nLSP errors detected:\n'
  output += errors.map(formatDiagnostic).join('\n')
}
```

---

## Summary

| Sprint | Focus | Lines |
|--------|-------|-------|
| 21.1 | Provider expansion (10+ new) | ~500 |
| 21.2 | Model-specific prompts (4 variants) | ~400 |
| 21.3 | Tree-sitter (bash + symbols) | ~350 |
| 21.4 | Basic LSP diagnostics | ~250 |
| **Total** | | **~1,500** |

---

## Success Criteria

- [ ] 15+ providers supported
- [ ] Model-specific prompt variants load correctly
- [ ] Bash commands analyzed for directories/files
- [ ] Symbol extraction works for TS/JS
- [ ] LSP diagnostics appear after file edits
- [ ] All existing tests pass

---

## Future Considerations (Not in this Epic)

- Full LSP operation support (goto definition, find references)
- 43+ language server support
- Tree-sitter for all languages
- Provider-specific message transforms
- OAuth for additional providers
