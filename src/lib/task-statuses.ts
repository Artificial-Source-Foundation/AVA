/**
 * Delta9 Task Status Constants
 *
 * Centralized status constants for background tasks to ensure
 * consistency across the codebase.
 */

// =============================================================================
// Constants
// =============================================================================

/**
 * Background task status values
 */
export const BACKGROUND_STATUS = {
  PENDING: 'pending',
  RUNNING: 'running',
  COMPLETED: 'completed',
  FAILED: 'failed',
  CANCELLED: 'cancelled',
} as const

/**
 * All valid background status values
 */
export const ALL_BACKGROUND_STATUSES = [
  BACKGROUND_STATUS.PENDING,
  BACKGROUND_STATUS.RUNNING,
  BACKGROUND_STATUS.COMPLETED,
  BACKGROUND_STATUS.FAILED,
  BACKGROUND_STATUS.CANCELLED,
] as const

// =============================================================================
// Types
// =============================================================================

/**
 * Background task status type
 */
export type BackgroundStatus = (typeof BACKGROUND_STATUS)[keyof typeof BACKGROUND_STATUS]

// =============================================================================
// Helpers
// =============================================================================

/**
 * Check if a status represents a terminal (completed) state
 *
 * @param status - Status string to check
 * @returns True if the task is in a terminal state
 */
export function isTerminalStatus(status: string): boolean {
  return (
    status === BACKGROUND_STATUS.COMPLETED ||
    status === BACKGROUND_STATUS.FAILED ||
    status === BACKGROUND_STATUS.CANCELLED
  )
}

/**
 * Check if a status represents an active (non-terminal) state
 *
 * @param status - Status string to check
 * @returns True if the task is still active
 */
export function isActiveStatus(status: string): boolean {
  return status === BACKGROUND_STATUS.PENDING || status === BACKGROUND_STATUS.RUNNING
}

/**
 * Check if a status represents a successful completion
 *
 * @param status - Status string to check
 * @returns True if the task completed successfully
 */
export function isSuccessStatus(status: string): boolean {
  return status === BACKGROUND_STATUS.COMPLETED
}

/**
 * Check if a status represents a failure
 *
 * @param status - Status string to check
 * @returns True if the task failed or was cancelled
 */
export function isFailureStatus(status: string): boolean {
  return status === BACKGROUND_STATUS.FAILED || status === BACKGROUND_STATUS.CANCELLED
}
