/**
 * Context Budget
 * Lightweight token tracking and compaction trigger.
 * Replaces ContextTracker + Compactor from @ava/core.
 */

/** Local MessageContent type (replaces @ava/core-v2/llm import) */
type MessageContent = string | Array<{ type: string; text?: string; [key: string]: unknown }>

/** Extract plain text from MessageContent. */
function textOf(content: MessageContent): string {
  if (typeof content === 'string') return content
  return content
    .filter((b) => b.type === 'text')
    .map((b) => (b as { text: string }).text)
    .join('\n')
}

/** Stub for getContextStrategies (replaces @ava/core-v2/extensions import) */
function getContextStrategies(): Map<
  string,
  {
    compact: (
      messages: Array<{ role: string; content: string }>,
      target: number
    ) => Array<{ role: string; content: MessageContent }>
  }
> {
  return new Map()
}

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

  constructor(private limit: number) {}

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

  /** Set the total token usage directly (used for external sync from agent events) */
  setUsed(tokens: number): void {
    this.total = tokens
  }

  /** Update the context window limit (e.g. when the selected model changes) */
  setLimit(newLimit: number): void {
    this.limit = newLimit
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
        const text = textOf(cm.content)
        const orig = messages.find((m) => m.content === text)
        return { id: orig?.id ?? '', content: text }
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
