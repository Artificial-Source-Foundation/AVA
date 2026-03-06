import type { LLMProvider } from '../llm/types.js'

export interface ContextFallbackCandidate {
  provider: LLMProvider
  model: string
  contextWindow: number
}

const MODEL_CONTEXT_LIMITS: Record<string, number> = {
  'claude-haiku': 200_000,
  'claude-sonnet': 200_000,
  'claude-opus': 200_000,
  'gpt-4': 128_000,
  'gpt-5': 400_000,
  'gemini-2.5-pro': 1_000_000,
  'gemini-1.5': 1_000_000,
  kimi: 262_000,
  glm: 200_000,
}

const CONTEXT_FALLBACK_CHAINS: Record<string, ContextFallbackCandidate[]> = {
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

export function getContextLimit(model: string): number {
  for (const [prefix, limit] of Object.entries(MODEL_CONTEXT_LIMITS)) {
    if (model.startsWith(prefix)) return limit
  }
  return 200_000
}

export function getContextFallbackCandidate(
  provider: LLMProvider,
  model: string,
  requiredTokens: number
): ContextFallbackCandidate | null {
  const chain = CONTEXT_FALLBACK_CHAINS[provider]
  if (!chain || chain.length === 0) return null

  const normalizedModel = model.toLowerCase()
  const current = chain.find((entry) => normalizedModel.includes(entry.model.toLowerCase()))
  const currentWindow = current?.contextWindow ?? getContextLimit(model)

  const ordered = [...chain].sort((a, b) => a.contextWindow - b.contextWindow)
  for (const entry of ordered) {
    if (entry.contextWindow <= currentWindow) continue
    if (entry.contextWindow < requiredTokens) continue
    return entry
  }

  return null
}
