/**
 * Mock LLM Client
 * Programmable LLMClient that yields scripted StreamDelta sequences for testing
 */

import type { LLMClient } from '../../llm/client.js'
import type { StreamDelta, ToolUseBlock } from '../../types/llm.js'

// ============================================================================
// Types
// ============================================================================

export interface MockLLMTurn {
  content?: string
  toolCalls?: Array<{ id: string; name: string; input: Record<string, unknown> }>
  usage?: { inputTokens: number; outputTokens: number }
}

// ============================================================================
// Mock LLM Client
// ============================================================================

/**
 * Create a mock LLM client that yields scripted turns.
 * Each call to stream() pops the next turn from the queue.
 * If the queue is empty, auto-yields attempt_completion to prevent infinite loops.
 */
export function createMockLLMClient(turns: MockLLMTurn[]): LLMClient {
  const queue = [...turns]

  return {
    async *stream(): AsyncGenerator<StreamDelta, void, unknown> {
      const turn = queue.shift()

      // Auto-complete if no more scripted turns
      if (!turn) {
        const toolUse: ToolUseBlock = {
          type: 'tool_use',
          id: 'auto-complete',
          name: 'attempt_completion',
          input: { result: 'Auto-completed: no more scripted turns' },
        }
        yield { content: '', toolUse }
        yield {
          content: '',
          usage: { inputTokens: 10, outputTokens: 10 },
          done: true,
        }
        return
      }

      // Yield content if present
      if (turn.content) {
        yield { content: turn.content }
      }

      // Yield tool calls
      if (turn.toolCalls) {
        for (const tc of turn.toolCalls) {
          const toolUse: ToolUseBlock = {
            type: 'tool_use',
            id: tc.id,
            name: tc.name,
            input: tc.input,
          }
          yield { content: '', toolUse }
        }
      }

      // Yield usage
      yield {
        content: '',
        usage: turn.usage ?? { inputTokens: 100, outputTokens: 50 },
        done: true,
      }
    },
  }
}
