/**
 * Model availability tracking and fallback.
 */

import { emitEvent } from '@ava/core-v2/extensions'
import { createLogger } from '@ava/core-v2/logger'

const log = createLogger('ModelAvailability')

export type ModelStatus = 'available' | 'degraded' | 'unavailable'

export interface ModelHealthRecord {
  provider: string
  model: string
  status: ModelStatus
  lastCheck: number
  consecutiveFailures: number
  lastError?: string
  avgLatencyMs?: number
}

const healthRecords = new Map<string, ModelHealthRecord>()

function modelKey(provider: string, model: string): string {
  return `${provider}:${model}`
}

export function recordSuccess(provider: string, model: string, latencyMs: number): void {
  const key = modelKey(provider, model)
  const existing = healthRecords.get(key)
  const avgLatency = existing?.avgLatencyMs
    ? existing.avgLatencyMs * 0.8 + latencyMs * 0.2 // exponential moving average
    : latencyMs

  healthRecords.set(key, {
    provider,
    model,
    status: 'available',
    lastCheck: Date.now(),
    consecutiveFailures: 0,
    avgLatencyMs: avgLatency,
  })
}

export function recordFailure(provider: string, model: string, error: string): void {
  const key = modelKey(provider, model)
  const existing = healthRecords.get(key)
  const failures = (existing?.consecutiveFailures ?? 0) + 1
  const status: ModelStatus =
    failures >= 3 ? 'unavailable' : failures >= 1 ? 'degraded' : 'available'

  healthRecords.set(key, {
    provider,
    model,
    status,
    lastCheck: Date.now(),
    consecutiveFailures: failures,
    lastError: error,
    avgLatencyMs: existing?.avgLatencyMs,
  })

  if (status === 'unavailable') {
    emitEvent('model:unavailable', { provider, model, failures, error })
    log.warn(`Model ${provider}/${model} marked unavailable after ${failures} failures`)
  }
}

export function getModelStatus(provider: string, model: string): ModelHealthRecord | undefined {
  return healthRecords.get(modelKey(provider, model))
}

export function isModelAvailable(provider: string, model: string): boolean {
  const record = healthRecords.get(modelKey(provider, model))
  return !record || record.status !== 'unavailable'
}

// Fallback chain per provider tier
const FALLBACK_CHAINS: Record<string, string[]> = {
  'anthropic:claude-opus': ['claude-sonnet-4-20250514', 'claude-haiku-4-5-20251001'],
  'anthropic:claude-sonnet': ['claude-haiku-4-5-20251001'],
  'openai:gpt-4o': ['gpt-4o-mini'],
  'openrouter:anthropic/claude-opus': ['anthropic/claude-sonnet-4', 'openai/gpt-4o'],
}

export interface ContextFallbackChain {
  provider: string
  model: string
  contextWindow: number
}

/** Ordered by context window size (ascending). */
const CONTEXT_FALLBACK_CHAINS: Record<string, ContextFallbackChain[]> = {
  anthropic: [
    { provider: 'anthropic', model: 'claude-haiku-4-5', contextWindow: 200_000 },
    { provider: 'anthropic', model: 'claude-sonnet-4-6', contextWindow: 200_000 },
    { provider: 'anthropic', model: 'claude-opus-4-6', contextWindow: 200_000 },
  ],
  openrouter: [
    { provider: 'openrouter', model: 'anthropic/claude-sonnet-4-6', contextWindow: 200_000 },
    { provider: 'openrouter', model: 'google/gemini-2.5-pro', contextWindow: 1_000_000 },
  ],
}

function normalizeModelName(model: string): string {
  return model.toLowerCase()
}

function getCurrentContextWindow(provider: string, model: string): number {
  const chain = CONTEXT_FALLBACK_CHAINS[provider]
  if (!chain) return 0
  const normalized = normalizeModelName(model)
  const match = chain.find((entry) => normalized.includes(normalizeModelName(entry.model)))
  return match?.contextWindow ?? 0
}

export function getContextFallback(
  currentProvider: string,
  currentModel: string,
  requiredTokens: number
): ContextFallbackChain | null {
  const chain = CONTEXT_FALLBACK_CHAINS[currentProvider]
  if (!chain || chain.length === 0) return null

  const currentWindow = getCurrentContextWindow(currentProvider, currentModel)
  const ordered = [...chain].sort((a, b) => a.contextWindow - b.contextWindow)

  for (const entry of ordered) {
    if (entry.contextWindow <= currentWindow) continue
    if (entry.contextWindow < requiredTokens) continue
    if (!isModelAvailable(entry.provider, entry.model)) continue
    return entry
  }

  return null
}

export function getFallbackModel(
  provider: string,
  model: string
): { provider: string; model: string } | undefined {
  // Check provider-specific fallback chains
  for (const [key, chain] of Object.entries(FALLBACK_CHAINS)) {
    const [chainProvider, chainPrefix] = key.split(':')
    if (provider === chainProvider && model.startsWith(chainPrefix!)) {
      for (const fallback of chain) {
        if (isModelAvailable(provider, fallback)) {
          log.info(`Falling back from ${model} to ${fallback}`)
          emitEvent('model:fallback', { provider, from: model, to: fallback })
          return { provider, model: fallback }
        }
      }
    }
  }
  return undefined
}

export function getAllModelStatuses(): ModelHealthRecord[] {
  return [...healthRecords.values()]
}

export function resetAvailability(): void {
  healthRecords.clear()
}
