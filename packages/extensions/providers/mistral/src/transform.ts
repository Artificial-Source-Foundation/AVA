/**
 * Mistral-specific message transforms.
 *
 * Mistral requires tool_call IDs to be max 9 characters, alphanumeric only.
 */

/**
 * Truncate a tool call ID to fit Mistral's constraints.
 * - Max 9 characters
 * - Alphanumeric only (strips non-alphanumeric chars first)
 */
export function truncateMistralIds(id: string): string {
  const cleaned = id.replace(/[^a-zA-Z0-9]/g, '')
  return cleaned.slice(0, 9)
}
