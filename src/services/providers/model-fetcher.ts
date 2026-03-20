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

import type { ProviderModel } from '../../config/defaults/provider-defaults'
import type { LLMProvider } from '../../types/llm'
import { logWarn } from '../logger'
import { getModelFromCatalog, getModelsDevModels } from './models-dev-catalog'

// Re-export public types from the types module
export type { FetchedModel, FetchModelsOptions } from './model-fetcher-types'

import { COPILOT_DEFAULT_MODELS } from './copilot-defaults'
import {
  fetchCohereModels,
  fetchCopilotModels,
  fetchGLMModels,
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
  provider: LLMProvider,
  options: { apiKey?: string; baseUrl?: string } = {}
): Promise<FetchedModel[]> {
  switch (provider) {
    case 'openai':
      if (options.apiKey) {
        return enrichWithCatalog('openai', await fetchOpenAIModels(options.apiKey))
      }
      // OAuth users have no API key — use models.dev catalog as fallback
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
      // No list API — try models.dev first, fall back to hardcoded
      const catalogModels = getModelsDevModels('anthropic')
      return catalogModels.length > 0 ? catalogModelsToFetched(catalogModels) : getAnthropicModels()
    }

    case 'google':
      if (options.apiKey) {
        try {
          return await fetchGoogleModels(options.apiKey)
        } catch {
          logWarn('models', 'Could not fetch Google models, using defaults')
        }
      }
      {
        const catalogFallback = catalogModelsToFetched(getModelsDevModels('google'))
        return catalogFallback.length > 0 ? catalogFallback : []
      }

    case 'xai':
    case 'mistral':
    case 'groq':
    case 'deepseek':
    case 'together':
    case 'kimi': {
      const config = OPENAI_COMPAT_CONFIGS[provider]
      if (options.apiKey && config) {
        try {
          const models = await fetchOpenAICompatModels(options.apiKey, config)
          return enrichWithCatalog(provider, models)
        } catch {
          logWarn('models', `Could not fetch ${config.providerName} models, trying catalog`)
        }
      }
      // Try models.dev catalog before falling back to empty
      return catalogModelsToFetched(getModelsDevModels(provider))
    }

    case 'cohere':
      if (options.apiKey) {
        try {
          return await fetchCohereModels(options.apiKey)
        } catch {
          logWarn('models', 'Could not fetch Cohere models, trying catalog')
        }
      }
      return catalogModelsToFetched(getModelsDevModels('cohere'))

    case 'glm':
      if (options.apiKey) {
        try {
          return await fetchGLMModels(options.apiKey)
        } catch {
          logWarn('models', 'Could not fetch GLM models, trying catalog')
        }
      }
      return catalogModelsToFetched(getModelsDevModels('glm'))

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
  }
}

// ============================================================================
// Catalog Helpers
// ============================================================================

/** Convert ProviderModel[] from models.dev → FetchedModel[] */
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
 * Enrich fetched models with metadata from models.dev catalog.
 * Fills gaps (contextWindow=4096, missing pricing/capabilities) without
 * overriding values the provider API already provides.
 */
export function enrichWithCatalog(provider: LLMProvider, fetched: FetchedModel[]): FetchedModel[] {
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
