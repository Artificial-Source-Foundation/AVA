/**
 * Summarization Compaction Strategy
 *
 * Uses LLM to summarize older messages while preserving recent context.
 * Creates a summary message that captures the gist of dropped messages.
 *
 * Pros:
 * - Preserves important context from older messages
 * - Maintains conversation continuity
 * - Better for long, complex conversations
 *
 * Cons:
 * - Requires LLM call (slower, costs tokens)
 * - Summary quality depends on LLM
 * - May lose specific details
 */

import { countTokens } from '../tracker.js'
import type { CompactionStrategy, Message, SummarizeConfig, SummarizeFn } from '../types.js'

// ============================================================================
// Default Summarization Prompt
// ============================================================================

const SUMMARIZE_SYSTEM_PROMPT = `You are a conversation summarizer. Your task is to create a concise summary of the conversation history that preserves:

1. Key decisions and conclusions
2. Important context and facts established
3. Current goals or tasks being worked on
4. Any pending questions or unresolved issues

Keep the summary focused and factual. Use bullet points for clarity.
Do not include pleasantries or conversational filler.`

/**
 * Format messages for summarization
 */
function formatMessagesForSummary(messages: Message[]): string {
  return messages
    .map((m) => {
      const role = m.role === 'user' ? 'User' : m.role === 'assistant' ? 'Assistant' : 'System'
      return `[${role}]: ${m.content}`
    })
    .join('\n\n')
}

// ============================================================================
// Strategy Implementation
// ============================================================================

/**
 * Create a summarization strategy with custom config
 *
 * @example
 * ```ts
 * const strategy = createSummarize({
 *   preserveRecent: 6,
 *   summarizeFn: async (messages) => {
 *     // Custom LLM call
 *     return await myLLM.summarize(messages)
 *   }
 * })
 * ```
 */
export function createSummarize(config: SummarizeConfig = {}): CompactionStrategy {
  const { preserveRecent = 6, summarizeFn } = config

  // Default summarize function that throws (must be provided)
  const defaultSummarizeFn: SummarizeFn = async () => {
    throw new Error(
      'No summarizeFn provided to summarize strategy. ' +
        'Provide a custom function that calls your LLM to generate summaries.'
    )
  }

  const summarize = summarizeFn ?? defaultSummarizeFn

  return {
    name: 'summarize',

    async compact(messages: Message[], _targetTokens: number): Promise<Message[]> {
      if (messages.length === 0) {
        return []
      }

      // Separate system message
      const systemMessage = messages.find((m) => m.role === 'system')
      const conversationMessages = messages.filter((m) => m.role !== 'system')

      // If few messages, no need to summarize
      if (conversationMessages.length <= preserveRecent) {
        return systemMessage ? [systemMessage, ...conversationMessages] : conversationMessages
      }

      // Split: older messages to summarize, recent to preserve
      const recentMessages = conversationMessages.slice(-preserveRecent)
      const olderMessages = conversationMessages.slice(0, -preserveRecent)

      if (olderMessages.length === 0) {
        return systemMessage ? [systemMessage, ...recentMessages] : recentMessages
      }

      // Summarize older messages
      const summary = await summarize(olderMessages)

      // Create summary message
      const summaryMessage: Message = {
        id: `summary-${Date.now()}`,
        sessionId: messages[0].sessionId,
        role: 'system',
        content: `[Previous conversation summary]\n${summary}`,
        createdAt: Date.now(),
        tokenCount: countTokens(summary),
      }

      // Combine: system + summary + recent
      const result: Message[] = []

      if (systemMessage) {
        result.push(systemMessage)
      }

      result.push(summaryMessage)
      result.push(...recentMessages)

      return result
    },
  }
}

// ============================================================================
// Standalone Summarize Utility
// ============================================================================

/**
 * Generate a summarization prompt for messages
 * Can be used with any LLM client
 */
export function getSummarizationPrompt(messages: Message[]): { system: string; user: string } {
  const formatted = formatMessagesForSummary(messages)

  return {
    system: SUMMARIZE_SYSTEM_PROMPT,
    user: `Please summarize the following conversation:\n\n${formatted}`,
  }
}

/**
 * Extract summary from LLM response
 * Handles common response patterns
 */
export function extractSummary(response: string): string {
  // Remove common prefixes
  const prefixes = [
    'Here is a summary:',
    "Here's a summary:",
    'Summary:',
    'Here is the summary:',
    "Here's the summary:",
  ]

  let summary = response.trim()

  for (const prefix of prefixes) {
    if (summary.toLowerCase().startsWith(prefix.toLowerCase())) {
      summary = summary.slice(prefix.length).trim()
      break
    }
  }

  return summary
}

// ============================================================================
// Default Export (requires summarizeFn)
// ============================================================================

/**
 * Default summarize strategy instance
 * Note: Requires providing summarizeFn in compactor options
 */
export const summarize = createSummarize()

export default summarize
