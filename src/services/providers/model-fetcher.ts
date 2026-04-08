/**
 * Dynamic Model Fetcher
 *
 * Fetches available models from provider APIs.
 *
 * - OpenAI-compat (GET /v1/models): OpenAI, Inception, Z.AI, MiniMax
 * - OpenRouter: GET /api/v1/models
 * - Copilot: GET /models
 * - Gemini: GET /v1beta/models
 * - Ollama: GET /api/tags
 * - Anthropic: Documented models (no list API)
 */

import type { ProviderModel } from '../../config/defaults/provider-defaults'
import { type AnyLLMProvider, normalizeProviderId } from '../../types/llm'
import { logWarn } from '../logger'
import { getModelFromCatalog, getModelsDevModels } from './curated-model-catalog'

// Re-export public types from the types module
export type { FetchedModel, FetchModelsOptions } from './model-fetcher-types'

import { COPILOT_DEFAULT_MODELS } from './copilot-defaults'
import {
  fetchCopilotModels,
  fetchGoogleModels,
  fetchOllamaModels,
  fetchOpenAICompatModels,
  fetchOpenAIModels,
  fetchOpenRouterModels,
  getAlibabaModels,
  getAnthropicModels,
  OPENAI_COMPAT_CONFIGS,
} from './model-fetcher-providers'
import type { FetchedModel } from './model-fetcher-types'

// ============================================================================
// Main Export
// ============================================================================

/**
 * Fetch models for a specific provider
 */
export async function fetchModels(
  provider: AnyLLMProvider,
  options: { apiKey?: string; baseUrl?: string } = {}
): Promise<FetchedModel[]> {
  const normalizedProvider = normalizeProviderId(provider) as AnyLLMProvider

  switch (normalizedProvider) {
    case 'openai':
      if (options.apiKey) {
        return enrichWithCatalog('openai', await fetchOpenAIModels(options.apiKey))
      }
      // OAuth users have no API key — use the curated catalog as fallback
      return catalogModelsToFetched(getModelsDevModels('openai'))

    case 'copilot':
      if (options.apiKey) {
        try {
          return await fetchCopilotModels(options.apiKey)
        } catch {
          logWarn('models', 'Could not fetch Copilot models, using defaults')
        }
      }
      return COPILOT_DEFAULT_MODELS

    case 'openrouter':
      return fetchOpenRouterModels(options.apiKey)

    case 'anthropic': {
      // No list API — use the curated catalog first, then fall back to hardcoded defaults
      const catalogModels = getModelsDevModels('anthropic')
      return catalogModels.length > 0 ? catalogModelsToFetched(catalogModels) : getAnthropicModels()
    }

    case 'gemini':
      if (options.apiKey) {
        try {
          return await fetchGoogleModels(options.apiKey)
        } catch {
          logWarn('models', 'Could not fetch Google models, using defaults')
        }
      }
      {
        const catalogFallback = catalogModelsToFetched(getModelsDevModels('gemini'))
        return catalogFallback.length > 0 ? catalogFallback : []
      }

    case 'inception': {
      const config = OPENAI_COMPAT_CONFIGS.inception
      if (options.apiKey && config) {
        try {
          const models = await fetchOpenAICompatModels(options.apiKey, config)
          return enrichWithCatalog('inception', models)
        } catch {
          logWarn('models', `Could not fetch ${config.providerName} models, trying catalog`)
        }
      }
      return catalogModelsToFetched(getModelsDevModels('inception'))
    }

    case 'zai': {
      const config = OPENAI_COMPAT_CONFIGS.zai
      if (options.apiKey && config) {
        try {
          const models = await fetchOpenAICompatModels(options.apiKey, config)
          return enrichWithCatalog('zai', models)
        } catch {
          logWarn('models', `Could not fetch ${config.providerName} models, trying catalog`)
        }
      }
      return catalogModelsToFetched(getModelsDevModels('zai'))
    }

    case 'minimax': {
      const config = OPENAI_COMPAT_CONFIGS.minimax
      if (options.apiKey && config) {
        try {
          const models = await fetchOpenAICompatModels(options.apiKey, config)
          return enrichWithCatalog('minimax', models)
        } catch {
          logWarn('models', `Could not fetch ${config.providerName} models, trying catalog`)
        }
      }
      return catalogModelsToFetched(getModelsDevModels('minimax'))
    }

    case 'kimi': {
      const config = OPENAI_COMPAT_CONFIGS[normalizedProvider]
      if (options.apiKey && config) {
        try {
          const models = await fetchOpenAICompatModels(options.apiKey, config)
          return enrichWithCatalog(normalizedProvider, models)
        } catch {
          logWarn('models', `Could not fetch ${config.providerName} models, trying catalog`)
        }
      }
      // Try the curated catalog before falling back to empty
      return catalogModelsToFetched(getModelsDevModels(normalizedProvider))
    }

    case 'xai':
    case 'mistral':
    case 'groq':
    case 'deepseek':
    case 'together':
    case 'cohere':
    case 'glm':
    case 'azure':
    case 'bedrock':
    case 'mock':
      return []

    case 'alibaba':
      // Alibaba coding plan uses Anthropic-compatible API — no model list endpoint.
      // Return documented models from the coding plan.
      return getAlibabaModels()

    case 'ollama':
      try {
        return await fetchOllamaModels(options.baseUrl)
      } catch {
        logWarn('models', 'Could not fetch Ollama models, using defaults')
        return []
      }

    default:
      return []
  }
}

// ============================================================================
// Catalog Helpers
// ============================================================================

/** Convert curated catalog ProviderModel[] → FetchedModel[] */
function catalogModelsToFetched(models: ProviderModel[]): FetchedModel[] {
  return models.map((m) => ({
    id: m.id,
    name: m.name,
    contextWindow: m.contextWindow,
    ...(m.pricing && {
      pricing: { prompt: m.pricing.input ?? 0, completion: m.pricing.output ?? 0 },
    }),
    ...(m.capabilities?.length && { capabilities: m.capabilities }),
  }))
}

// ============================================================================
// Catalog Enrichment
// ============================================================================

/**
 * Enrich fetched models with metadata from the curated model catalog.
 * Fills gaps (contextWindow=4096, missing pricing/capabilities) without
 * overriding values the provider API already provides.
 */
export function enrichWithCatalog(
  provider: AnyLLMProvider,
  fetched: FetchedModel[]
): FetchedModel[] {
  return fetched.map((model) => {
    const catalogEntry = getModelFromCatalog(model.id, provider)
    if (!catalogEntry) return model

    const enriched = { ...model }

    // Fill context window if it's the default placeholder (4096)
    if (enriched.contextWindow <= 4096 && catalogEntry.limit?.context) {
      enriched.contextWindow = catalogEntry.limit.context
    }

    // Fill pricing if missing
    if (!enriched.pricing && catalogEntry.cost?.input !== undefined) {
      enriched.pricing = {
        prompt: catalogEntry.cost.input,
        completion: catalogEntry.cost.output ?? 0,
      }
    }

    // Merge capabilities from catalog (add missing ones, don't remove existing)
    {
      const existing = new Set(enriched.capabilities ?? [])
      if (catalogEntry.tool_call && !existing.has('tools')) existing.add('tools')
      if (catalogEntry.reasoning && !existing.has('reasoning')) existing.add('reasoning')
      if (
        (catalogEntry.attachment || catalogEntry.modalities?.input?.includes('image')) &&
        !existing.has('vision')
      ) {
        existing.add('vision')
      }
      enriched.capabilities = [...existing]
    }

    return enriched
  })
}
