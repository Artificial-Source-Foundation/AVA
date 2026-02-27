/**
 * Model Browser Helpers
 *
 * Aggregation, filtering, sorting, and formatting for the model browser.
 * Pricing and capabilities now come primarily from per-provider files
 * (src/config/defaults/providers/*.ts). The KNOWN_PRICING map here is
 * a fallback for dynamically-fetched models that don't carry pricing.
 */

import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import type {
  BrowsableModel,
  FilterState,
  ModelCapability,
  ModelPricing,
  SortOption,
} from './model-browser-types'

// ============================================================================
// Fallback Pricing (per 1M tokens) — for dynamically-fetched models only
// ============================================================================

const KNOWN_PRICING: Record<string, ModelPricing> = {
  // Anthropic
  'claude-opus-4-6': { input: 5, output: 25 },
  'claude-sonnet-4-6': { input: 3, output: 15 },
  'claude-haiku-4-5-20251001': { input: 1, output: 5 },
  'claude-sonnet-4-5-20250929': { input: 3, output: 15 },
  'claude-opus-4-5-20251101': { input: 5, output: 25 },
  'claude-opus-4-1-20250805': { input: 15, output: 75 },
  'claude-opus-4-20250514': { input: 15, output: 75 },
  'claude-sonnet-4-20250514': { input: 3, output: 15 },
  // OpenAI
  'gpt-5.2': { input: 1.75, output: 14 },
  'gpt-5.1': { input: 1.25, output: 10 },
  'gpt-5': { input: 1.25, output: 10 },
  'gpt-5-mini': { input: 0.25, output: 2 },
  'gpt-5-nano': { input: 0.05, output: 0.4 },
  'gpt-5.3-codex': { input: 1.75, output: 14 },
  'gpt-5.2-codex': { input: 1.75, output: 14 },
  'gpt-5.1-codex': { input: 1.25, output: 10 },
  'gpt-5.1-codex-mini': { input: 0.25, output: 2 },
  'codex-mini-latest': { input: 1.5, output: 6 },
  'gpt-4.1': { input: 2, output: 8 },
  'gpt-4.1-mini': { input: 0.4, output: 1.6 },
  'gpt-4.1-nano': { input: 0.1, output: 0.4 },
  o3: { input: 2, output: 8 },
  'o4-mini': { input: 1.1, output: 4.4 },
  'o3-mini': { input: 1.1, output: 4.4 },
  'gpt-4o': { input: 2.5, output: 10 },
  'gpt-4o-mini': { input: 0.15, output: 0.6 },
  // Google
  'gemini-3.1-pro-preview': { input: 2, output: 12 },
  'gemini-3-flash-preview': { input: 0.5, output: 3 },
  'gemini-2.5-pro': { input: 1.25, output: 10 },
  'gemini-2.5-flash': { input: 0.3, output: 2.5 },
  'gemini-2.5-flash-lite': { input: 0.1, output: 0.4 },
  // xAI
  'grok-4-1-fast-reasoning': { input: 0.2, output: 0.5 },
  'grok-4-1-fast-non-reasoning': { input: 0.2, output: 0.5 },
  'grok-4-0709': { input: 3, output: 15 },
  'grok-code-fast-1': { input: 0.2, output: 1.5 },
  'grok-3': { input: 3, output: 15 },
  'grok-3-mini': { input: 0.3, output: 0.5 },
  // Mistral
  'mistral-large-latest': { input: 0.5, output: 1.5 },
  'mistral-medium-latest': { input: 0.4, output: 2 },
  'mistral-small-latest': { input: 0.1, output: 0.3 },
  'magistral-medium-latest': { input: 2, output: 5 },
  'magistral-small-latest': { input: 0.5, output: 1.5 },
  'devstral-latest': { input: 0.4, output: 2 },
  'codestral-latest': { input: 0.3, output: 0.9 },
  // DeepSeek
  'deepseek-chat': { input: 0.28, output: 0.42 },
  'deepseek-reasoner': { input: 0.28, output: 0.42 },
  // Cohere
  'command-a': { input: 2.5, output: 10 },
  'command-a-vision': { input: 2.5, output: 10 },
  'command-r-plus': { input: 2.5, output: 10 },
  'command-r': { input: 0.15, output: 0.6 },
  'command-r7b-12-2024': { input: 0.0375, output: 0.15 },
}

// Fallback sets — used by inferCapabilities() for models without stored caps
const REASONING_MODELS = new Set([
  // OpenAI
  'o3',
  'o3-pro',
  'o4-mini',
  'o3-mini',
  'gpt-5.2',
  'gpt-5.1',
  'gpt-5',
  'gpt-5-mini',
  'gpt-5-nano',
  'gpt-5.3-codex',
  'gpt-5.2-codex',
  'gpt-5.1-codex',
  'codex-mini-latest',
  // Anthropic
  'claude-opus-4-6',
  'claude-sonnet-4-6',
  'claude-sonnet-4-5-20250929',
  'claude-opus-4-5-20251101',
  'claude-opus-4-1-20250805',
  'claude-opus-4-20250514',
  'claude-sonnet-4-20250514',
  'claude-haiku-4-5-20251001',
  // Google
  'gemini-3.1-pro-preview',
  'gemini-3-flash-preview',
  'gemini-2.5-pro',
  'gemini-2.5-flash',
  'gemini-2.5-flash-lite',
  // xAI
  'grok-4-1-fast-reasoning',
  'grok-4-0709',
  'grok-code-fast-1',
  'grok-3-mini',
  // Others
  'deepseek-reasoner',
  'magistral-medium-latest',
  'magistral-small-latest',
  'qwen/qwen3-32b',
])

const VISION_MODELS = new Set([
  // OpenAI
  'gpt-5.2',
  'gpt-5.1',
  'gpt-5',
  'gpt-5-mini',
  'gpt-5-nano',
  'gpt-5.3-codex',
  'gpt-5.2-codex',
  'gpt-5.1-codex',
  'gpt-4.1',
  'gpt-4.1-mini',
  'gpt-4.1-nano',
  'o3',
  'o4-mini',
  'gpt-4o',
  'gpt-4o-mini',
  // Anthropic
  'claude-opus-4-6',
  'claude-sonnet-4-6',
  'claude-sonnet-4-5-20250929',
  'claude-opus-4-5-20251101',
  'claude-haiku-4-5-20251001',
  'claude-sonnet-4-20250514',
  'claude-opus-4-20250514',
  'claude-opus-4-1-20250805',
  // Google
  'gemini-3.1-pro-preview',
  'gemini-3-flash-preview',
  'gemini-2.5-pro',
  'gemini-2.5-flash',
  'gemini-2.5-flash-lite',
  // xAI
  'grok-4-1-fast-reasoning',
  'grok-4-1-fast-non-reasoning',
  'grok-4-0709',
  // Others
  'mistral-large-latest',
  'mistral-medium-latest',
  'mistral-small-latest',
  'magistral-medium-latest',
  'magistral-small-latest',
  'command-a-vision',
  'meta-llama/llama-4-maverick-17b-128e-instruct',
  'meta-llama/llama-4-scout-17b-16e-instruct',
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
      models.push({
        id: model.id,
        name: model.name,
        providerId: provider.id,
        providerName: provider.name,
        contextWindow: model.contextWindow,
        isDefault: model.isDefault,
        pricing: model.pricing ?? KNOWN_PRICING[model.id],
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
      if (c === 'reasoning' || c === 'tools' || c === 'vision' || c === 'free') {
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

/** Infer capabilities from model ID patterns and hardcoded sets */
export function inferCapabilities(modelId: string, providerId: string): ModelCapability[] {
  const caps: ModelCapability[] = []

  caps.push('tools')

  if (REASONING_MODELS.has(modelId)) caps.push('reasoning')
  if (VISION_MODELS.has(modelId)) caps.push('vision')

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
