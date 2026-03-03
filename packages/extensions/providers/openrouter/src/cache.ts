/**
 * OpenRouter Prompt Caching
 *
 * Adds `cache_control: { type: 'ephemeral' }` markers to OpenAI-format messages.
 * OpenRouter passes these through to Anthropic models for prompt caching.
 * https://openrouter.ai/docs/features/prompt-caching
 */

interface CacheControl {
  type: 'ephemeral'
}

interface OpenAIBodyMessage {
  role: string
  content: string | Array<Record<string, unknown>> | null
  [key: string]: unknown
}

/**
 * Wrap a string content value as an array with a cache-marked text block.
 */
function wrapWithCacheMarker(content: string): Array<Record<string, unknown>> {
  return [{ type: 'text', text: content, cache_control: { type: 'ephemeral' } as CacheControl }]
}

/**
 * Mark the last block of an array content with cache_control.
 */
function markLastBlock(content: Array<Record<string, unknown>>): Array<Record<string, unknown>> {
  if (content.length === 0) return content
  const blocks = content.map((b) => ({ ...b }))
  blocks[blocks.length - 1]!.cache_control = { type: 'ephemeral' } as CacheControl
  return blocks
}

/**
 * Add cache_control markers to OpenAI-format body messages for OpenRouter.
 *
 * Strategy:
 * - Mark system message with cache_control (cached across turns)
 * - Mark last 2 user/tool messages with cache_control (sliding window)
 *
 * Returns a new array — does not mutate the input.
 */
/**
 * Add cache_control marker to the last tool in OpenAI-format tool array.
 * OpenRouter passes cache_control through to Anthropic models.
 * Returns a new array — does not mutate the input.
 */
export function addToolCacheMarkers(
  tools: Array<Record<string, unknown>>
): Array<Record<string, unknown>> {
  if (tools.length === 0) return tools
  const result = tools.map((t) => ({ ...t }))
  result[result.length - 1]!.cache_control = { type: 'ephemeral' } as CacheControl
  return result
}

export function addCacheControlMarkers(messages: OpenAIBodyMessage[]): OpenAIBodyMessage[] {
  if (messages.length === 0) return []

  const result: OpenAIBodyMessage[] = messages.map((m) => ({ ...m }))

  // Mark system message
  for (const msg of result) {
    if (msg.role === 'system') {
      if (typeof msg.content === 'string') {
        msg.content = wrapWithCacheMarker(msg.content)
      } else if (Array.isArray(msg.content)) {
        msg.content = markLastBlock(msg.content)
      }
      break
    }
  }

  // Find last 2 user messages and mark them
  const userIndices: number[] = []
  for (let i = result.length - 1; i >= 0; i--) {
    if (result[i]!.role === 'user') {
      userIndices.push(i)
      if (userIndices.length === 2) break
    }
  }

  for (const idx of userIndices) {
    const msg = result[idx]!
    if (typeof msg.content === 'string') {
      msg.content = wrapWithCacheMarker(msg.content)
    } else if (Array.isArray(msg.content)) {
      msg.content = markLastBlock(msg.content)
    }
  }

  return result
}
