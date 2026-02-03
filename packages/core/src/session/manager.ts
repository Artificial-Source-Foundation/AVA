/**
 * Session Manager
 *
 * Manages session state with LRU caching, persistence, and checkpoints.
 * Provides save/restore functionality and event notifications.
 *
 * Usage:
 * ```ts
 * const manager = new SessionManager({ maxSessions: 10 })
 *
 * // Create a new session
 * const session = await manager.create('My Session', '/path/to/project')
 *
 * // Update session state
 * await manager.addMessage(session.id, message)
 *
 * // Create checkpoint before risky operation
 * const checkpoint = await manager.createCheckpoint(session.id, 'Before refactor')
 *
 * // Rollback if needed
 * await manager.rollbackToCheckpoint(session.id, checkpoint.id)
 * ```
 */

import type { TokenStats } from '../context/tracker.js'
import type { Message } from '../context/types.js'
import type {
  Checkpoint,
  CheckpointMeta,
  FileState,
  SerializedSessionState,
  SessionEvent,
  SessionEventListener,
  SessionManagerConfig,
  SessionMeta,
  SessionState,
  SessionStorage,
} from './types.js'

// ============================================================================
// LRU Cache Implementation
// ============================================================================

/**
 * Simple LRU cache for sessions
 */
class LRUCache<T> {
  private cache = new Map<string, T>()
  private order: string[] = []

  constructor(private readonly maxSize: number) {}

  get(key: string): T | undefined {
    const value = this.cache.get(key)
    if (value !== undefined) {
      // Move to end (most recently used)
      this.order = this.order.filter((k) => k !== key)
      this.order.push(key)
    }
    return value
  }

  set(key: string, value: T): void {
    // Remove if exists
    if (this.cache.has(key)) {
      this.order = this.order.filter((k) => k !== key)
    }

    // Evict oldest if at capacity
    while (this.order.length >= this.maxSize) {
      const oldest = this.order.shift()
      if (oldest) {
        this.cache.delete(oldest)
      }
    }

    this.cache.set(key, value)
    this.order.push(key)
  }

  delete(key: string): boolean {
    this.order = this.order.filter((k) => k !== key)
    return this.cache.delete(key)
  }

  has(key: string): boolean {
    return this.cache.has(key)
  }

  keys(): string[] {
    return [...this.order]
  }

  values(): T[] {
    return this.order.map((k) => this.cache.get(k)!).filter(Boolean)
  }

  clear(): void {
    this.cache.clear()
    this.order = []
  }

  get size(): number {
    return this.cache.size
  }
}

// ============================================================================
// Session Manager
// ============================================================================

/**
 * Manages session state with LRU caching and persistence
 */
export class SessionManager {
  private sessions: LRUCache<SessionState>
  private storage: SessionStorage | null
  private autoSaveTimer: ReturnType<typeof setInterval> | null = null
  private listeners = new Set<SessionEventListener>()
  private dirtySessionIds = new Set<string>()

  constructor(config: SessionManagerConfig = {}) {
    const { maxSessions = 10, autoSaveInterval = 0, storage } = config

    this.sessions = new LRUCache<SessionState>(maxSessions)
    this.storage = storage ?? null

    // Set up auto-save if configured
    if (autoSaveInterval > 0 && storage) {
      this.autoSaveTimer = setInterval(() => {
        void this.saveAllDirty()
      }, autoSaveInterval)
    }
  }

  // ==========================================================================
  // Session Lifecycle
  // ==========================================================================

  /**
   * Create a new session
   */
  async create(name: string | undefined, workingDirectory: string): Promise<SessionState> {
    const now = Date.now()
    const id = `session-${now}-${Math.random().toString(36).slice(2, 8)}`

    const session: SessionState = {
      id,
      name,
      messages: [],
      workingDirectory,
      toolCallCount: 0,
      tokenStats: {
        messages: new Map(),
        total: 0,
        limit: 200000, // Default Claude limit
        remaining: 200000,
        percentUsed: 0,
      },
      openFiles: new Map(),
      env: {},
      createdAt: now,
      updatedAt: now,
      status: 'active',
    }

    this.sessions.set(id, session)
    this.markDirty(id)
    await this.maybePersist(id)

    return session
  }

  /**
   * Get a session by ID
   */
  async get(sessionId: string): Promise<SessionState | null> {
    // Check cache first
    let session = this.sessions.get(sessionId)

    if (!session && this.storage) {
      // Try loading from storage
      const serialized = await this.storage.load(sessionId)
      if (serialized) {
        session = this.deserialize(serialized)
        this.sessions.set(sessionId, session)
        this.emit({ type: 'session_loaded', sessionId })
      }
    }

    return session ?? null
  }

  /**
   * Save a session to persistent storage
   */
  async save(sessionId: string): Promise<void> {
    const session = this.sessions.get(sessionId)
    if (!session) return

    if (this.storage) {
      const serialized = this.serialize(session)
      await this.storage.save(serialized)
      this.dirtySessionIds.delete(sessionId)
      this.emit({ type: 'session_saved', sessionId })
    }
  }

  /**
   * Delete a session
   */
  async delete(sessionId: string): Promise<void> {
    this.sessions.delete(sessionId)
    this.dirtySessionIds.delete(sessionId)

    if (this.storage) {
      await this.storage.delete(sessionId)
    }

    this.emit({ type: 'session_cleared', sessionId })
  }

  /**
   * List all sessions (metadata only)
   */
  async list(): Promise<SessionMeta[]> {
    const metas: SessionMeta[] = []

    // Get from cache
    for (const session of this.sessions.values()) {
      metas.push(this.toMeta(session))
    }

    // Get from storage
    if (this.storage) {
      const storedIds = await this.storage.list()
      for (const id of storedIds) {
        if (!this.sessions.has(id)) {
          const serialized = await this.storage.load(id)
          if (serialized) {
            metas.push({
              id: serialized.id,
              name: serialized.name,
              messageCount: serialized.messages.length,
              workingDirectory: serialized.workingDirectory,
              createdAt: serialized.createdAt,
              updatedAt: serialized.updatedAt,
              status: serialized.status,
            })
          }
        }
      }
    }

    // Sort by updatedAt (most recent first)
    return metas.sort((a, b) => b.updatedAt - a.updatedAt)
  }

  // ==========================================================================
  // Message Management
  // ==========================================================================

  /**
   * Add a message to a session
   */
  addMessage(sessionId: string, message: Message): void {
    const session = this.sessions.get(sessionId)
    if (!session) {
      throw new Error(`Session not found: ${sessionId}`)
    }

    session.messages.push(message)
    session.updatedAt = Date.now()
    this.markDirty(sessionId)
    this.emit({ type: 'message_added', messageId: message.id, sessionId })
  }

  /**
   * Remove a message from a session
   */
  removeMessage(sessionId: string, messageId: string): boolean {
    const session = this.sessions.get(sessionId)
    if (!session) return false

    const index = session.messages.findIndex((m) => m.id === messageId)
    if (index === -1) return false

    session.messages.splice(index, 1)
    session.updatedAt = Date.now()
    this.markDirty(sessionId)
    this.emit({ type: 'message_removed', messageId, sessionId })
    return true
  }

  /**
   * Update session messages (e.g., after compaction)
   */
  setMessages(sessionId: string, messages: Message[]): void {
    const session = this.sessions.get(sessionId)
    if (!session) {
      throw new Error(`Session not found: ${sessionId}`)
    }

    session.messages = messages
    session.updatedAt = Date.now()
    this.markDirty(sessionId)
  }

  // ==========================================================================
  // Session State Updates
  // ==========================================================================

  /**
   * Update token statistics
   */
  updateTokenStats(sessionId: string, stats: TokenStats): void {
    const session = this.sessions.get(sessionId)
    if (session) {
      session.tokenStats = stats
      session.updatedAt = Date.now()
      this.markDirty(sessionId)
    }
  }

  /**
   * Track an open file
   */
  trackFile(sessionId: string, file: FileState): void {
    const session = this.sessions.get(sessionId)
    if (session) {
      session.openFiles.set(file.path, file)
      session.updatedAt = Date.now()
      this.markDirty(sessionId)
    }
  }

  /**
   * Untrack a file
   */
  untrackFile(sessionId: string, path: string): void {
    const session = this.sessions.get(sessionId)
    if (session) {
      session.openFiles.delete(path)
      session.updatedAt = Date.now()
      this.markDirty(sessionId)
    }
  }

  /**
   * Increment tool call count
   */
  incrementToolCalls(sessionId: string): void {
    const session = this.sessions.get(sessionId)
    if (session) {
      session.toolCallCount++
      session.updatedAt = Date.now()
      this.markDirty(sessionId)
    }
  }

  /**
   * Set session status
   */
  setStatus(sessionId: string, status: SessionState['status'], errorMessage?: string): void {
    const session = this.sessions.get(sessionId)
    if (session) {
      session.status = status
      session.errorMessage = errorMessage
      session.updatedAt = Date.now()
      this.markDirty(sessionId)
      this.emit({ type: 'status_changed', status, sessionId })
    }
  }

  /**
   * Set environment variable
   */
  setEnv(sessionId: string, key: string, value: string): void {
    const session = this.sessions.get(sessionId)
    if (session) {
      session.env[key] = value
      session.updatedAt = Date.now()
      this.markDirty(sessionId)
    }
  }

  // ==========================================================================
  // Checkpoints
  // ==========================================================================

  /**
   * Create a checkpoint for a session
   */
  async createCheckpoint(sessionId: string, description: string): Promise<Checkpoint> {
    const session = this.sessions.get(sessionId)
    if (!session) {
      throw new Error(`Session not found: ${sessionId}`)
    }

    const checkpoint: Checkpoint = {
      id: `checkpoint-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
      timestamp: Date.now(),
      description,
      messageCount: session.messages.length,
      stateSnapshot: JSON.stringify(this.serialize(session)),
    }

    // Update session with checkpoint
    session.checkpoint = checkpoint
    session.checkpointIds = session.checkpointIds ?? []
    session.checkpointIds.push(checkpoint.id)
    session.updatedAt = Date.now()
    this.markDirty(sessionId)

    // Persist checkpoint if storage available
    if (this.storage) {
      await this.storage.saveCheckpoint(sessionId, checkpoint)
    }

    this.emit({ type: 'checkpoint_created', checkpointId: checkpoint.id, sessionId })
    return checkpoint
  }

  /**
   * Rollback session to a checkpoint
   */
  async rollbackToCheckpoint(sessionId: string, checkpointId: string): Promise<void> {
    const session = this.sessions.get(sessionId)
    if (!session) {
      throw new Error(`Session not found: ${sessionId}`)
    }

    // Find checkpoint
    let checkpoint: Checkpoint | null = null

    // Check if it's the current checkpoint
    if (session.checkpoint?.id === checkpointId) {
      checkpoint = session.checkpoint
    } else if (this.storage) {
      // Load from storage
      checkpoint = await this.storage.loadCheckpoint(sessionId, checkpointId)
    }

    if (!checkpoint || !checkpoint.stateSnapshot) {
      throw new Error(`Checkpoint not found or has no snapshot: ${checkpointId}`)
    }

    // Restore state
    const serialized = JSON.parse(checkpoint.stateSnapshot) as SerializedSessionState
    const restored = this.deserialize(serialized)

    // Update session in place
    session.messages = restored.messages
    session.tokenStats = restored.tokenStats
    session.openFiles = restored.openFiles
    session.env = restored.env
    session.toolCallCount = restored.toolCallCount
    session.updatedAt = Date.now()

    this.markDirty(sessionId)
    this.emit({ type: 'checkpoint_restored', checkpointId, sessionId })
  }

  /**
   * List checkpoints for a session
   */
  async listCheckpoints(sessionId: string): Promise<CheckpointMeta[]> {
    if (this.storage) {
      return this.storage.listCheckpoints(sessionId)
    }

    const session = this.sessions.get(sessionId)
    if (!session?.checkpoint) return []

    return [
      {
        id: session.checkpoint.id,
        timestamp: session.checkpoint.timestamp,
        description: session.checkpoint.description,
        messageCount: session.checkpoint.messageCount,
      },
    ]
  }

  /**
   * Delete a checkpoint
   */
  async deleteCheckpoint(sessionId: string, checkpointId: string): Promise<void> {
    const session = this.sessions.get(sessionId)
    if (session) {
      if (session.checkpoint?.id === checkpointId) {
        session.checkpoint = undefined
      }
      session.checkpointIds = session.checkpointIds?.filter((id) => id !== checkpointId)
      this.markDirty(sessionId)
    }

    if (this.storage) {
      await this.storage.deleteCheckpoint(sessionId, checkpointId)
    }
  }

  // ==========================================================================
  // Event System
  // ==========================================================================

  /**
   * Subscribe to session events
   */
  on(listener: SessionEventListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  /**
   * Emit an event to all listeners
   */
  private emit(event: SessionEvent): void {
    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch (err) {
        console.warn('Session event listener error:', err)
      }
    }
  }

  // ==========================================================================
  // Cleanup
  // ==========================================================================

  /**
   * Dispose of the session manager
   */
  async dispose(): Promise<void> {
    // Clear auto-save timer
    if (this.autoSaveTimer) {
      clearInterval(this.autoSaveTimer)
      this.autoSaveTimer = null
    }

    // Save all dirty sessions
    await this.saveAllDirty()

    // Clear cache and listeners
    this.sessions.clear()
    this.listeners.clear()
  }

  // ==========================================================================
  // Helpers
  // ==========================================================================

  private markDirty(sessionId: string): void {
    this.dirtySessionIds.add(sessionId)
  }

  private async maybePersist(sessionId: string): Promise<void> {
    // Only auto-persist if no auto-save timer (immediate save)
    if (!this.autoSaveTimer && this.storage) {
      await this.save(sessionId)
    }
  }

  private async saveAllDirty(): Promise<void> {
    for (const sessionId of this.dirtySessionIds) {
      await this.save(sessionId)
    }
  }

  private toMeta(session: SessionState): SessionMeta {
    return {
      id: session.id,
      name: session.name,
      messageCount: session.messages.length,
      workingDirectory: session.workingDirectory,
      createdAt: session.createdAt,
      updatedAt: session.updatedAt,
      status: session.status,
    }
  }

  private serialize(session: SessionState): SerializedSessionState {
    return {
      id: session.id,
      name: session.name,
      messages: session.messages,
      workingDirectory: session.workingDirectory,
      toolCallCount: session.toolCallCount,
      tokenStats: {
        messages: Array.from(session.tokenStats.messages.entries()),
        total: session.tokenStats.total,
        limit: session.tokenStats.limit,
        remaining: session.tokenStats.remaining,
        percentUsed: session.tokenStats.percentUsed,
      },
      openFiles: Array.from(session.openFiles.entries()),
      env: session.env,
      checkpoint: session.checkpoint,
      checkpointIds: session.checkpointIds,
      createdAt: session.createdAt,
      updatedAt: session.updatedAt,
      status: session.status,
      errorMessage: session.errorMessage,
    }
  }

  private deserialize(serialized: SerializedSessionState): SessionState {
    return {
      id: serialized.id,
      name: serialized.name,
      messages: serialized.messages,
      workingDirectory: serialized.workingDirectory,
      toolCallCount: serialized.toolCallCount,
      tokenStats: {
        messages: new Map(serialized.tokenStats.messages),
        total: serialized.tokenStats.total,
        limit: serialized.tokenStats.limit,
        remaining: serialized.tokenStats.remaining,
        percentUsed: serialized.tokenStats.percentUsed,
      },
      openFiles: new Map(serialized.openFiles),
      env: serialized.env,
      checkpoint: serialized.checkpoint,
      checkpointIds: serialized.checkpointIds,
      createdAt: serialized.createdAt,
      updatedAt: serialized.updatedAt,
      status: serialized.status,
      errorMessage: serialized.errorMessage,
    }
  }
}

// ============================================================================
// Factory Function
// ============================================================================

/**
 * Create a session manager with default configuration
 */
export function createSessionManager(config: SessionManagerConfig = {}): SessionManager {
  return new SessionManager(config)
}
