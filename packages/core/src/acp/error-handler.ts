/**
 * ACP Error Handler
 *
 * Graceful error handling for ACP sessions:
 * - Editor disconnect detection
 * - Session state preservation on crash
 * - Error reporting via ACP error format
 * - Automatic retry for transient failures
 */

import type { AcpSessionStore } from './session-store.js'
import { AcpError, AcpErrorCode } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Maximum errors before triggering emergency save */
const ERROR_THRESHOLD = 3

/** Time window for counting errors (30 seconds) */
const ERROR_WINDOW_MS = 30_000

// ============================================================================
// Error Handler
// ============================================================================

/**
 * Handles errors and disconnections in ACP sessions.
 *
 * Responsibilities:
 * - Track error frequency to detect instability
 * - Preserve session state on editor disconnect
 * - Format errors for ACP JSON-RPC protocol
 * - Manage graceful shutdown on fatal errors
 */
export class AcpErrorHandler {
  private sessionStore: AcpSessionStore | null = null
  private errorHistory: ErrorEntry[] = []
  private disconnectCallbacks = new Set<() => void>()
  private disposed = false

  // ==========================================================================
  // Configuration
  // ==========================================================================

  /**
   * Set the session store for emergency saves
   */
  setSessionStore(store: AcpSessionStore): void {
    this.sessionStore = store
  }

  // ==========================================================================
  // Error Handling
  // ==========================================================================

  /**
   * Handle an error during ACP operation.
   * Returns a formatted error for the ACP protocol.
   */
  async handleError(error: unknown, context?: string): Promise<FormattedError> {
    const formatted = this.formatError(error)

    // Track error
    this.errorHistory.push({
      timestamp: Date.now(),
      code: formatted.code,
      message: formatted.message,
      context,
    })

    // Prune old errors
    this.pruneErrors()

    // Check if we've hit the error threshold
    if (this.getRecentErrorCount() >= ERROR_THRESHOLD) {
      await this.emergencySave()
    }

    return formatted
  }

  /**
   * Handle editor disconnect.
   * Saves all sessions and notifies listeners.
   */
  async handleDisconnect(): Promise<void> {
    if (this.disposed) return

    // Save all sessions
    await this.emergencySave()

    // Notify disconnect listeners
    for (const callback of this.disconnectCallbacks) {
      try {
        callback()
      } catch {
        // Ignore callback errors during disconnect
      }
    }
  }

  /**
   * Register a callback for editor disconnection
   */
  onDisconnect(callback: () => void): () => void {
    this.disconnectCallbacks.add(callback)
    return () => this.disconnectCallbacks.delete(callback)
  }

  // ==========================================================================
  // Error Formatting
  // ==========================================================================

  /**
   * Format any error into an ACP-compatible error response
   */
  formatError(error: unknown): FormattedError {
    if (error instanceof AcpError) {
      return {
        code: error.code,
        message: error.message,
        data: error.data,
      }
    }

    if (error instanceof Error) {
      // Map common errors to ACP error codes
      if (error.name === 'AbortError') {
        return {
          code: AcpErrorCode.CANCELLED,
          message: 'Operation cancelled',
        }
      }

      if (error.message.includes('Session not found')) {
        return {
          code: AcpErrorCode.SESSION_NOT_FOUND,
          message: error.message,
        }
      }

      return {
        code: AcpErrorCode.INTERNAL,
        message: error.message,
        data: { stack: error.stack },
      }
    }

    return {
      code: AcpErrorCode.INTERNAL,
      message: String(error),
    }
  }

  /**
   * Check if an error is a disconnect error
   */
  isDisconnectError(error: unknown): boolean {
    if (error instanceof AcpError) {
      return error.code === AcpErrorCode.DISCONNECTED
    }

    if (error instanceof Error) {
      const msg = error.message.toLowerCase()
      return (
        msg.includes('broken pipe') ||
        msg.includes('epipe') ||
        msg.includes('connection reset') ||
        msg.includes('stream closed') ||
        msg.includes('stdin closed')
      )
    }

    return false
  }

  /**
   * Check if an error is transient (retryable)
   */
  isTransientError(error: unknown): boolean {
    if (error instanceof Error) {
      const msg = error.message.toLowerCase()
      return (
        msg.includes('timeout') ||
        msg.includes('econnreset') ||
        msg.includes('econnrefused') ||
        msg.includes('rate limit')
      )
    }
    return false
  }

  // ==========================================================================
  // State Management
  // ==========================================================================

  /**
   * Get count of errors in the recent window
   */
  getRecentErrorCount(): number {
    const cutoff = Date.now() - ERROR_WINDOW_MS
    return this.errorHistory.filter((e) => e.timestamp > cutoff).length
  }

  /**
   * Get recent error history
   */
  getErrorHistory(): ReadonlyArray<ErrorEntry> {
    return this.errorHistory
  }

  /**
   * Clear error history
   */
  clearErrors(): void {
    this.errorHistory = []
  }

  // ==========================================================================
  // Cleanup
  // ==========================================================================

  /**
   * Dispose of the error handler
   */
  async dispose(): Promise<void> {
    if (this.disposed) return
    this.disposed = true

    await this.emergencySave()
    this.disconnectCallbacks.clear()
    this.errorHistory = []
  }

  // ==========================================================================
  // Private Helpers
  // ==========================================================================

  /**
   * Emergency save: persist all sessions to disk
   */
  private async emergencySave(): Promise<void> {
    if (!this.sessionStore) return

    try {
      await this.sessionStore.saveAll()
    } catch {
      // Last resort - nothing more we can do
    }
  }

  /**
   * Remove errors outside the tracking window
   */
  private pruneErrors(): void {
    const cutoff = Date.now() - ERROR_WINDOW_MS
    this.errorHistory = this.errorHistory.filter((e) => e.timestamp > cutoff)
  }
}

// ============================================================================
// Types
// ============================================================================

/** Error entry in the tracking history */
export interface ErrorEntry {
  timestamp: number
  code: number
  message: string
  context?: string
}

/** Formatted error for ACP JSON-RPC */
export interface FormattedError {
  code: number
  message: string
  data?: unknown
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create an ACP error handler
 */
export function createAcpErrorHandler(): AcpErrorHandler {
  return new AcpErrorHandler()
}
