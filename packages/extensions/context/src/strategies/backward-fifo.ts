import type { ContextStrategy } from '@ava/core-v2/extensions'
import type { ChatMessage, ContentBlock } from '@ava/core-v2/llm'

import {
  buildToolUseIdMap,
  estimateTokens,
  isProtectedToolResult,
  PRUNE_MIN_THRESHOLD,
  PRUNE_TOKEN_BUDGET,
} from './common.js'

interface ToolResultRef {
  messageIndex: number
  blockIndex: number
  tokens: number
  protected: boolean
}

export interface BackwardFifoOptions {
  protectionWindowTokens: number
  minPrunableThreshold: number
  sentinel: string
}

const DEFAULT_OPTIONS: BackwardFifoOptions = {
  protectionWindowTokens: PRUNE_TOKEN_BUDGET,
  minPrunableThreshold: PRUNE_MIN_THRESHOLD,
  sentinel: '[Tool output pruned - originally {tokens} tokens]',
}

export function createBackwardFifoStrategy(
  options: Partial<BackwardFifoOptions> = {}
): ContextStrategy {
  const cfg: BackwardFifoOptions = { ...DEFAULT_OPTIONS, ...options }

  return {
    name: 'backward-fifo',
    description: 'Prune oldest tool outputs while protecting newest window',
    compact(messages: ChatMessage[]): ChatMessage[] {
      const refs = collectToolResults(messages)
      if (refs.length === 0) return messages

      let protectedTokens = 0
      const prunable: ToolResultRef[] = []
      for (let i = refs.length - 1; i >= 0; i--) {
        const ref = refs[i]
        if (!ref || ref.protected) continue
        if (protectedTokens < cfg.protectionWindowTokens) {
          protectedTokens += ref.tokens
          continue
        }
        prunable.push(ref)
      }

      const prunableTokens = prunable.reduce((sum, ref) => sum + ref.tokens, 0)
      if (prunableTokens < cfg.minPrunableThreshold) return messages

      const byMessage = new Map<number, ContentBlock[]>()
      for (const ref of prunable.reverse()) {
        const source = byMessage.get(ref.messageIndex) ?? cloneBlocks(messages, ref.messageIndex)
        const target = source[ref.blockIndex]
        if (!target || target.type !== 'tool_result') continue
        source[ref.blockIndex] = {
          ...target,
          content: cfg.sentinel.replace('{tokens}', String(ref.tokens)),
        }
        byMessage.set(ref.messageIndex, source)
      }

      return messages.map((msg, idx) => {
        const blocks = byMessage.get(idx)
        if (!blocks) return msg
        return { ...msg, content: blocks }
      })
    },
  }
}

function cloneBlocks(messages: ChatMessage[], index: number): ContentBlock[] {
  const message = messages[index]
  if (!message || typeof message.content === 'string') return []
  return [...message.content]
}

function collectToolResults(messages: ChatMessage[]): ToolResultRef[] {
  const toolMap = buildToolUseIdMap(messages)
  const refs: ToolResultRef[] = []

  for (let messageIndex = 0; messageIndex < messages.length; messageIndex++) {
    const message = messages[messageIndex]
    if (!message || typeof message.content === 'string') continue
    for (let blockIndex = 0; blockIndex < message.content.length; blockIndex++) {
      const block = message.content[blockIndex]
      if (!block || block.type !== 'tool_result') continue
      refs.push({
        messageIndex,
        blockIndex,
        tokens: estimateTokens(block.content),
        protected: isProtectedToolResult(block.tool_use_id, toolMap),
      })
    }
  }

  return refs
}

export const backwardFifoStrategy = createBackwardFifoStrategy()
