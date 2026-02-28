/**
 * Simplified session manager — CRUD, auto-save.
 *
 * No checkpoints, no forking — those can be added by extensions.
 */

import type { ChatMessage } from '../llm/types.js'
import { createLogger } from '../logger/logger.js'
import type { SessionStorage } from './storage.js'
import type {
  FileState,
  SessionEvent,
  SessionEventListener,
  SessionManagerConfig,
  SessionMeta,
  SessionState,
  TokenStats,
} from './types.js'

const log = createLogger('Session')

export class SessionManager {
  private sessions = new Map<string, SessionState>()
  private listeners = new Set<SessionEventListener>()
  private config: Required<Omit<SessionManagerConfig, 'storage'>>
  private storage: SessionStorage | null
  private autoSaveTimer: ReturnType<typeof setInterval> | null = null

  constructor(config?: SessionManagerConfig) {
    this.config = {
      maxSessions: config?.maxSessions ?? 50,
      autoSaveInterval: config?.autoSaveInterval ?? 30_000,
    }
    this.storage = config?.storage ?? null
  }

  // ─── CRUD ──────────────────────────────────────────────────────────────

  create(name: string | undefined, workingDirectory: string): SessionState {
    const id = crypto.randomUUID()
    const now = Date.now()
    const session: SessionState = {
      id,
      name,
      messages: [],
      workingDirectory,
      toolCallCount: 0,
      tokenStats: { inputTokens: 0, outputTokens: 0, messages: new Map() },
      openFiles: new Map(),
      env: {},
      createdAt: now,
      updatedAt: now,
      status: 'active',
    }

    // Evict oldest if at capacity
    if (this.sessions.size >= this.config.maxSessions) {
      const oldest = this.getOldestSessionId()
      if (oldest) this.sessions.delete(oldest)
    }

    this.sessions.set(id, session)
    log.debug(`Session created: ${id}`, { name })
    return session
  }

  get(sessionId: string): SessionState | null {
    return this.sessions.get(sessionId) ?? null
  }

  delete(sessionId: string): boolean {
    const deleted = this.sessions.delete(sessionId)
    if (deleted) {
      this.emit({ type: 'session_cleared', sessionId })
    }
    return deleted
  }

  list(): SessionMeta[] {
    return [...this.sessions.values()].map((s) => ({
      id: s.id,
      name: s.name,
      messageCount: s.messages.length,
      workingDirectory: s.workingDirectory,
      createdAt: s.createdAt,
      updatedAt: s.updatedAt,
      status: s.status,
    }))
  }

  // ─── Messages ──────────────────────────────────────────────────────────

  addMessage(sessionId: string, message: ChatMessage): void {
    const session = this.getOrThrow(sessionId)
    session.messages.push(message)
    session.updatedAt = Date.now()
    this.emit({ type: 'message_added', messageId: String(session.messages.length - 1), sessionId })
  }

  setMessages(sessionId: string, messages: ChatMessage[]): void {
    const session = this.getOrThrow(sessionId)
    session.messages = messages
    session.updatedAt = Date.now()
  }

  // ─── State Updates ─────────────────────────────────────────────────────

  updateTokenStats(sessionId: string, stats: Partial<TokenStats>): void {
    const session = this.getOrThrow(sessionId)
    if (stats.inputTokens !== undefined) session.tokenStats.inputTokens = stats.inputTokens
    if (stats.outputTokens !== undefined) session.tokenStats.outputTokens = stats.outputTokens
    session.updatedAt = Date.now()
  }

  trackFile(sessionId: string, file: FileState): void {
    const session = this.getOrThrow(sessionId)
    session.openFiles.set(file.path, file)
  }

  untrackFile(sessionId: string, path: string): void {
    const session = this.getOrThrow(sessionId)
    session.openFiles.delete(path)
  }

  incrementToolCalls(sessionId: string): void {
    const session = this.getOrThrow(sessionId)
    session.toolCallCount++
  }

  setStatus(sessionId: string, status: SessionState['status'], errorMessage?: string): void {
    const session = this.getOrThrow(sessionId)
    session.status = status
    session.errorMessage = errorMessage
    session.updatedAt = Date.now()
    this.emit({ type: 'status_changed', status, sessionId })
  }

  setEnv(sessionId: string, key: string, value: string): void {
    const session = this.getOrThrow(sessionId)
    session.env[key] = value
  }

  // ─── Events ──────────────────────────────────────────────────────────────

  on(listener: SessionEventListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  // ─── Internal ──────────────────────────────────────────────────────────

  private getOrThrow(sessionId: string): SessionState {
    const session = this.sessions.get(sessionId)
    if (!session) throw new Error(`Session not found: ${sessionId}`)
    return session
  }

  private getOldestSessionId(): string | null {
    let oldest: string | null = null
    let oldestTime = Infinity
    for (const [id, session] of this.sessions) {
      if (session.updatedAt < oldestTime) {
        oldestTime = session.updatedAt
        oldest = id
      }
    }
    return oldest
  }

  private emit(event: SessionEvent): void {
    for (const listener of this.listeners) {
      listener(event)
    }
  }

  get size(): number {
    return this.sessions.size
  }

  // ─── Storage ────────────────────────────────────────────────────────────

  /** Persist a session to storage (no-op if no storage configured). */
  async save(sessionId: string): Promise<void> {
    if (!this.storage) return
    const session = this.sessions.get(sessionId)
    if (!session) return
    await this.storage.save(session)
    this.emit({ type: 'session_saved', sessionId })
    log.debug(`Session saved: ${sessionId}`)
  }

  /** Load a session from storage into memory. */
  async loadSession(sessionId: string): Promise<SessionState | null> {
    if (!this.storage) return null
    const session = await this.storage.load(sessionId)
    if (session) {
      this.sessions.set(session.id, session)
      this.emit({ type: 'session_loaded', sessionId })
    }
    return session
  }

  /** Load all sessions from storage into memory. */
  async loadFromStorage(): Promise<number> {
    if (!this.storage) return 0
    const sessions = await this.storage.loadAll()
    for (const session of sessions) {
      this.sessions.set(session.id, session)
    }
    log.debug(`Loaded ${sessions.length} sessions from storage`)
    return sessions.length
  }

  /** Start periodic auto-save for all active sessions. */
  startAutoSave(): void {
    if (!this.storage || this.autoSaveTimer) return
    this.autoSaveTimer = setInterval(() => {
      for (const session of this.sessions.values()) {
        if (session.status === 'active') {
          this.storage!.save(session).catch((err) => {
            log.error(`Auto-save failed for ${session.id}: ${err}`)
          })
        }
      }
    }, this.config.autoSaveInterval)
  }

  dispose(): void {
    if (this.autoSaveTimer) {
      clearInterval(this.autoSaveTimer)
      this.autoSaveTimer = null
    }
    this.sessions.clear()
    this.listeners.clear()
  }
}

// ─── Factory ─────────────────────────────────────────────────────────────────

export function createSessionManager(config?: SessionManagerConfig): SessionManager {
  return new SessionManager(config)
}
