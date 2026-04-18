import { createBoundedSessionCache } from '../../lib/bounded-session-cache'
import type { Agent, FileOperation, MemoryItem, Message, TerminalExecution } from '../../types'

export interface SessionCheckpointSnapshot {
  id: string
  timestamp: number
  description: string
  messageCount: number
}

export interface SessionArtifactSnapshot {
  messages: Message[]
  agents: Agent[]
  fileOps: FileOperation[]
  terminalExecutions: TerminalExecution[]
  memoryItems: MemoryItem[]
  checkpoints: SessionCheckpointSnapshot[]
}

const RECENT_SESSION_ARTIFACT_CACHE_LIMIT = 6

const sessionArtifactCache = createBoundedSessionCache<SessionArtifactSnapshot>(
  RECENT_SESSION_ARTIFACT_CACHE_LIMIT
)

export function cloneSessionArtifactSnapshot(
  snapshot: SessionArtifactSnapshot
): SessionArtifactSnapshot {
  return {
    messages: [...snapshot.messages],
    agents: [...snapshot.agents],
    fileOps: [...snapshot.fileOps],
    terminalExecutions: [...snapshot.terminalExecutions],
    memoryItems: [...snapshot.memoryItems],
    checkpoints: [...snapshot.checkpoints],
  }
}

export function cacheSessionArtifacts(sessionId: string, snapshot: SessionArtifactSnapshot): void {
  sessionArtifactCache.set(sessionId, cloneSessionArtifactSnapshot(snapshot))
}

export function getCachedSessionArtifacts(sessionId: string): SessionArtifactSnapshot | undefined {
  const snapshot = sessionArtifactCache.get(sessionId)
  return snapshot ? { ...snapshot } : undefined
}

export function updateCachedSessionArtifacts(
  sessionId: string,
  updater: (snapshot: SessionArtifactSnapshot) => SessionArtifactSnapshot
): void {
  const existing = sessionArtifactCache.peek(sessionId)
  if (!existing) {
    return
  }
  sessionArtifactCache.set(sessionId, cloneSessionArtifactSnapshot(updater(existing)))
}

export function deleteCachedSessionArtifacts(sessionId: string): void {
  sessionArtifactCache.delete(sessionId)
}

export function clearSessionArtifactCache(): void {
  sessionArtifactCache.clear()
}
