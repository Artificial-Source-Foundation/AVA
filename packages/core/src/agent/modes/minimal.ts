/**
 * Minimal Tool Mode
 * Restricts available tools to a core subset for token efficiency
 *
 * When enabled, the agent can only use:
 * - read_file, write_file, edit, bash, glob, grep
 * - attempt_completion, question
 *
 * Follows the same per-session state pattern as plan mode.
 */

import type { ToolResult } from '../../tools/types.js'

// ============================================================================
// Constants
// ============================================================================

/**
 * Tools allowed in minimal mode (core operations only)
 */
export const MINIMAL_MODE_ALLOWED_TOOLS: readonly string[] = [
  // File operations
  'read_file',
  'write_file',
  'edit',
  // Search
  'glob',
  'grep',
  // Execution
  'bash',
  // Completion & interaction
  'attempt_completion',
  'question',
] as const

// ============================================================================
// Per-Session State
// ============================================================================

const minimalModeStates = new Map<string, boolean>()
const DEFAULT_SESSION = 'default'

/**
 * Check if minimal mode is active for a session
 */
export function isMinimalModeActive(sessionId?: string): boolean {
  return minimalModeStates.get(sessionId ?? DEFAULT_SESSION) ?? false
}

/**
 * Enter minimal mode for a session
 */
export function enterMinimalMode(sessionId?: string): void {
  minimalModeStates.set(sessionId ?? DEFAULT_SESSION, true)
}

/**
 * Exit minimal mode for a session
 */
export function exitMinimalMode(sessionId?: string): void {
  minimalModeStates.set(sessionId ?? DEFAULT_SESSION, false)
}

/**
 * Clear all minimal mode states (for testing)
 */
export function clearAllMinimalModeStates(): void {
  minimalModeStates.clear()
}

// ============================================================================
// Registry Integration Helper
// ============================================================================

/**
 * Check tool access in minimal mode and return error if restricted.
 * Call this from registry.executeTool() before executing.
 */
export function checkMinimalModeAccess(
  toolName: string,
  sessionId?: string
): { allowed: boolean; error?: ToolResult } {
  if (!isMinimalModeActive(sessionId)) {
    return { allowed: true }
  }

  if (MINIMAL_MODE_ALLOWED_TOOLS.includes(toolName)) {
    return { allowed: true }
  }

  return {
    allowed: false,
    error: {
      success: false,
      output: `Tool '${toolName}' is not available in minimal mode. Available tools: ${MINIMAL_MODE_ALLOWED_TOOLS.join(', ')}`,
      error: 'MINIMAL_MODE_RESTRICTED',
    },
  }
}
