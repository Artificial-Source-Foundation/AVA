import type { ContextStrategy } from '@ava/core-v2/extensions'
import type { ChatMessage, ContentBlock } from '@ava/core-v2/llm'
import { estimateTokens } from './common.js'
import { summarizeStrategy } from './summarize.js'
import { truncateStrategy } from './truncate.js'

const TOOL_RESULT_LIMIT = 2000
const TOOL_RESULT_HEAD = 500
const TOOL_RESULT_TAIL = 500

function targetForWindow(windowTokens: number): number {
  if (windowTokens >= 200_000) return 160_000
  if (windowTokens >= 128_000) return 98_000
  if (windowTokens >= 64_000) return 37_000
  return Math.max(4_000, Math.floor(windowTokens * 0.8))
}

function truncateToolResults(messages: ChatMessage[]): ChatMessage[] {
  return messages.map((message) => {
    if (typeof message.content === 'string') {
      return message
    }

    let changed = false
    const blocks: ContentBlock[] = message.content.map((block) => {
      if (block.type !== 'tool_result' || block.content.length <= TOOL_RESULT_LIMIT) {
        return block
      }

      changed = true
      return {
        ...block,
        content: `${block.content.slice(0, TOOL_RESULT_HEAD)}\n[tool output truncated by tiered compaction]\n${block.content.slice(-TOOL_RESULT_TAIL)}`,
      }
    })

    return changed ? { ...message, content: blocks } : message
  })
}

export const tieredCompactionStrategy: ContextStrategy = {
  name: 'tiered-compaction',
  description: 'Three-stage compaction using truncation, sliding thresholds, and summary fallback',
  compact(messages: ChatMessage[], targetTokens: number): ChatMessage[] {
    const truncated = truncateToolResults(messages)
    const threshold = targetForWindow(targetTokens)
    const slid = truncateStrategy.compact(truncated, threshold)
    const afterSlidingTokens = slid.reduce(
      (sum, msg) => sum + estimateTokens(JSON.stringify(msg)),
      0
    )
    if (afterSlidingTokens <= threshold) {
      return slid
    }

    // TODO(sprint-4): Replace static summarize fallback with cheapest-model summary call.
    // Tracking: issue #0.
    return summarizeStrategy.compact(slid, threshold)
  },
}

export { targetForWindow }
