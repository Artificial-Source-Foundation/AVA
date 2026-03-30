/**
 * Model Browser Helpers
 *
 * Aggregation, filtering, sorting, and formatting for the model browser.
 * Pricing and capabilities now come primarily from models.dev catalog
 * (src/services/providers/models-dev-catalog.ts). Hardcoded maps are kept
 * as offline fallbacks for when the catalog hasn't loaded.
 */

import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import {
  getModelFromCatalog,
  isBlockedModelId,
} from '../../../services/providers/models-dev-catalog'
import type {
  BrowsableModel,
  FilterState,
  ModelCapability,
  ModelPricing,
  SortOption,
} from './model-browser-types'

export function buildModelSpec(modelId: string, providerId?: string | null): string {
  return providerId ? `${providerId}/${modelId}` : modelId
}

export function matchesModelSelection(
  value: string | null | undefined,
  modelId: string,
  providerId: string
): boolean {
  if (!value) return false
  if (value === modelId) return true
  return value === buildModelSpec(modelId, providerId)
}

export function findSelectedModel(
  models: BrowsableModel[],
  value: string | null | undefined,
  providerId?: string | null
): BrowsableModel | undefined {
  return (
    models.find(
      (model) =>
        matchesModelSelection(value, model.id, model.providerId) &&
        (!providerId || model.providerId === providerId)
    ) ?? models.find((model) => matchesModelSelection(value, model.id, model.providerId))
  )
}

export function formatModelSelectionLabel(
  models: BrowsableModel[],
  value: string | null | undefined,
  options?: {
    autoLabel?: string
    includeProvider?: boolean
    providerId?: string | null
  }
): string {
  const selected = findSelectedModel(models, value, options?.providerId)
  if (!selected) return options?.autoLabel ?? 'Auto'
  return options?.includeProvider === false
    ? selected.name
    : `${selected.providerName} - ${selected.name}`
}

// ============================================================================
// Catalog-Backed Lookups (with hardcoded fallbacks)
// ============================================================================

/**
 * Look up pricing for a model. Tries models.dev catalog first, then hardcoded fallback.
 */
function lookupPricing(modelId: string): ModelPricing | undefined {
  const entry = getModelFromCatalog(modelId)
  if (entry?.cost?.input !== undefined && entry?.cost?.output !== undefined) {
    return { input: entry.cost.input, output: entry.cost.output }
  }
  return FALLBACK_PRICING[modelId]
}

/**
 * Check if a model has reasoning capability. Catalog first, then fallback set.
 */
function hasReasoning(modelId: string): boolean {
  const entry = getModelFromCatalog(modelId)
  if (entry) return entry.reasoning === true
  return FALLBACK_REASONING.has(modelId)
}

/**
 * Check if a model has vision capability. Catalog first, then fallback set.
 */
function hasVision(modelId: string): boolean {
  const entry = getModelFromCatalog(modelId)
  if (entry)
    return entry.attachment === true || (entry.modalities?.input?.includes('image') ?? false)
  return FALLBACK_VISION.has(modelId)
}

// ============================================================================
// Fallback Maps (offline / pre-catalog-load)
// ============================================================================

const FALLBACK_PRICING: Record<string, ModelPricing> = {
  // Anthropic
  'claude-opus-4-6': { input: 5, output: 25 },
  'claude-sonnet-4-6': { input: 3, output: 15 },
  'claude-haiku-4-5-20251001': { input: 1, output: 5 },
  // OpenAI
  'gpt-5.2': { input: 1.75, output: 14 },
  'gpt-5-mini': { input: 0.25, output: 2 },
  'gpt-4.1': { input: 2, output: 8 },
  o3: { input: 2, output: 8 },
  'o4-mini': { input: 1.1, output: 4.4 },
  // Google
  'gemini-2.5-pro': { input: 1.25, output: 10 },
  'gemini-2.5-flash': { input: 0.3, output: 2.5 },
  // xAI
  'grok-4-1-fast-reasoning': { input: 0.2, output: 0.5 },
  // Mistral
  'mistral-large-latest': { input: 0.5, output: 1.5 },
  'devstral-latest': { input: 0.4, output: 2 },
  // DeepSeek
  'deepseek-chat': { input: 0.28, output: 0.42 },
  'deepseek-reasoner': { input: 0.28, output: 0.42 },
  // Cohere
  'command-a': { input: 2.5, output: 10 },
}

const FALLBACK_REASONING_EXACT = new Set([
  'o3',
  'o3-pro',
  'o4-mini',
  'o3-mini',
  'claude-opus-4-6',
  'claude-sonnet-4-6',
  'claude-haiku-4-5-20251001',
  'gemini-2.5-pro',
  'gemini-2.5-flash',
  'grok-4-1-fast-reasoning',
  'grok-code-fast-1',
  'deepseek-reasoner',
  'magistral-medium-latest',
  'magistral-small-latest',
])

/** Prefix patterns — any model starting with these supports reasoning */
const FALLBACK_REASONING_PREFIXES = [
  'gpt-5', // covers gpt-5, gpt-5.1, gpt-5.2, gpt-5.3, gpt-5.4, gpt-5.x-codex, etc.
  'o3', // covers o3, o3-pro, o3-mini
  'o4', // covers o4-mini
  'claude-opus',
  'claude-sonnet',
]

const FALLBACK_REASONING = {
  has(id: string): boolean {
    if (FALLBACK_REASONING_EXACT.has(id)) return true
    const lower = id.toLowerCase()
    return FALLBACK_REASONING_PREFIXES.some((prefix) => lower.startsWith(prefix))
  },
}

const FALLBACK_VISION = new Set([
  'gpt-5.2',
  'gpt-5.1',
  'gpt-5',
  'gpt-5-mini',
  'gpt-4.1',
  'gpt-4.1-mini',
  'o3',
  'o4-mini',
  'gpt-4o',
  'claude-opus-4-6',
  'claude-sonnet-4-6',
  'claude-haiku-4-5-20251001',
  'gemini-2.5-pro',
  'gemini-2.5-flash',
  'grok-4-1-fast-reasoning',
  'grok-4-1-fast-non-reasoning',
  'mistral-large-latest',
  'mistral-medium-latest',
  'command-a-vision',
])

const FREE_MODELS = new Set([
  'gemma2-9b-it',
  'llama-3.3-70b-versatile',
  'llama-3.1-8b-instant',
  'mixtral-8x7b-32768',
  'meta-llama/llama-4-maverick-17b-128e-instruct',
  'meta-llama/llama-4-scout-17b-16e-instruct',
  'qwen/qwen3-32b',
  'moonshotai/kimi-k2-instruct-0905',
])

// ============================================================================
// Aggregation
// ============================================================================

export function aggregateModels(providers: LLMProviderConfig[]): BrowsableModel[] {
  const models: BrowsableModel[] = []
  for (const provider of providers) {
    for (const model of provider.models) {
      // Skip known-outdated models that may have been added from stale API data
      if (isBlockedModelId(model.id)) continue
      models.push({
        id: model.id,
        name: model.name,
        providerId: provider.id,
        providerName: provider.name,
        contextWindow: model.contextWindow,
        isDefault: model.isDefault,
        pricing: model.pricing ?? lookupPricing(model.id),
        capabilities: mergeCapabilities(model.id, provider.id, model.capabilities),
      })
    }
  }
  return models
}

// ============================================================================
// Capability Merging
// ============================================================================

/**
 * Merge stored capabilities (from provider files or API) with inferred fallbacks.
 * Stored caps take priority; inference fills in gaps for dynamically-fetched models.
 */
function mergeCapabilities(
  modelId: string,
  providerId: string,
  stored?: string[]
): ModelCapability[] {
  if (stored?.length) {
    // Stored capabilities are authoritative — normalize to our ModelCapability type
    const caps = new Set<ModelCapability>()
    for (const c of stored) {
      if (
        c === 'reasoning' ||
        c === 'tools' ||
        c === 'vision' ||
        c === 'free' ||
        c === 'thinking'
      ) {
        caps.add(c)
      } else if (c.includes('reason')) {
        caps.add('reasoning')
      } else if (c.includes('vision') || c.includes('image')) {
        caps.add('vision')
      } else if (c.includes('tool') || c.includes('function')) {
        caps.add('tools')
      }
    }
    // Also apply provider-level free inference
    if (providerId === 'groq' || providerId === 'ollama') caps.add('free')
    if (FREE_MODELS.has(modelId)) caps.add('free')
    return [...caps]
  }

  // No stored caps — fall back to inference
  return inferCapabilities(modelId, providerId)
}

/** Infer capabilities from catalog lookups, then fallback sets */
export function inferCapabilities(modelId: string, providerId: string): ModelCapability[] {
  const caps: ModelCapability[] = []

  caps.push('tools')

  if (hasReasoning(modelId)) caps.push('reasoning')
  if (hasVision(modelId)) caps.push('vision')

  if (FREE_MODELS.has(modelId) || providerId === 'groq' || providerId === 'ollama') {
    caps.push('free')
  }

  return caps
}

// ============================================================================
// Filtering
// ============================================================================

export function filterModels(models: BrowsableModel[], filters: FilterState): BrowsableModel[] {
  let result = models

  // Search
  if (filters.search) {
    const q = filters.search.toLowerCase()
    result = result.filter(
      (m) =>
        m.name.toLowerCase().includes(q) ||
        m.id.toLowerCase().includes(q) ||
        m.providerName.toLowerCase().includes(q)
    )
  }

  // Provider filter
  if (filters.provider) {
    result = result.filter((m) => m.providerId === filters.provider)
  }

  // Capability filters
  if (filters.capabilities.length > 0) {
    result = result.filter((m) => filters.capabilities.every((cap) => m.capabilities.includes(cap)))
  }

  return result
}

// ============================================================================
// Sorting
// ============================================================================

export function sortModels(models: BrowsableModel[], sort: SortOption): BrowsableModel[] {
  const sorted = [...models]
  switch (sort) {
    case 'name':
      sorted.sort((a, b) => a.name.localeCompare(b.name))
      break
    case 'context':
      sorted.sort((a, b) => b.contextWindow - a.contextWindow)
      break
    case 'price':
      sorted.sort((a, b) => (a.pricing?.input ?? 999) - (b.pricing?.input ?? 999))
      break
  }
  return sorted
}

// ============================================================================
// Formatting
// ============================================================================

export function formatContextWindow(tokens: number): string {
  if (tokens >= 1000000) return `${(tokens / 1000000).toFixed(1)}M`
  if (tokens >= 1000) return `${Math.round(tokens / 1000)}K`
  return tokens.toString()
}

export function formatPricing(pricing: ModelPricing | undefined): string {
  if (!pricing) return ''
  if (pricing.input === 0 && pricing.output === 0) return 'Free'
  if (pricing.input !== undefined) return `$${pricing.input}/M in`
  return ''
}

export function formatPricingFull(pricing: ModelPricing | undefined): string {
  if (!pricing) return 'Pricing unavailable'
  if (pricing.input === 0 && pricing.output === 0) return 'Free'
  const parts: string[] = []
  if (pricing.input !== undefined) parts.push(`$${pricing.input}/M input`)
  if (pricing.output !== undefined) parts.push(`$${pricing.output}/M output`)
  return parts.join(' · ')
}
