/**
 * Dynamic Model Fetcher
 *
 * Fetches available models from provider APIs. All 14 providers now support
 * dynamic fetching (or return documented models for Anthropic).
 *
 * - OpenAI-compat (GET /v1/models): OpenAI, xAI, Mistral, Groq, DeepSeek, Together, Kimi
 * - OpenRouter: GET /api/v1/models
 * - Copilot: GET /models
 * - Google: GET /v1beta/models
 * - Cohere: GET /v2/models (custom shape)
 * - GLM (Zhipu): GET /v4/models (custom shape)
 * - Ollama: GET /api/tags
 * - Anthropic: Documented models (no list API)
 */

import type { LLMProvider } from '../../types/llm'
import { logWarn } from '../logger'

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

interface CopilotModel {
  id: string
  name: string
  version: string
  capabilities?: {
    type: string
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
  copilot: 'https://api.githubcopilot.com/models',
  ollama: 'http://localhost:11434/api/tags',
}

// ============================================================================
// Context Window Estimates (for APIs that don't provide this)
// ============================================================================

const OPENAI_CONTEXT_WINDOWS: Record<string, number> = {
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
  'gpt-5.2-codex': 400000,
  'gpt-5.1-codex': 400000,
  // GPT-4.1 family
  'gpt-4.1': 1047576,
  'gpt-4.1-mini': 1047576,
  'gpt-4.1-nano': 1047576,
  // GPT-4o family (legacy)
  'gpt-4o': 128000,
  'gpt-4o-mini': 128000,
  'gpt-4-turbo': 128000,
  // o-series reasoning
  o3: 200000,
  'o3-mini': 200000,
  'o3-pro': 200000,
  'o4-mini': 200000,
  // Open-weight
  'gpt-oss-120b': 128000,
  'gpt-oss-20b': 128000,
}

const COPILOT_CONTEXT_WINDOWS: Record<string, number> = {
  'gpt-4.1': 1000000,
  'gpt-4o': 128000,
  'gpt-4o-mini': 128000,
  'claude-3.5-sonnet': 200000,
  'o3-mini': 200000,
  'o1-mini': 128000,
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

  // Filter to only chat/reasoning/coding models
  const chatModels = models.filter((m) => {
    const id = m.id.toLowerCase()
    if (m.owned_by === 'user') return false // Exclude fine-tuned models
    if (id.includes('embedding') || id.includes('tts') || id.includes('whisper')) return false
    if (id.includes('realtime') || id.includes('audio') || id.includes('moderation')) return false
    if (id.includes('dall') || id.includes('image') || id.includes('sora')) return false
    return (
      id.includes('gpt') ||
      id.startsWith('o3') ||
      id.startsWith('o4') ||
      id.startsWith('gpt-oss') ||
      id.includes('codex')
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
 * Fetch models from GitHub Copilot API
 */
async function fetchCopilotModels(token: string): Promise<FetchedModel[]> {
  const response = await fetch(ENDPOINTS.copilot, {
    headers: {
      Authorization: `Bearer ${token}`,
      'Copilot-Integration-Id': 'vscode-chat',
    },
  })

  if (!response.ok) {
    throw new Error(`Copilot API error: ${response.status} ${response.statusText}`)
  }

  const data = await response.json()
  const models: CopilotModel[] = data.data ?? data.models ?? data

  return models
    .filter((m) => !m.capabilities || m.capabilities.type === 'chat')
    .map((model) => ({
      id: model.id,
      name: model.name || formatModelName(model.id),
      contextWindow: findContextWindow(model.id, COPILOT_CONTEXT_WINDOWS),
    }))
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
    {
      id: 'claude-opus-4-6',
      name: 'Claude Opus 4.6',
      contextWindow: 200000,
      pricing: { prompt: 5, completion: 25 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-sonnet-4-6',
      name: 'Claude Sonnet 4.6',
      contextWindow: 200000,
      pricing: { prompt: 3, completion: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-haiku-4-5-20251001',
      name: 'Claude Haiku 4.5',
      contextWindow: 200000,
      pricing: { prompt: 1, completion: 5 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-sonnet-4-5-20250929',
      name: 'Claude Sonnet 4.5',
      contextWindow: 200000,
      pricing: { prompt: 3, completion: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-opus-4-5-20251101',
      name: 'Claude Opus 4.5',
      contextWindow: 200000,
      pricing: { prompt: 5, completion: 25 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-opus-4-1-20250805',
      name: 'Claude Opus 4.1',
      contextWindow: 200000,
      pricing: { prompt: 15, completion: 75 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-sonnet-4-20250514',
      name: 'Claude Sonnet 4',
      contextWindow: 200000,
      pricing: { prompt: 3, completion: 15 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
    {
      id: 'claude-opus-4-20250514',
      name: 'Claude Opus 4',
      contextWindow: 200000,
      pricing: { prompt: 15, completion: 75 },
      capabilities: ['vision', 'tools', 'reasoning'],
    },
  ]
}

/**
 * Fetch models from Google AI (Generative Language API)
 */
async function fetchGoogleModels(apiKey: string): Promise<FetchedModel[]> {
  const response = await fetch(
    `https://generativelanguage.googleapis.com/v1beta/models?key=${apiKey}`
  )

  if (!response.ok) {
    throw new Error(`Google AI API error: ${response.status} ${response.statusText}`)
  }

  interface GoogleModel {
    name: string
    displayName: string
    inputTokenLimit?: number
    outputTokenLimit?: number
    supportedGenerationMethods?: string[]
  }

  const data = (await response.json()) as { models: GoogleModel[] }
  const models = data.models || []

  // Only include models that support generateContent (chat-capable)
  return models
    .filter((m) => m.supportedGenerationMethods?.includes('generateContent'))
    .map((model) => ({
      id: model.name.replace('models/', ''),
      name: model.displayName || model.name.replace('models/', ''),
      contextWindow: model.inputTokenLimit || 32000,
    }))
    .sort((a, b) => b.contextWindow - a.contextWindow)
}

// ============================================================================
// OpenAI-Compatible Fetcher (xAI, Mistral, Groq, DeepSeek, Together, Kimi)
// ============================================================================

interface OpenAICompatConfig {
  baseUrl: string
  providerName: string
  filterFn?: (model: OpenAIModel) => boolean
}

const OPENAI_COMPAT_CONFIGS: Record<string, OpenAICompatConfig> = {
  xai: {
    baseUrl: 'https://api.x.ai/v1/models',
    providerName: 'xAI',
    filterFn: (m) => m.owned_by.includes('xai') || m.id.startsWith('grok'),
  },
  mistral: {
    baseUrl: 'https://api.mistral.ai/v1/models',
    providerName: 'Mistral',
  },
  groq: {
    baseUrl: 'https://api.groq.com/openai/v1/models',
    providerName: 'Groq',
    filterFn: (m) => !m.id.includes('whisper') && !m.id.includes('tool-use'),
  },
  deepseek: {
    baseUrl: 'https://api.deepseek.com/models',
    providerName: 'DeepSeek',
  },
  together: {
    baseUrl: 'https://api.together.xyz/v1/models',
    providerName: 'Together',
    filterFn: (m) => m.id.includes('Instruct') || m.id.includes('chat') || m.id.includes('Chat'),
  },
  kimi: {
    baseUrl: 'https://api.moonshot.cn/v1/models',
    providerName: 'Kimi',
  },
}

/**
 * Generic fetcher for OpenAI-compatible /v1/models endpoints
 */
async function fetchOpenAICompatModels(
  apiKey: string,
  config: OpenAICompatConfig
): Promise<FetchedModel[]> {
  const response = await fetch(config.baseUrl, {
    headers: { Authorization: `Bearer ${apiKey}` },
  })

  if (!response.ok) {
    throw new Error(`${config.providerName} API error: ${response.status} ${response.statusText}`)
  }

  const data = await response.json()
  const models: OpenAIModel[] = data.data || []
  const filtered = config.filterFn ? models.filter(config.filterFn) : models

  return filtered.map((model) => ({
    id: model.id,
    name: formatModelName(model.id),
    contextWindow: 4096, // OpenAI-compat APIs don't return context window
  }))
}

// ============================================================================
// Cohere Fetcher (custom response shape)
// ============================================================================

/**
 * Fetch models from Cohere API (v2)
 */
async function fetchCohereModels(apiKey: string): Promise<FetchedModel[]> {
  const response = await fetch('https://api.cohere.com/v2/models', {
    headers: { Authorization: `Bearer ${apiKey}` },
  })

  if (!response.ok) {
    throw new Error(`Cohere API error: ${response.status} ${response.statusText}`)
  }

  interface CohereModel {
    name: string
    endpoints: string[]
    context_length?: number
  }

  const data = (await response.json()) as { models: CohereModel[] }
  const models = data.models || []

  return models
    .filter((m) => m.endpoints?.includes('chat'))
    .map((model) => ({
      id: model.name,
      name: formatModelName(model.name),
      contextWindow: model.context_length || 128000,
    }))
}

// ============================================================================
// GLM (Zhipu) Fetcher (custom response shape)
// ============================================================================

/**
 * Fetch models from Zhipu (GLM) API
 */
async function fetchGLMModels(apiKey: string): Promise<FetchedModel[]> {
  const response = await fetch('https://open.bigmodel.cn/api/paas/v4/models', {
    headers: { Authorization: `Bearer ${apiKey}` },
  })

  if (!response.ok) {
    throw new Error(`GLM API error: ${response.status} ${response.statusText}`)
  }

  interface GLMModel {
    id: string
    object: string
  }

  const data = (await response.json()) as { data: GLMModel[] }
  const models = data.data || []

  return models
    .filter((m) => m.id.startsWith('glm'))
    .map((model) => ({
      id: model.id,
      name: formatModelName(model.id),
      contextWindow: 128000,
    }))
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

    case 'copilot':
      if (options.apiKey) {
        try {
          return await fetchCopilotModels(options.apiKey)
        } catch {
          logWarn('models', 'Could not fetch Copilot models, using defaults')
        }
      }
      return [
        {
          id: 'gpt-4.1',
          name: 'GPT-4.1',
          contextWindow: 1000000,
          capabilities: ['vision', 'tools'],
        },
        {
          id: 'gpt-4o',
          name: 'GPT-4o',
          contextWindow: 128000,
          capabilities: ['vision', 'tools'],
        },
        {
          id: 'claude-3.5-sonnet',
          name: 'Claude 3.5 Sonnet',
          contextWindow: 200000,
          capabilities: ['vision', 'tools'],
        },
        {
          id: 'o3-mini',
          name: 'o3 Mini',
          contextWindow: 200000,
          capabilities: ['tools', 'reasoning'],
        },
      ]

    case 'openrouter':
      return fetchOpenRouterModels(options.apiKey)

    case 'anthropic':
      // No API, return documented models
      return getAnthropicModels()

    case 'google':
      if (options.apiKey) {
        try {
          return await fetchGoogleModels(options.apiKey)
        } catch {
          logWarn('models', 'Could not fetch Google models, using defaults')
        }
      }
      return [
        { id: 'gemini-3-pro-preview', name: 'Gemini 3 Pro', contextWindow: 2000000 },
        { id: 'gemini-2.5-pro', name: 'Gemini 2.5 Pro', contextWindow: 1000000 },
        { id: 'gemini-2.5-flash', name: 'Gemini 2.5 Flash', contextWindow: 1000000 },
        { id: 'gemini-2.0-flash', name: 'Gemini 2.0 Flash', contextWindow: 1000000 },
      ]

    case 'xai':
    case 'mistral':
    case 'groq':
    case 'deepseek':
    case 'together':
    case 'kimi': {
      const config = OPENAI_COMPAT_CONFIGS[provider]
      if (options.apiKey && config) {
        try {
          return await fetchOpenAICompatModels(options.apiKey, config)
        } catch {
          logWarn('models', `Could not fetch ${config.providerName} models, using defaults`)
        }
      }
      return [] // Fall back to static defaults in provider config
    }

    case 'cohere':
      if (options.apiKey) {
        try {
          return await fetchCohereModels(options.apiKey)
        } catch {
          logWarn('models', 'Could not fetch Cohere models, using defaults')
        }
      }
      return []

    case 'glm':
      if (options.apiKey) {
        try {
          return await fetchGLMModels(options.apiKey)
        } catch {
          logWarn('models', 'Could not fetch GLM models, using defaults')
        }
      }
      return []

    case 'ollama':
      try {
        return await fetchOllamaModels(options.baseUrl)
      } catch {
        logWarn('models', 'Could not fetch Ollama models, using defaults')
        return []
      }
  }
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
