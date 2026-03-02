/**
 * Session types — state, checkpoints, events.
 */

import type { ChatMessage } from '../llm/types.js'

// ─── Session State ───────────────────────────────────────────────────────────

export type SessionStatus = 'active' | 'paused' | 'completed' | 'error' | 'busy' | 'archived'

export interface TokenStats {
  inputTokens: number
  outputTokens: number
  messages: Map<string, number>
}

export interface FileState {
  path: string
  content: string
  mtime: number
  dirty: boolean
  languageId?: string
  lineCount?: number
}

export interface SessionState {
  id: string
  name?: string
  slug?: string
  messages: ChatMessage[]
  workingDirectory: string
  toolCallCount: number
  tokenStats: TokenStats
  openFiles: Map<string, FileState>
  env: Record<string, string>
  createdAt: number
  updatedAt: number
  status: SessionStatus
  errorMessage?: string
  // DAG/branching fields
  parentSessionId?: string
  branchName?: string
  branchPoint?: number // message index where branch was created
  children?: string[]
}

export interface SessionMeta {
  id: string
  name?: string
  slug?: string
  messageCount: number
  workingDirectory: string
  createdAt: number
  updatedAt: number
  status: SessionStatus
  parentSessionId?: string
  branchName?: string
  childCount?: number
}

// ─── Errors ─────────────────────────────────────────────────────────────────

export class SessionBusyError extends Error {
  constructor(sessionId: string) {
    super(`Session ${sessionId} is busy`)
    this.name = 'SessionBusyError'
  }
}

// ─── Events ──────────────────────────────────────────────────────────────────

export type SessionEvent =
  | { type: 'message_added'; messageId: string; sessionId: string }
  | { type: 'message_removed'; messageId: string; sessionId: string }
  | { type: 'session_saved'; sessionId: string }
  | { type: 'session_loaded'; sessionId: string }
  | { type: 'session_cleared'; sessionId: string }
  | { type: 'status_changed'; status: SessionStatus; sessionId: string }
  | { type: 'session:status'; sessionId: string; status: 'idle' | 'busy' | 'retry' }

export type SessionEventListener = (event: SessionEvent) => void

// ─── Config ──────────────────────────────────────────────────────────────────

export interface SessionManagerConfig {
  maxSessions?: number
  autoSaveInterval?: number
  storage?: import('./storage.js').SessionStorage
}
