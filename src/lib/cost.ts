/**
 * Cost Utilities
 * Pure functions for model pricing and cost formatting.
 * Replaces estimateCost/formatCost from @ava/core.
 */

// Per-1K token pricing for common models
const MODEL_PRICING: Record<string, { inputPer1k: number; outputPer1k: number }> = {
  'claude-opus-4': { inputPer1k: 0.015, outputPer1k: 0.075 },
  'claude-sonnet-4': { inputPer1k: 0.003, outputPer1k: 0.015 },
  'claude-haiku-4': { inputPer1k: 0.0008, outputPer1k: 0.004 },
  'claude-3-opus': { inputPer1k: 0.015, outputPer1k: 0.075 },
  'claude-3-sonnet': { inputPer1k: 0.003, outputPer1k: 0.015 },
  'claude-3-haiku': { inputPer1k: 0.00025, outputPer1k: 0.00125 },
  'gpt-4o': { inputPer1k: 0.005, outputPer1k: 0.015 },
  'gpt-4-turbo': { inputPer1k: 0.01, outputPer1k: 0.03 },
  'gpt-4': { inputPer1k: 0.03, outputPer1k: 0.06 },
  'gpt-3.5-turbo': { inputPer1k: 0.0005, outputPer1k: 0.0015 },
  'gemini-pro': { inputPer1k: 0.00025, outputPer1k: 0.0005 },
  'gemini-1.5-pro': { inputPer1k: 0.00125, outputPer1k: 0.005 },
  'deepseek-chat': { inputPer1k: 0.00014, outputPer1k: 0.00028 },

  // OpenAI GPT-5.x family
  'gpt-5.2': { inputPer1k: 0.005, outputPer1k: 0.02 },
  'gpt-5.1': { inputPer1k: 0.004, outputPer1k: 0.016 },
  'gpt-5': { inputPer1k: 0.005, outputPer1k: 0.02 },
  'gpt-5-mini': { inputPer1k: 0.001, outputPer1k: 0.004 },
  'gpt-4.1': { inputPer1k: 0.002, outputPer1k: 0.008 },
  o3: { inputPer1k: 0.01, outputPer1k: 0.04 },
  'o4-mini': { inputPer1k: 0.001, outputPer1k: 0.004 },

  // Google Gemini 2.5+
  'gemini-2.5-pro': { inputPer1k: 0.00125, outputPer1k: 0.01 },
  'gemini-2.5-flash': { inputPer1k: 0.00015, outputPer1k: 0.0006 },
  'gemini-3': { inputPer1k: 0.002, outputPer1k: 0.008 },

  // xAI Grok
  'grok-4': { inputPer1k: 0.003, outputPer1k: 0.015 },
  'grok-3': { inputPer1k: 0.003, outputPer1k: 0.015 },

  // Mistral
  'mistral-large': { inputPer1k: 0.002, outputPer1k: 0.006 },
  'mistral-small': { inputPer1k: 0.0002, outputPer1k: 0.0006 },

  // DeepSeek
  'deepseek-reasoner': { inputPer1k: 0.00055, outputPer1k: 0.002 },

  // Cohere
  'command-a': { inputPer1k: 0.0025, outputPer1k: 0.01 },
  'command-r': { inputPer1k: 0.00015, outputPer1k: 0.0006 },
}

/** Estimate cost for a given model and token usage. Returns null if model pricing unknown. */
export function estimateCost(
  model: string,
  inputTokens: number,
  outputTokens: number
): number | null {
  // Try exact match, then prefix match
  const pricing =
    MODEL_PRICING[model] ?? Object.entries(MODEL_PRICING).find(([k]) => model.startsWith(k))?.[1]
  if (!pricing) return null

  return (inputTokens / 1000) * pricing.inputPer1k + (outputTokens / 1000) * pricing.outputPer1k
}

/** Format cost as currency string */
export function formatCost(cost: number): string {
  if (cost < 0.01) {
    return `$${(cost * 1000).toFixed(2)}m`
  }
  return `$${cost.toFixed(4)}`
}
