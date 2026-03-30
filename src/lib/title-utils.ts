/**
 * Session Title Utilities
 *
 * Derives a human-readable session title from the first user message.
 * Uses a simple heuristic: take the first sentence/line, clean it up,
 * and truncate to a reasonable length.
 */

const MAX_TITLE_LENGTH = 60

/**
 * Derive a session title from the user's first message.
 *
 * Strategy:
 * 1. Take the first line (or first sentence if shorter).
 * 2. Strip markdown artifacts, leading punctuation, excess whitespace.
 * 3. Truncate with ellipsis if needed.
 *
 * Returns null if the message is empty or produces no useful title.
 */
export function deriveSessionTitle(message: string): string | null {
  if (!message || !message.trim()) return null

  // Take first line
  let title = message.split('\n')[0]?.trim() ?? ''

  // Strip markdown code fences, headers, bold/italic markers
  title = title
    .replace(/^#{1,6}\s+/, '') // markdown headers
    .replace(/\*{1,2}([^*]+)\*{1,2}/g, '$1') // bold/italic
    .replace(/`([^`]+)`/g, '$1') // inline code
    .replace(/^\s*[-*]\s+/, '') // list items
    .trim()

  if (!title) return null

  // If the first sentence ends before the max length, use it
  const sentenceEnd = title.search(/[.!?]\s/)
  if (sentenceEnd > 0 && sentenceEnd < MAX_TITLE_LENGTH) {
    title = title.slice(0, sentenceEnd + 1)
  }

  // Truncate with ellipsis
  if (title.length > MAX_TITLE_LENGTH) {
    // Try to break at a word boundary
    const breakPoint = title.lastIndexOf(' ', MAX_TITLE_LENGTH - 3)
    title =
      breakPoint > MAX_TITLE_LENGTH * 0.4
        ? `${title.slice(0, breakPoint)}...`
        : `${title.slice(0, MAX_TITLE_LENGTH - 3)}...`
  }

  return title || null
}
