/**
 * Context Tracking & Memory
 * Handles token tracking sync, auto-compaction, memory recall, and API message building.
 */

import { getCoreCompactor, getCoreMemory, getCoreTracker } from '../../services/core-bridge'
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
// Memory Recall
// ============================================================================

/**
 * Recall relevant memories for the current user message.
 * Returns a formatted system message string, or empty if unavailable.
 */
export async function recallMemoryContext(userMessage: string): Promise<string> {
  const memory = getCoreMemory()
  if (!memory) return ''

  try {
    const [similar, procedural] = await Promise.all([
      memory.recallSimilar(userMessage, 3),
      memory.recall({
        type: 'procedural',
        minImportance: 0.5,
        limit: 3,
        orderBy: 'importance',
        order: 'desc',
      }),
    ])

    if (similar.length === 0 && procedural.length === 0) return ''

    const parts: string[] = ['## Relevant Memories\n']

    if (similar.length > 0) {
      parts.push('### Past Experiences')
      for (const r of similar) {
        const pct = (r.similarity * 100).toFixed(0)
        parts.push(`- ${r.memory.content.slice(0, 200)} (${pct}% match)`)
      }
      parts.push('')
    }

    if (procedural.length > 0) {
      parts.push('### Learned Patterns')
      for (const p of procedural) {
        const meta = p.metadata
        const rate =
          meta.successRate != null ? ` (${(meta.successRate * 100).toFixed(0)}% success)` : ''
        parts.push(`- ${p.content.slice(0, 200)}${rate}`)
      }
    }

    return parts.join('\n')
  } catch {
    return '' // Graceful degradation — memory is optional
  }
}

// ============================================================================
// API Message Building
// ============================================================================

/** Build the messages array to send to the LLM API */
export async function buildApiMessages(
  deps: ChatDeps,
  excludeId?: string,
  userMessage?: string
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

  // Prepend memory context (before custom instructions so instructions take priority)
  if (userMessage) {
    const memoryContext = await recallMemoryContext(userMessage)
    if (memoryContext) {
      msgs.unshift({ role: 'system', content: memoryContext })
    }
  }

  return msgs
}
