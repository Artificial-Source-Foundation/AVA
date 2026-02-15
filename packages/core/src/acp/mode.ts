/**
 * ACP Mode Switching
 *
 * Manages plan/agent mode switching for ACP sessions.
 * Integrates with AVA's plan mode system to restrict tools
 * when in plan mode.
 */

import type { AcpMode, AcpTransport } from './types.js'
import { AcpError, AcpErrorCode } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Tools allowed in plan mode */
const PLAN_MODE_TOOLS = new Set(['glob', 'grep', 'read', 'ls', 'websearch', 'webfetch', 'todoread'])

/** Valid mode values */
const VALID_MODES = new Set<AcpMode>(['agent', 'plan'])

// ============================================================================
// ACP Mode Manager
// ============================================================================

/**
 * Manages mode switching for ACP sessions.
 *
 * When `session/set_mode` is received from the editor:
 * - `agent` mode: All tools available
 * - `plan` mode: Only read-only tools (glob, grep, read, ls, etc.)
 */
export class AcpModeManager {
  private modes = new Map<string, AcpMode>()
  private transport: AcpTransport | null = null
  private listeners = new Set<ModeChangeListener>()

  // ==========================================================================
  // Configuration
  // ==========================================================================

  /**
   * Set the ACP transport for sending mode change notifications
   */
  setTransport(transport: AcpTransport): void {
    this.transport = transport
  }

  // ==========================================================================
  // Mode Operations
  // ==========================================================================

  /**
   * Set the mode for a session
   *
   * @param sessionId - ACP session ID
   * @param mode - Target mode ('agent' or 'plan')
   * @throws AcpError if mode is invalid
   */
  setMode(sessionId: string, mode: AcpMode): void {
    if (!VALID_MODES.has(mode)) {
      throw new AcpError(
        AcpErrorCode.INVALID_MODE,
        `Invalid mode: '${mode}'. Must be 'agent' or 'plan'.`
      )
    }

    const previousMode = this.modes.get(sessionId) ?? 'agent'
    this.modes.set(sessionId, mode)

    // Notify listeners
    if (previousMode !== mode) {
      this.notifyModeChange(sessionId, mode, previousMode)
    }
  }

  /**
   * Get the current mode for a session
   */
  getMode(sessionId: string): AcpMode {
    return this.modes.get(sessionId) ?? 'agent'
  }

  /**
   * Check if a tool is allowed in the current mode
   */
  isToolAllowed(sessionId: string, toolName: string): boolean {
    const mode = this.getMode(sessionId)

    if (mode === 'agent') {
      return true // All tools allowed
    }

    // Plan mode: only read-only tools
    return PLAN_MODE_TOOLS.has(toolName)
  }

  /**
   * Get the list of allowed tools for a session's current mode
   */
  getAllowedTools(sessionId: string): string[] | null {
    const mode = this.getMode(sessionId)

    if (mode === 'agent') {
      return null // All tools (no filter)
    }

    return Array.from(PLAN_MODE_TOOLS)
  }

  /**
   * Check if a session is in plan mode
   */
  isPlanMode(sessionId: string): boolean {
    return this.getMode(sessionId) === 'plan'
  }

  // ==========================================================================
  // Session Lifecycle
  // ==========================================================================

  /**
   * Initialize mode for a new session (defaults to 'agent')
   */
  initSession(sessionId: string, mode: AcpMode = 'agent'): void {
    this.modes.set(sessionId, mode)
  }

  /**
   * Remove mode tracking for a deleted session
   */
  removeSession(sessionId: string): void {
    this.modes.delete(sessionId)
  }

  // ==========================================================================
  // Events
  // ==========================================================================

  /**
   * Subscribe to mode change events
   */
  onModeChange(listener: ModeChangeListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  private notifyModeChange(sessionId: string, newMode: AcpMode, previousMode: AcpMode): void {
    const event: ModeChangeEvent = { sessionId, mode: newMode, previousMode }

    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch {
        // Ignore listener errors
      }
    }

    // Notify the editor if transport is available
    if (this.transport) {
      this.transport.notify('session/mode_changed', {
        sessionId,
        mode: newMode,
      })
    }
  }
}

// ============================================================================
// Types
// ============================================================================

/** Mode change event */
export interface ModeChangeEvent {
  sessionId: string
  mode: AcpMode
  previousMode: AcpMode
}

/** Listener for mode changes */
export type ModeChangeListener = (event: ModeChangeEvent) => void

// ============================================================================
// Factory
// ============================================================================

/**
 * Create an ACP mode manager
 */
export function createAcpModeManager(): AcpModeManager {
  return new AcpModeManager()
}
