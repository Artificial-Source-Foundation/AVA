/**
 * ACP Session Store
 *
 * Bridges ACP sessions to AVA's SessionManager for persistence.
 * Maps ACP session IDs to AVA session IDs and handles save/load/resume.
 */

import { createFileSessionStorage } from '../session/file-storage.js'
import { createSessionManager, type SessionManager } from '../session/manager.js'
import type { SessionMeta, SessionState } from '../session/types.js'
import type { AcpSessionInfo } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Prefix for ACP-originated session names */
const ACP_SESSION_PREFIX = 'ACP: '

// ============================================================================
// ACP Session Store
// ============================================================================

/**
 * Manages ACP session persistence using AVA's SessionManager.
 *
 * - Maps ACP session IDs → AVA session IDs
 * - Persists sessions to ~/.estela/sessions/
 * - Supports resume via `session/load`
 */
export class AcpSessionStore {
  private sessionManager: SessionManager
  private mappings = new Map<string, AcpSessionInfo>()
  private disposed = false

  constructor(sessionManager?: SessionManager) {
    this.sessionManager = sessionManager ?? createDefaultSessionManager()
  }

  // ==========================================================================
  // Session Lifecycle
  // ==========================================================================

  /**
   * Create a new ACP session with persistence
   */
  async create(acpSessionId: string, workingDirectory: string): Promise<SessionState> {
    this.ensureNotDisposed()

    const session = await this.sessionManager.create(
      `${ACP_SESSION_PREFIX}${acpSessionId}`,
      workingDirectory
    )

    const info: AcpSessionInfo = {
      sessionId: acpSessionId,
      estelaSessionId: session.id,
      workingDirectory,
      createdAt: Date.now(),
      lastActiveAt: Date.now(),
      mode: 'agent',
    }

    this.mappings.set(acpSessionId, info)
    return session
  }

  /**
   * Get the AVA session for an ACP session ID
   */
  async get(acpSessionId: string): Promise<SessionState | null> {
    this.ensureNotDisposed()

    const info = this.mappings.get(acpSessionId)
    if (!info) return null

    return this.sessionManager.get(info.estelaSessionId)
  }

  /**
   * Load a previously persisted session by AVA session ID.
   * Used by ACP's `session/load` capability.
   */
  async load(estelaSessionId: string): Promise<SessionState | null> {
    this.ensureNotDisposed()

    const session = await this.sessionManager.get(estelaSessionId)
    if (!session) return null

    // Create a mapping for the loaded session
    const acpSessionId = `resumed-${Date.now()}`
    const info: AcpSessionInfo = {
      sessionId: acpSessionId,
      estelaSessionId: session.id,
      workingDirectory: session.workingDirectory,
      createdAt: session.createdAt,
      lastActiveAt: Date.now(),
      mode: 'agent',
    }

    this.mappings.set(acpSessionId, info)
    return session
  }

  /**
   * Save the current state of an ACP session
   */
  async save(acpSessionId: string): Promise<void> {
    this.ensureNotDisposed()

    const info = this.mappings.get(acpSessionId)
    if (!info) return

    info.lastActiveAt = Date.now()
    await this.sessionManager.save(info.estelaSessionId)
  }

  /**
   * Save all active sessions
   */
  async saveAll(): Promise<void> {
    this.ensureNotDisposed()

    for (const info of this.mappings.values()) {
      info.lastActiveAt = Date.now()
      await this.sessionManager.save(info.estelaSessionId)
    }
  }

  /**
   * Delete an ACP session
   */
  async delete(acpSessionId: string): Promise<void> {
    this.ensureNotDisposed()

    const info = this.mappings.get(acpSessionId)
    if (!info) return

    await this.sessionManager.delete(info.estelaSessionId)
    this.mappings.delete(acpSessionId)
  }

  // ==========================================================================
  // Listing & Query
  // ==========================================================================

  /**
   * List all available sessions for ACP `session/load` capability
   */
  async list(): Promise<SessionMeta[]> {
    this.ensureNotDisposed()
    return this.sessionManager.list()
  }

  /**
   * Get session info for an ACP session
   */
  getInfo(acpSessionId: string): AcpSessionInfo | null {
    return this.mappings.get(acpSessionId) ?? null
  }

  /**
   * Check if an ACP session exists
   */
  has(acpSessionId: string): boolean {
    return this.mappings.has(acpSessionId)
  }

  /**
   * Get the AVA session ID for an ACP session
   */
  getAVAId(acpSessionId: string): string | null {
    return this.mappings.get(acpSessionId)?.estelaSessionId ?? null
  }

  /**
   * Get the underlying SessionManager
   */
  getSessionManager(): SessionManager {
    return this.sessionManager
  }

  // ==========================================================================
  // Mode
  // ==========================================================================

  /**
   * Update the mode for an ACP session
   */
  setMode(acpSessionId: string, mode: AcpSessionInfo['mode']): void {
    const info = this.mappings.get(acpSessionId)
    if (info) {
      info.mode = mode
    }
  }

  /**
   * Get the current mode for an ACP session
   */
  getMode(acpSessionId: string): AcpSessionInfo['mode'] | null {
    return this.mappings.get(acpSessionId)?.mode ?? null
  }

  // ==========================================================================
  // Cleanup
  // ==========================================================================

  /**
   * Dispose of the session store - saves all dirty sessions
   */
  async dispose(): Promise<void> {
    if (this.disposed) return

    // Save before marking disposed (saveAll checks disposed flag)
    try {
      for (const info of this.mappings.values()) {
        info.lastActiveAt = Date.now()
        await this.sessionManager.save(info.estelaSessionId)
      }
    } catch {
      // Best-effort save during dispose
    }

    this.disposed = true
    await this.sessionManager.dispose()
    this.mappings.clear()
  }

  private ensureNotDisposed(): void {
    if (this.disposed) {
      throw new Error('AcpSessionStore has been disposed')
    }
  }
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create an ACP session store with default configuration
 */
export function createAcpSessionStore(sessionManager?: SessionManager): AcpSessionStore {
  return new AcpSessionStore(sessionManager)
}

/**
 * Create a default session manager with file persistence
 */
function createDefaultSessionManager(): SessionManager {
  const storage = createFileSessionStorage()
  return createSessionManager({
    maxSessions: 20,
    autoSaveInterval: 30_000, // 30 seconds
    storage,
  })
}
