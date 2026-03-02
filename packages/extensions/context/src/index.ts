/**
 * Context extension.
 * Provides token tracking and context compaction strategies.
 *
 * Compaction pipeline order (agent loop should apply in this sequence):
 * 1. **prune** — Clears old tool result content (preserves message structure)
 * 2. **summarize** or **truncate** — Drops/summarizes entire messages
 *
 * Strategy selection logic:
 * - Sessions > 20 messages use "summarize" (preserves more context)
 * - Shorter sessions use "truncate" (simple and fast)
 * - "prune" always runs first as a pre-pass (reduces payload without losing messages)
 *
 * Emits `context:compacted` event with before/after token counts after compaction.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { ALL_STRATEGIES } from './strategies.js'
import { trackTokens } from './tracker.js'

/** Strategy selection threshold — sessions above this use summarize. */
export const SUMMARIZE_THRESHOLD = 20

/**
 * Select the best compaction strategy name based on session message count.
 * Returns 'summarize' for longer sessions, 'truncate' for shorter ones.
 */
export function selectStrategyName(messageCount: number): 'summarize' | 'truncate' {
  return messageCount > SUMMARIZE_THRESHOLD ? 'summarize' : 'truncate'
}

export function activate(api: ExtensionAPI): Disposable {
  // Register all compaction strategies
  const strategyDisposables = ALL_STRATEGIES.map((s) => api.registerContextStrategy(s))

  // Track token usage via events
  const tokenDisposable = api.on('llm:usage', (data) => {
    const usage = data as { sessionId: string; inputTokens: number; outputTokens: number }
    trackTokens(usage.sessionId, usage.inputTokens, usage.outputTokens)
  })

  // Log compaction events for observability
  const compactedDisposable = api.on('context:compacted', (data) => {
    const event = data as {
      agentId: string
      tokensBefore: number
      tokensAfter: number
      messagesBefore: number
      messagesAfter: number
      strategy: string
    }
    api.log.info(
      `Context compacted: ${event.tokensBefore} → ${event.tokensAfter} tokens ` +
        `(${event.messagesBefore} → ${event.messagesAfter} messages, strategy: ${event.strategy})`
    )
  })

  // Forward session:status events for observability
  const statusDisposable = api.on('session:status', (data) => {
    const event = data as { sessionId: string; status: 'idle' | 'busy' | 'retry' }
    api.log.debug(`Session status: ${event.sessionId} → ${event.status}`)
  })

  return {
    dispose() {
      for (const d of strategyDisposables) d.dispose()
      tokenDisposable.dispose()
      compactedDisposable.dispose()
      statusDisposable.dispose()
    },
  }
}

export {
  estimateTokens,
  PROTECTED_TOOLS,
  PRUNE_TOKEN_BUDGET,
  pruneStrategy,
  summarizeStrategy,
  truncateStrategy,
} from './strategies.js'
export type { TokenStats } from './tracker.js'
export { getTokenStats, resetTokenStats, trackTokens } from './tracker.js'
