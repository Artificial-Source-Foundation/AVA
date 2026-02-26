/**
 * Context extension.
 * Provides token tracking and context compaction strategies.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { ALL_STRATEGIES } from './strategies.js'
import { trackTokens } from './tracker.js'

export function activate(api: ExtensionAPI): Disposable {
  // Register all compaction strategies
  const strategyDisposables = ALL_STRATEGIES.map((s) => api.registerContextStrategy(s))

  // Track token usage via events
  const tokenDisposable = api.on('llm:usage', (data) => {
    const usage = data as { sessionId: string; inputTokens: number; outputTokens: number }
    trackTokens(usage.sessionId, usage.inputTokens, usage.outputTokens)
  })

  return {
    dispose() {
      for (const d of strategyDisposables) d.dispose()
      tokenDisposable.dispose()
    },
  }
}

export { summarizeStrategy, truncateStrategy } from './strategies.js'
export type { TokenStats } from './tracker.js'
export { getTokenStats, resetTokenStats, trackTokens } from './tracker.js'
