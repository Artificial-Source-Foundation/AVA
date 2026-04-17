/**
 * Model Fetcher — Types, Constants & Helpers
 *
 * Shared types, API endpoints, context window data maps,
 * and utility functions used by model fetcher modules.
 */

// ============================================================================
// Types
// ============================================================================

export interface FetchedModel {
  id: string
  name: string
  contextWindow: number
  description?: string
  pricing?: {
    prompt: number // per 1M tokens
    completion: number // per 1M tokens
  }
  capabilities?: string[]
}

export interface OpenAIModel {
  id: string
  object: 'model'
  created: number
  owned_by: string
}

export interface OpenRouterModel {
  id: string
  name: string
  description?: string
  context_length: number
  pricing: {
    prompt: string
    completion: string
  }
  top_provider?: {
    context_length: number
  }
}

export interface CopilotModel {
  id: string
  name: string
  version: string
  capabilities?: {
    type: string
  }
}

export interface OllamaModel {
  name: string
  modified_at: string
  size: number
  digest: string
  details: {
    format: string
    family: string
    families?: string[]
    parameter_size: string
    quantization_level: string
  }
}

export interface OpenAICompatConfig {
  baseUrl: string
  providerName: string
  filterFn?: (model: OpenAIModel) => boolean
}

export interface FetchModelsOptions {
  apiKey?: string
  baseUrl?: string
  authType?: 'api-key' | 'oauth-token'
}

// ============================================================================
// API Endpoints
// ============================================================================

export const ENDPOINTS = {
  openai: 'https://api.openai.com/v1/models',
  openrouter: 'https://openrouter.ai/api/v1/models',
  copilot: 'https://api.githubcopilot.com/models',
  ollama: 'http://localhost:11434/api/tags',
}

// ============================================================================
// Context Window Estimates (for APIs that don't provide this)
// ============================================================================

export const OPENAI_CONTEXT_WINDOWS: Record<string, number> = {
  // GPT-5 family
  'gpt-5.2': 400000,
  'gpt-5.2-chat': 400000,
  'gpt-5.2-pro': 400000,
  'gpt-5.1': 400000,
  'gpt-5.1-chat': 128000,
  'gpt-5': 400000,
  'gpt-5-mini': 400000,
  'gpt-5-nano': 400000,
  // Codex family
  'gpt-5.3-codex': 400000,
  'gpt-5.3-codex-spark': 128000,
  'gpt-5.2-codex': 400000,
  'gpt-5.1-codex': 400000,
  'gpt-5.1-codex-max': 400000,
  'gpt-5-codex': 400000,
  // Pro family
  'gpt-5-pro': 400000,
  'o3-pro': 200000,
  // GPT-4.1 family
  'gpt-4.1': 1048576,
  'gpt-4.1-mini': 1048576,
  'gpt-4.1-nano': 1048576,
  // GPT-4o family (legacy)
  'gpt-4o': 128000,
  'gpt-4o-mini': 128000,
  'gpt-4-turbo': 128000,
  // o-series reasoning
  o3: 200000,
  'o3-mini': 200000,
  'o4-mini': 200000,
  // Open-weight
  'gpt-oss-120b': 128000,
  'gpt-oss-20b': 128000,
}

// Copilot enforces its own context limits (lower than native provider limits)
export const COPILOT_CONTEXT_WINDOWS: Record<string, number> = {
  // OpenAI
  'gpt-4.1': 64000,
  'gpt-4o': 64000,
  'gpt-5': 128000,
  'gpt-5-mini': 128000,
  'gpt-5.1': 128000,
  'gpt-5.1-codex': 128000,
  'gpt-5.1-codex-max': 128000,
  'gpt-5.1-codex-mini': 128000,
  'gpt-5.2': 128000,
  'gpt-5.2-codex': 272000,
  // Anthropic
  'claude-haiku-4.5': 128000,
  'claude-sonnet-4': 128000,
  'claude-sonnet-4.5': 128000,
  'claude-sonnet-4.6': 128000,
  'claude-opus-41': 80000,
  'claude-opus-4.5': 128000,
  'claude-opus-4.6': 128000,
  // Google
  'gemini-2.5-pro': 128000,
  'gemini-3-flash-preview': 128000,
  'gemini-3-pro-preview': 128000,
  'gemini-3.1-pro-preview': 128000,
  // xAI
  'grok-code-fast-1': 128000,
}

export const OLLAMA_CONTEXT_WINDOWS: Record<string, number> = {
  llama3: 128000,
  'llama3.3': 128000,
  'llama3.2': 128000,
  'llama3.1': 128000,
  llama2: 4096,
  codellama: 16000,
  mistral: 32000,
  mixtral: 32000,
  deepseek: 64000,
  'deepseek-coder': 128000,
  'deepseek-r1': 64000,
  qwen: 32000,
  'qwen2.5': 32000,
  phi: 16000,
  phi3: 128000,
  phi4: 16000,
  gemma: 8192,
  gemma2: 8192,
}

// ============================================================================
// Helpers
// ============================================================================

export function findContextWindow(modelId: string, known: Record<string, number>): number {
  // Try exact match first
  if (known[modelId]) return known[modelId]

  // Try prefix match
  const modelLower = modelId.toLowerCase()
  for (const [key, value] of Object.entries(known)) {
    if (modelLower.startsWith(key.toLowerCase())) {
      return value
    }
  }

  // Default
  return 4096
}

export function formatModelName(id: string): string {
  // Remove provider prefix if present (e.g., "openai/gpt-4" -> "GPT-4")
  const name = id.includes('/') ? id.split('/').pop()! : id

  // Convert to title case and clean up
  return name
    .replace(/-/g, ' ')
    .replace(/\./g, '.')
    .split(' ')
    .map((word) => {
      // Keep certain words uppercase
      if (['gpt', 'llm', 'ai'].includes(word.toLowerCase())) {
        return word.toUpperCase()
      }
      // Capitalize first letter
      return word.charAt(0).toUpperCase() + word.slice(1)
    })
    .join(' ')
    .replace(/(\d)([a-z])/gi, '$1 $2') // Add space between numbers and letters
    .trim()
}
