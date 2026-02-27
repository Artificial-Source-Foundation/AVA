/**
 * Context Budget
 * Lightweight token tracking and compaction trigger.
 * Replaces ContextTracker + Compactor from @ava/core.
 */

import { getContextStrategies } from '@ava/core-v2/extensions'

export interface ContextStats {
  total: number
  limit: number
  remaining: number
  percentUsed: number
}

export interface CompactionResult {
  messages: Array<{ id: string; content: string }>
  originalCount: number
  compactedCount: number
  tokensSaved: number
  strategyUsed: string
}

export class ContextBudget {
  private messages = new Map<string, number>()
  private total = 0

  constructor(private readonly limit: number) {}

  /** Add a message and its estimated tokens */
  addMessage(id: string, content: string): void {
    const tokens = Math.ceil(content.length / 4)
    const prev = this.messages.get(id) ?? 0
    this.total += tokens - prev
    this.messages.set(id, tokens)
  }

  /** Remove a message from tracking */
  removeMessage(id: string): void {
    const tokens = this.messages.get(id)
    if (tokens !== undefined) {
      this.total -= tokens
      this.messages.delete(id)
    }
  }

  /** Clear all tracked messages */
  clear(): void {
    this.messages.clear()
    this.total = 0
  }

  /** Get current context stats */
  getStats(): ContextStats {
    return {
      total: this.total,
      limit: this.limit,
      remaining: Math.max(0, this.limit - this.total),
      percentUsed: this.limit > 0 ? (this.total / this.limit) * 100 : 0,
    }
  }

  /** Check if compaction is needed based on a threshold percentage */
  needsCompaction(threshold: number): boolean {
    return this.getStats().percentUsed >= threshold
  }

  /** Run compaction using available context strategies */
  async compact(
    messages: Array<{ id: string; content: string; role: string }>
  ): Promise<CompactionResult> {
    const strategies = getContextStrategies()
    const targetTokens = Math.floor(this.limit * 0.5)

    // Use first available strategy
    for (const [name, strategy] of strategies) {
      const chatMessages = messages.map((m) => ({
        role: m.role as 'user' | 'assistant' | 'system',
        content: m.content,
      }))
      const compacted = strategy.compact(chatMessages, targetTokens)
      const kept = compacted.map((cm) => {
        const orig = messages.find((m) => m.content === cm.content)
        return { id: orig?.id ?? '', content: cm.content }
      })

      const newTotal = kept.reduce((sum, m) => sum + Math.ceil(m.content.length / 4), 0)
      return {
        messages: kept,
        originalCount: messages.length,
        compactedCount: kept.length,
        tokensSaved: this.total - newTotal,
        strategyUsed: name,
      }
    }

    // No strategies available — return as-is
    return {
      messages: messages.map((m) => ({ id: m.id, content: m.content })),
      originalCount: messages.length,
      compactedCount: messages.length,
      tokensSaved: 0,
      strategyUsed: 'none',
    }
  }
}
