export interface PricingInfo {
  inputPer1M: number
  outputPer1M: number
}

export interface SessionCostModelStats {
  provider: string
  model: string
  inputTokens: number
  outputTokens: number
  turns: number
  costUsd: number
}

export interface SessionCostStats {
  sessionId: string
  totalInputTokens: number
  totalOutputTokens: number
  totalTokens: number
  totalTurns: number
  totalCostUsd: number
  byModel: Record<string, SessionCostModelStats>
}

const pricingByModel = new Map<string, PricingInfo>()
const sessionCosts = new Map<string, SessionCostStats>()

function modelKey(provider: string, model: string): string {
  return `${provider}:${model}`
}

function toCost(tokens: number, per1M: number): number {
  return (tokens / 1_000_000) * per1M
}

export function registerModelPricing(provider: string, model: string, pricing: PricingInfo): void {
  pricingByModel.set(modelKey(provider, model), pricing)
}

export function getModelPricing(provider: string, model: string): PricingInfo | null {
  return pricingByModel.get(modelKey(provider, model)) ?? null
}

export function trackSessionCost(
  sessionId: string,
  provider: string,
  model: string,
  inputTokens: number,
  outputTokens: number
): SessionCostStats {
  const existing =
    sessionCosts.get(sessionId) ??
    ({
      sessionId,
      totalInputTokens: 0,
      totalOutputTokens: 0,
      totalTokens: 0,
      totalTurns: 0,
      totalCostUsd: 0,
      byModel: {},
    } satisfies SessionCostStats)

  existing.totalInputTokens += inputTokens
  existing.totalOutputTokens += outputTokens
  existing.totalTokens += inputTokens + outputTokens
  existing.totalTurns += 1

  const key = modelKey(provider, model)
  const modelStats =
    existing.byModel[key] ??
    ({
      provider,
      model,
      inputTokens: 0,
      outputTokens: 0,
      turns: 0,
      costUsd: 0,
    } satisfies SessionCostModelStats)

  modelStats.inputTokens += inputTokens
  modelStats.outputTokens += outputTokens
  modelStats.turns += 1

  const pricing = getModelPricing(provider, model)
  if (pricing) {
    const delta =
      toCost(inputTokens, pricing.inputPer1M) + toCost(outputTokens, pricing.outputPer1M)
    modelStats.costUsd += delta
    existing.totalCostUsd += delta
  }

  existing.byModel[key] = modelStats
  sessionCosts.set(sessionId, existing)
  return existing
}

export function getSessionCost(sessionId: string): SessionCostStats | null {
  return sessionCosts.get(sessionId) ?? null
}

export function resetSessionCost(sessionId?: string): void {
  if (sessionId) {
    sessionCosts.delete(sessionId)
    return
  }
  sessionCosts.clear()
}

export function resetPricingRegistry(): void {
  pricingByModel.clear()
}
