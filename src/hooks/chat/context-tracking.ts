/**
 * Context Tracking & Memory
 * Handles token tracking sync, auto-compaction, memory recall, and API message building.
 */

import { getCoreCompactor, getCoreTracker } from '../../services/core-bridge'
import { deleteMessageFromDb } from '../../services/database'
import { logInfo, logWarn } from '../../services/logger'
import type { ChatDeps } from './types'

// ============================================================================
// Tracker Stats Sync
// ============================================================================

/** Sync tracker stats → contextStats signal */
export function syncTrackerStats(deps: ChatDeps): void {
  const tracker = getCoreTracker()
  if (!tracker) return
  const s = tracker.getStats()
  deps.setContextStats({
    total: s.total,
    limit: s.limit,
    remaining: s.remaining,
    percentUsed: s.percentUsed,
  })
}

// ============================================================================
// Auto-Compaction
// ============================================================================

/**
 * Auto-compact conversation when context exceeds 80%.
 * Uses sliding window to trim to ~50%, syncs state + DB.
 */
export async function maybeCompact(deps: ChatDeps): Promise<void> {
  const tracker = getCoreTracker()
  const compactor = getCoreCompactor()
  if (!tracker || !compactor || !compactor.needsCompaction(80)) return

  const currentMsgs = deps.session.messages()
  if (currentMsgs.length <= 4) return

  // Convert frontend messages to core Message format
  const coreMessages = currentMsgs.map((m) => ({
    id: m.id,
    sessionId: m.sessionId,
    role: m.role as 'user' | 'assistant' | 'system',
    content: m.content,
    createdAt: m.createdAt,
  }))

  try {
    const result = await compactor.compact(coreMessages)
    if (result.tokensSaved === 0) return

    // Determine which messages were removed
    const keptIds = new Set(result.messages.map((m) => m.id))
    const removedMsgs = currentMsgs.filter((m) => !keptIds.has(m.id))

    // Update frontend state: keep only surviving messages
    deps.session.setMessages(currentMsgs.filter((m) => keptIds.has(m.id)))

    // Sync database: delete removed messages
    await Promise.all(removedMsgs.map((m) => deleteMessageFromDb(m.id)))

    // Rebuild tracker with remaining messages
    tracker.clear()
    for (const m of result.messages) {
      tracker.addMessage(m.id, m.content)
    }
    syncTrackerStats(deps)

    logInfo(deps.LOG_SRC, 'Compaction complete', {
      removed: result.originalCount - result.compactedCount,
      tokensSaved: result.tokensSaved,
      strategy: result.strategyUsed,
    })
  } catch (err) {
    logWarn(deps.LOG_SRC, 'Compaction failed', err)
  }
}

// ============================================================================
// API Message Building
// ============================================================================

/** Build the messages array to send to the LLM API */
export async function buildApiMessages(
  deps: ChatDeps,
  excludeId?: string
): Promise<Array<{ role: 'user' | 'assistant' | 'system'; content: string | unknown[] }>> {
  const msgs = deps.session
    .messages()
    .filter((m) => m.id !== excludeId)
    .map((m) => {
      // Build multimodal content if message has images
      const imgs = (m.metadata?.images ?? []) as Array<{
        data: string
        mimeType: string
      }>
      if (imgs.length > 0) {
        return {
          role: m.role as 'user' | 'assistant' | 'system',
          content: [
            ...imgs.map((img) => ({
              type: 'image' as const,
              source: { type: 'base64' as const, media_type: img.mimeType, data: img.data },
            })),
            { type: 'text' as const, text: m.content },
          ] as unknown as string,
        }
      }
      return {
        role: m.role as 'user' | 'assistant' | 'system',
        content: m.content,
      }
    })

  // Prepend custom instructions as system message
  const instructions = deps.settings.settings().generation.customInstructions.trim()
  if (instructions) {
    msgs.unshift({ role: 'system', content: instructions })
  }

  return msgs
}
