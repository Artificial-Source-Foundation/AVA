import { getSettingsManager } from '@ava/core-v2/config'
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { ChatMessage } from '@ava/core-v2/llm'
import { registerModelPricing, trackSessionCost } from './cost-tracker.js'
import {
  createHistoryProcessorByName,
  type HistoryProcessor,
  runHistoryProcessors,
} from './processors/index.js'
import { ALL_STRATEGIES, summarizeStrategy, truncateStrategy } from './strategies.js'
import { trackTokens } from './tracker.js'

export interface ContextExtensionSettings {
  strategy: string | string[]
  historyProcessors: string[]
}

export const DEFAULT_CONTEXT_SETTINGS: ContextExtensionSettings = {
  strategy: 'auto',
  historyProcessors: ['last-n-observations', 'tag-tool-calls'],
}

export const SUMMARIZE_THRESHOLD = 20

export function selectStrategyName(
  messageCount: number,
  settings: ContextExtensionSettings = DEFAULT_CONTEXT_SETTINGS
): string | string[] {
  if (Array.isArray(settings.strategy)) return settings.strategy
  if (settings.strategy !== 'auto') return settings.strategy
  return messageCount > SUMMARIZE_THRESHOLD ? summarizeStrategy.name : truncateStrategy.name
}

function createProcessors(settings: ContextExtensionSettings): HistoryProcessor[] {
  const result: HistoryProcessor[] = []
  for (const name of settings.historyProcessors) {
    const processor = createHistoryProcessorByName(name, { provider: 'anthropic' })
    if (processor) result.push(processor)
  }
  return result
}

function mergeSettings(value: unknown): ContextExtensionSettings {
  const partial = (value ?? {}) as Partial<ContextExtensionSettings>
  return {
    strategy: partial.strategy ?? DEFAULT_CONTEXT_SETTINGS.strategy,
    historyProcessors: partial.historyProcessors ?? DEFAULT_CONTEXT_SETTINGS.historyProcessors,
  }
}

export function activate(api: ExtensionAPI): Disposable {
  const manager = getSettingsManager()
  manager.registerCategory('context', DEFAULT_CONTEXT_SETTINGS)

  const strategyDisposables = ALL_STRATEGIES.map((strategy) =>
    api.registerContextStrategy(strategy)
  )

  let settings = mergeSettings(api.getSettings('context'))
  let processors = createProcessors(settings)

  const settingsDisposable = api.onSettingsChanged('context', (next) => {
    settings = mergeSettings(next)
    processors = createProcessors(settings)
  })

  const hookDisposable =
    typeof api.registerHook === 'function'
      ? api.registerHook('history:process', (_input: ChatMessage[], current: ChatMessage[]) =>
          runHistoryProcessors(current, processors)
        )
      : { dispose() {} }

  const tokenDisposable = api.on('llm:usage', (data) => {
    const usage = data as {
      sessionId: string
      provider?: string
      model?: string
      inputTokens: number
      outputTokens: number
      cacheReadTokens?: number
      cacheCreationTokens?: number
    }
    trackTokens(
      usage.sessionId,
      usage.inputTokens,
      usage.outputTokens,
      usage.cacheReadTokens,
      usage.cacheCreationTokens
    )

    if (usage.provider && usage.model) {
      const costStats = trackSessionCost(
        usage.sessionId,
        usage.provider,
        usage.model,
        usage.inputTokens,
        usage.outputTokens
      )
      api.emit('session:cost', costStats)
    }
  })

  const providerDisposable = api.on('provider:registered', (data) => {
    const payload = data as {
      provider?: string
      models?: Array<{ id: string; pricing?: { inputPer1M: number; outputPer1M: number } }>
    }
    if (!payload.provider || !payload.models) return
    for (const model of payload.models) {
      if (!model.pricing) continue
      registerModelPricing(payload.provider, model.id, model.pricing)
    }
  })

  const modelsDisposable = api.on('models:register', (data) => {
    const models = data as Array<{
      provider: string
      id: string
      pricing?: { inputPer1M: number; outputPer1M: number }
    }>
    for (const model of models) {
      if (!model.pricing) continue
      registerModelPricing(model.provider, model.id, model.pricing)
    }
  })

  const compactedDisposable = api.on('context:compacted', (data) => {
    const event = data as {
      tokensBefore: number
      tokensAfter: number
      messagesBefore: number
      messagesAfter: number
      strategy: string
    }
    api.log.info(
      `Context compacted: ${event.tokensBefore} -> ${event.tokensAfter} tokens (${event.messagesBefore} -> ${event.messagesAfter} messages, strategy: ${event.strategy})`
    )
  })

  const statusDisposable = api.on('session:status', (data) => {
    const event = data as { sessionId: string; status: 'idle' | 'busy' | 'retry' }
    api.log.debug(`Session status: ${event.sessionId} -> ${event.status}`)
  })

  return {
    dispose() {
      for (const disposable of strategyDisposables) disposable.dispose()
      hookDisposable.dispose()
      settingsDisposable.dispose()
      tokenDisposable.dispose()
      providerDisposable.dispose()
      modelsDisposable.dispose()
      compactedDisposable.dispose()
      statusDisposable.dispose()
    },
  }
}

export {
  getModelPricing,
  getSessionCost,
  registerModelPricing,
  resetPricingRegistry,
  resetSessionCost,
  trackSessionCost,
} from './cost-tracker.js'
export {
  estimateTokens,
  PROTECTED_TOOLS,
  PRUNE_TOKEN_BUDGET,
  summarizeStrategy,
  truncateStrategy,
} from './strategies.js'
export type { TokenStats } from './tracker.js'
export { getTokenStats, resetTokenStats, trackTokens } from './tracker.js'
