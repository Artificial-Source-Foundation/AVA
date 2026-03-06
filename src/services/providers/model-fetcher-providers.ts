/**
 * Model Fetcher — Provider API Implementations
 *
 * Individual fetch functions for each LLM provider's model list API.
 * Static/documented model lists are in model-fetcher-static.ts.
 */

import type {
  CopilotModel,
  FetchedModel,
  OllamaModel,
  OpenAICompatConfig,
  OpenAIModel,
  OpenRouterModel,
} from './model-fetcher-types'
import {
  COPILOT_CONTEXT_WINDOWS,
  ENDPOINTS,
  findContextWindow,
  formatModelName,
  OLLAMA_CONTEXT_WINDOWS,
  OPENAI_CONTEXT_WINDOWS,
} from './model-fetcher-types'

// Re-export static models and configs from the static module
export {
  getAlibabaModels,
  getAnthropicModels,
  OPENAI_COMPAT_CONFIGS,
} from './model-fetcher-static'

// ============================================================================
// OpenAI
// ============================================================================

export async function fetchOpenAIModels(apiKey: string): Promise<FetchedModel[]> {
  const response = await fetch(ENDPOINTS.openai, {
    headers: { Authorization: `Bearer ${apiKey}` },
  })

  if (!response.ok) {
    throw new Error(`OpenAI API error: ${response.status} ${response.statusText}`)
  }

  const data = await response.json()
  const models: OpenAIModel[] = data.data

  const chatModels = models.filter((m) => {
    const id = m.id.toLowerCase()
    if (m.owned_by === 'user') return false
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

  return chatModels.map((model) => ({
    id: model.id,
    name: formatModelName(model.id),
    contextWindow: findContextWindow(model.id, OPENAI_CONTEXT_WINDOWS),
  }))
}

// ============================================================================
// Copilot
// ============================================================================

export async function fetchCopilotModels(token: string): Promise<FetchedModel[]> {
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

// ============================================================================
// OpenRouter
// ============================================================================

export async function fetchOpenRouterModels(apiKey?: string): Promise<FetchedModel[]> {
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

// ============================================================================
// Ollama (local)
// ============================================================================

export async function fetchOllamaModels(baseUrl?: string): Promise<FetchedModel[]> {
  const url = baseUrl ? `${baseUrl}/api/tags` : ENDPOINTS.ollama
  const response = await fetch(url)

  if (!response.ok) {
    throw new Error(`Ollama API error: ${response.status} ${response.statusText}`)
  }

  const data = await response.json()
  const models: OllamaModel[] = data.models || []

  return models.map((model) => {
    const baseName = model.name.split(':')[0]
    return {
      id: model.name,
      name: `${formatModelName(baseName)} (${model.details.parameter_size})`,
      contextWindow: findContextWindow(baseName, OLLAMA_CONTEXT_WINDOWS),
      capabilities: [model.details.quantization_level, model.details.format],
    }
  })
}

// ============================================================================
// Google AI (Generative Language API)
// ============================================================================

export async function fetchGoogleModels(apiKey: string): Promise<FetchedModel[]> {
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
// OpenAI-Compatible generic fetcher
// ============================================================================

export async function fetchOpenAICompatModels(
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
// Cohere (custom response shape)
// ============================================================================

export async function fetchCohereModels(apiKey: string): Promise<FetchedModel[]> {
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
// GLM / Zhipu (custom response shape)
// ============================================================================

export async function fetchGLMModels(apiKey: string): Promise<FetchedModel[]> {
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
