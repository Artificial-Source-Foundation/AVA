/**
 * Session storage interface — abstracts persistence backend.
 *
 * Implementations: MemorySessionStorage (default), SqliteSessionStorage.
 */

import type { SessionState } from './types.js'

/** Serializable session (Maps converted to plain objects). */
export interface SerializedSession {
  id: string
  name?: string
  messages: unknown[]
  workingDirectory: string
  toolCallCount: number
  tokenStats: {
    inputTokens: number
    outputTokens: number
    messages: Record<string, number>
  }
  openFiles: Record<string, unknown>
  env: Record<string, string>
  createdAt: number
  updatedAt: number
  status: string
  errorMessage?: string
}

export interface SessionStorage {
  save(session: SessionState): Promise<void>
  load(id: string): Promise<SessionState | null>
  delete(id: string): Promise<boolean>
  list(): Promise<Array<{ id: string; name?: string; updatedAt: number }>>
  loadAll(): Promise<SessionState[]>
}

/** Convert SessionState (with Maps) to a JSON-serializable form. */
export function serializeSession(session: SessionState): SerializedSession {
  return {
    id: session.id,
    name: session.name,
    messages: session.messages,
    workingDirectory: session.workingDirectory,
    toolCallCount: session.toolCallCount,
    tokenStats: {
      inputTokens: session.tokenStats.inputTokens,
      outputTokens: session.tokenStats.outputTokens,
      messages: Object.fromEntries(session.tokenStats.messages),
    },
    openFiles: Object.fromEntries([...session.openFiles.entries()].map(([k, v]) => [k, v])),
    env: session.env,
    createdAt: session.createdAt,
    updatedAt: session.updatedAt,
    status: session.status,
    errorMessage: session.errorMessage,
  }
}

/** Convert serialized form back to SessionState (with Maps). */
export function deserializeSession(data: SerializedSession): SessionState {
  return {
    id: data.id,
    name: data.name,
    messages: data.messages as SessionState['messages'],
    workingDirectory: data.workingDirectory,
    toolCallCount: data.toolCallCount,
    tokenStats: {
      inputTokens: data.tokenStats.inputTokens,
      outputTokens: data.tokenStats.outputTokens,
      messages: new Map(Object.entries(data.tokenStats.messages)),
    },
    openFiles: new Map(Object.entries(data.openFiles)) as SessionState['openFiles'],
    env: data.env,
    createdAt: data.createdAt,
    updatedAt: data.updatedAt,
    status: data.status as SessionState['status'],
    errorMessage: data.errorMessage,
  }
}
