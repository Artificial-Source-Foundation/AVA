/**
 * ID Generation Utilities
 *
 * Consolidated from duplicate implementations across the codebase.
 */

/** Generate a unique message ID as a UUID v4 (compatible with Rust backend). */
export function generateMessageId(_prefix = 'msg'): string {
  return crypto.randomUUID()
}
