/**
 * Models.dev Catalog Integration
 *
 * Fetches rich model metadata (pricing, context windows, capabilities) from
 * https://models.dev/api.json — a community-maintained model registry.
 *
 * Cache: localStorage with 1-hour TTL + in-memory for same-session reuse.
 * Follows the cache pattern from src/stores/plugins-catalog.ts.
 */

import type { ProviderModel } from '../../config/defaults/provider-defaults'
import type { LLMProvider } from '../../types/llm'
import { logWarn } from '../logger'

// ============================================================================
// Types (matching models.dev schema)
// ============================================================================

export interface ModelsDevModel {
  id: string
  name: string
  family?: string
  attachment?: boolean
  reasoning?: boolean
  tool_call?: boolean
  structured_output?: boolean
  temperature?: boolean
  knowledge?: string
  release_date?: string
  last_updated?: string
  open_weights?: boolean
  status?: string
  modalities?: {
    input?: string[]
    output?: string[]
  }
  cost?: {
    input?: number
    output?: number
    cache_read?: number
    cache_write?: number
  }
  limit?: {
    context?: number
    output?: number
  }
}

export interface ModelsDevProvider {
  id: string
  name: string
  env?: string[]
  npm?: string
  api?: string
  doc?: string
  models: Record<string, ModelsDevModel>
}

type ModelsDevCatalog = Record<string, ModelsDevProvider>

// ============================================================================
// Constants
// ============================================================================

const CATALOG_URL = 'https://models.dev/api.json'
const CACHE_KEY = 'ava:models-dev-catalog'
const CACHE_TS_KEY = 'ava:models-dev-catalog-ts'
const CACHE_TTL_MS = 60 * 60 * 1000 // 1 hour

/** Maps AVA provider IDs → models.dev provider keys */
const PROVIDER_ID_MAP: Partial<Record<LLMProvider, string>> = {
  anthropic: 'anthropic',
  openai: 'openai',
  google: 'google',
  xai: 'xai',
  mistral: 'mistral',
  groq: 'groq',
  deepseek: 'deepseek',
  together: 'togetherai',
  kimi: 'moonshotai',
  cohere: 'cohere',
  glm: 'zhipuai',
  copilot: 'github-copilot',
  openrouter: 'openrouter',
}

// ============================================================================
// Cache
// ============================================================================

let memoryCatalog: ModelsDevCatalog | null = null

function isCacheValid(): boolean {
  if (typeof localStorage === 'undefined') return false
  const ts = localStorage.getItem(CACHE_TS_KEY)
  if (!ts) return false
  return Date.now() - Number(ts) < CACHE_TTL_MS
}

function loadFromCache(): ModelsDevCatalog | null {
  if (typeof localStorage === 'undefined') return null
  const raw = localStorage.getItem(CACHE_KEY)
  if (!raw) return null
  try {
    return JSON.parse(raw) as ModelsDevCatalog
  } catch {
    return null
  }
}

function saveToCache(catalog: ModelsDevCatalog): void {
  if (typeof localStorage === 'undefined') return
  try {
    localStorage.setItem(CACHE_KEY, JSON.stringify(catalog))
    localStorage.setItem(CACHE_TS_KEY, String(Date.now()))
  } catch {
    // localStorage full — non-critical, skip silently
  }
}

// ============================================================================
// Sync
// ============================================================================

/**
 * Fetch the models.dev catalog and cache it.
 * Safe to call on startup — returns cached data on network failure.
 */
export async function syncModelsCatalog(): Promise<ModelsDevCatalog | null> {
  // Fast path: memory cache
  if (memoryCatalog && isCacheValid()) return memoryCatalog

  // Check localStorage cache
  if (isCacheValid()) {
    const cached = loadFromCache()
    if (cached) {
      memoryCatalog = cached
      return cached
    }
  }

  // Fetch remote
  try {
    const response = await fetch(CATALOG_URL)
    if (!response.ok) throw new Error(`HTTP ${response.status}`)
    const data = (await response.json()) as ModelsDevCatalog
    saveToCache(data)
    memoryCatalog = data
    return data
  } catch {
    logWarn('models-dev', 'Could not fetch models.dev catalog, using cache')
    const cached = loadFromCache()
    if (cached) {
      memoryCatalog = cached
      return cached
    }
    return null
  }
}

// ============================================================================
// Query
// ============================================================================

/** Known-outdated model ID patterns that should never appear in the browser */
const BLOCKED_MODEL_PATTERNS = [
  'aurora', // xAI Aurora Alpha — experimental, never GA
  'codestral-2501', // Mistral legacy preview
]

/** Non-coding model patterns to filter out */
function isNonCodingModel(model: ModelsDevModel): boolean {
  const id = model.id.toLowerCase()
  // Filter embeddings, TTS, image-gen, transcription, moderation
  if (id.includes('embed') || id.includes('tts') || id.includes('whisper')) return true
  if (id.includes('dall') || id.includes('imagen') || id.includes('sora')) return true
  if (id.includes('moderation') || id.includes('realtime')) return true
  // Filter by modalities — must support text output
  if (model.modalities?.output && !model.modalities.output.includes('text')) return true
  // Filter deprecated and pre-release models
  if (model.status === 'deprecated' || model.status === 'alpha' || model.status === 'preview')
    return true
  // Filter known-outdated models by ID pattern
  if (BLOCKED_MODEL_PATTERNS.some((pattern) => id.includes(pattern))) return true
  // Must support tool calling for coding use
  if (model.tool_call === false) return true
  return false
}

/** Transform a models.dev model → AVA ProviderModel */
function transformModel(model: ModelsDevModel): ProviderModel {
  const capabilities: string[] = []
  if (model.tool_call) capabilities.push('tools')
  if (model.reasoning) capabilities.push('reasoning')
  if (model.attachment || model.modalities?.input?.includes('image')) capabilities.push('vision')

  const pricing =
    model.cost?.input !== undefined && model.cost?.output !== undefined
      ? { input: model.cost.input, output: model.cost.output }
      : undefined

  return {
    id: model.id,
    name: model.name || model.id,
    contextWindow: model.limit?.context ?? 4096,
    ...(pricing && { pricing }),
    ...(capabilities.length > 0 && { capabilities }),
  }
}

/**
 * Get transformed ProviderModel[] for an AVA provider ID from the catalog.
 * Returns [] if catalog unavailable or provider not found.
 */
export function getModelsDevModels(avaProviderId: LLMProvider): ProviderModel[] {
  if (!memoryCatalog) return []

  const catalogKey = PROVIDER_ID_MAP[avaProviderId]
  if (!catalogKey) return []

  const provider = memoryCatalog[catalogKey]
  if (!provider?.models) return []

  return Object.values(provider.models)
    .filter((m) => !isNonCodingModel(m))
    .map(transformModel)
}

/**
 * Look up a single model's metadata from the catalog.
 * Searches within the specified provider, or across all providers if not specified.
 */
export function getModelFromCatalog(
  modelId: string,
  avaProviderId?: LLMProvider
): ModelsDevModel | null {
  if (!memoryCatalog) return null

  if (avaProviderId) {
    const catalogKey = PROVIDER_ID_MAP[avaProviderId]
    if (!catalogKey) return null
    return memoryCatalog[catalogKey]?.models?.[modelId] ?? null
  }

  // Search all providers
  for (const provider of Object.values(memoryCatalog)) {
    if (provider.models?.[modelId]) return provider.models[modelId]
  }
  return null
}

/**
 * Check if a model ID matches the blocklist of known-outdated models.
 * Useful for filtering models from provider configs that bypass the catalog.
 */
export function isBlockedModelId(modelId: string): boolean {
  const id = modelId.toLowerCase()
  return BLOCKED_MODEL_PATTERNS.some((pattern) => id.includes(pattern))
}

/** Reset memory cache (for testing) */
export function _resetCatalogCache(): void {
  memoryCatalog = null
}
