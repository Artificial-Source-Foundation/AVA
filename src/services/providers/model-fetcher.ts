/**
 * Dynamic Model Fetcher
 *
 * Fetches available models from provider APIs:
 * - OpenAI: GET /v1/models
 * - OpenRouter: GET /api/v1/models
 * - Ollama: GET /api/tags
 * - Anthropic: No public API (uses documented models)
 * - Google: Uses documented models
 *
 * Sources:
 * - https://platform.openai.com/docs/api-reference/models/list
 * - https://openrouter.ai/docs/api/api-reference/models/get-models
 * - https://docs.ollama.com/api/tags
 */

import type { LLMProvider } from '../../types/llm'

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

interface OpenAIModel {
  id: string
  object: 'model'
  created: number
  owned_by: string
}

interface OpenRouterModel {
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

interface OllamaModel {
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

// ============================================================================
// API Endpoints
// ============================================================================

const ENDPOINTS = {
  openai: 'https://api.openai.com/v1/models',
  openrouter: 'https://openrouter.ai/api/v1/models',
  ollama: 'http://localhost:11434/api/tags',
}

// ============================================================================
// Context Window Estimates (for APIs that don't provide this)
// ============================================================================

const OPENAI_CONTEXT_WINDOWS: Record<string, number> = {
  'gpt-5.2': 196000,
  'gpt-5.1': 196000,
  'gpt-4.1': 1000000,
  'gpt-4.1-mini': 1000000,
  'gpt-4.1-nano': 1000000,
  'gpt-4o': 128000,
  'gpt-4o-mini': 128000,
  'gpt-4-turbo': 128000,
  'gpt-4': 8192,
  'gpt-3.5-turbo': 16385,
  o3: 200000,
  'o3-mini': 200000,
  'o3-pro': 200000,
  'o4-mini': 200000,
  o1: 200000,
  'o1-mini': 128000,
  'o1-preview': 128000,
}

const OLLAMA_CONTEXT_WINDOWS: Record<string, number> = {
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
// Model Fetchers
// ============================================================================

/**
 * Fetch models from OpenAI API
 */
async function fetchOpenAIModels(apiKey: string): Promise<FetchedModel[]> {
  const response = await fetch(ENDPOINTS.openai, {
    headers: {
      Authorization: `Bearer ${apiKey}`,
    },
  })

  if (!response.ok) {
    throw new Error(`OpenAI API error: ${response.status} ${response.statusText}`)
  }

  const data = await response.json()
  const models: OpenAIModel[] = data.data

  // Filter to only chat models (exclude embeddings, whisper, dall-e, etc.)
  const chatModels = models.filter((m) => {
    const id = m.id.toLowerCase()
    return (
      (id.includes('gpt') || id.startsWith('o1') || id.startsWith('o3') || id.startsWith('o4')) &&
      !id.includes('embedding') &&
      !id.includes('instruct') &&
      !id.includes('realtime') &&
      m.owned_by !== 'user' // Exclude fine-tuned models
    )
  })

  return chatModels.map((model) => {
    // Find context window from known values or estimate
    const contextWindow = findContextWindow(model.id, OPENAI_CONTEXT_WINDOWS)

    return {
      id: model.id,
      name: formatModelName(model.id),
      contextWindow,
    }
  })
}

/**
 * Fetch models from OpenRouter API
 */
async function fetchOpenRouterModels(apiKey?: string): Promise<FetchedModel[]> {
  const headers: Record<string, string> = {}
  if (apiKey) {
    headers.Authorization = `Bearer ${apiKey}`
  }

  const response = await fetch(ENDPOINTS.openrouter, { headers })

  if (!response.ok) {
    throw new Error(`OpenRouter API error: ${response.status} ${response.statusText}`)
  }

  const data = await response.json()
  const models: OpenRouterModel[] = data.data

  // Sort by popularity/relevance - put major providers first
  const priorityProviders = [
    'anthropic',
    'openai',
    'google',
    'deepseek',
    'meta-llama',
    'mistralai',
    'x-ai',
  ]

  return models
    .map((model) => ({
      id: model.id,
      name: model.name || formatModelName(model.id),
      contextWindow: model.context_length || model.top_provider?.context_length || 4096,
      description: model.description,
      pricing: {
        prompt: parseFloat(model.pricing.prompt) * 1000000,
        completion: parseFloat(model.pricing.completion) * 1000000,
      },
    }))
    .sort((a, b) => {
      const aProvider = a.id.split('/')[0]
      const bProvider = b.id.split('/')[0]
      const aIdx = priorityProviders.indexOf(aProvider)
      const bIdx = priorityProviders.indexOf(bProvider)

      if (aIdx !== -1 && bIdx !== -1) return aIdx - bIdx
      if (aIdx !== -1) return -1
      if (bIdx !== -1) return 1
      return a.name.localeCompare(b.name)
    })
}

/**
 * Fetch models from Ollama API (local)
 */
async function fetchOllamaModels(baseUrl?: string): Promise<FetchedModel[]> {
  const url = baseUrl ? `${baseUrl}/api/tags` : ENDPOINTS.ollama

  const response = await fetch(url)

  if (!response.ok) {
    throw new Error(`Ollama API error: ${response.status} ${response.statusText}`)
  }

  const data = await response.json()
  const models: OllamaModel[] = data.models || []

  return models.map((model) => {
    const baseName = model.name.split(':')[0]
    const contextWindow = findContextWindow(baseName, OLLAMA_CONTEXT_WINDOWS)

    return {
      id: model.name,
      name: `${formatModelName(baseName)} (${model.details.parameter_size})`,
      contextWindow,
      capabilities: [model.details.quantization_level, model.details.format],
    }
  })
}

/**
 * Get Anthropic models (documented, no API)
 */
function getAnthropicModels(): FetchedModel[] {
  // Anthropic doesn't have a models list API
  // These are the current documented models as of Feb 2026
  return [
    { id: 'claude-sonnet-4-5-20250514', name: 'Claude Sonnet 4.5', contextWindow: 200000 },
    { id: 'claude-opus-4-5-20251124', name: 'Claude Opus 4.5', contextWindow: 200000 },
    { id: 'claude-haiku-4-5-20251022', name: 'Claude Haiku 4.5', contextWindow: 200000 },
    { id: 'claude-opus-4-1-20250801', name: 'Claude Opus 4.1', contextWindow: 200000 },
    { id: 'claude-opus-4-20250514', name: 'Claude Opus 4', contextWindow: 200000 },
    { id: 'claude-sonnet-4-20250514', name: 'Claude Sonnet 4', contextWindow: 200000 },
    { id: 'claude-3-7-sonnet-20250219', name: 'Claude 3.7 Sonnet', contextWindow: 200000 },
    { id: 'claude-3-5-sonnet-20241022', name: 'Claude 3.5 Sonnet v2', contextWindow: 200000 },
    { id: 'claude-3-5-haiku-20241022', name: 'Claude 3.5 Haiku', contextWindow: 200000 },
  ]
}

// ============================================================================
// Main Export
// ============================================================================

export interface FetchModelsOptions {
  apiKey?: string
  baseUrl?: string
}

/**
 * Fetch models for a specific provider
 */
export async function fetchModels(
  provider: LLMProvider,
  options: FetchModelsOptions = {}
): Promise<FetchedModel[]> {
  switch (provider) {
    case 'openai':
      if (!options.apiKey) {
        throw new Error('OpenAI API key required to fetch models')
      }
      return fetchOpenAIModels(options.apiKey)

    case 'openrouter':
      return fetchOpenRouterModels(options.apiKey)

    case 'anthropic':
      // No API, return documented models
      return getAnthropicModels()

    case 'google':
      // TODO: Implement Google AI models API
      return [
        { id: 'gemini-3-pro-preview', name: 'Gemini 3 Pro', contextWindow: 2000000 },
        { id: 'gemini-2.5-pro', name: 'Gemini 2.5 Pro', contextWindow: 1000000 },
        { id: 'gemini-2.5-flash', name: 'Gemini 2.5 Flash', contextWindow: 1000000 },
        { id: 'gemini-2.0-flash', name: 'Gemini 2.0 Flash', contextWindow: 1000000 },
      ]

    default:
      // For Ollama and other local providers
      try {
        return await fetchOllamaModels(options.baseUrl)
      } catch {
        console.warn(`Could not fetch models for ${provider}, using defaults`)
        return []
      }
  }
}

/**
 * Check if a provider supports dynamic model fetching
 */
export function supportsDynamicFetch(provider: LLMProvider): boolean {
  return ['openai', 'openrouter', 'ollama'].includes(provider)
}

// ============================================================================
// Helpers
// ============================================================================

function findContextWindow(modelId: string, known: Record<string, number>): number {
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

function formatModelName(id: string): string {
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
