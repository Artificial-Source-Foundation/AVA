/**
 * ID Generation Utilities
 *
 * Consolidated from duplicate implementations across the codebase.
 */

/** Generate a unique message ID with the given prefix: "msg-1710000000000-a1b2" */
export function generateMessageId(prefix = 'msg'): string {
  return `${prefix}-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`
}
