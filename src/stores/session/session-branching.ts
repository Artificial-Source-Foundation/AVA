/**
 * Session Branching Actions
 * Duplicate, fork, and branch sessions.
 */

import { isTauri } from '@tauri-apps/api/core'
import { STORAGE_KEYS } from '../../config/constants'
import {
  createSession as dbCreateSession,
  insertMessages as dbInsertMessages,
  getMessages,
} from '../../services/database'
import { logInfo } from '../../services/logger'
import type { Session, SessionWithStats } from '../../types'
import { useProject } from '../project'
import { setLastSessionForProject } from '../session-persistence'
import {
  currentSession,
  messages,
  sessions,
  setCurrentSession,
  setIsLoadingMessages,
  setMessages,
  setSessions,
} from './session-state'

const API_BASE = import.meta.env.VITE_API_URL || ''

// ============================================================================
// Helpers
// ============================================================================

/** Web-mode helper: call the backend duplicate endpoint which copies messages server-side. */
async function duplicateViaApi(
  sourceSessionId: string,
  name: string,
  projectId: string | undefined
): Promise<void> {
  const newId = crypto.randomUUID()
  const res = await fetch(`${API_BASE}/api/sessions/${sourceSessionId}/duplicate`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ name, id: newId }),
  })
  if (!res.ok) {
    console.error('[session] duplicate API failed:', res.status, await res.text())
    return
  }
  const data = (await res.json()) as { id: string; title: string; message_count: number }

  // Build a local Session object matching what dbCreateSession returns
  const now = Date.now()
  const newSession: Session = {
    id: data.id,
    name: data.title,
    projectId,
    parentSessionId: sourceSessionId,
    createdAt: now,
    updatedAt: now,
    status: 'active' as const,
    metadata: {},
  }

  await activateClonedSession(
    newSession,
    { messageCount: data.message_count, totalTokens: 0, lastPreview: '' },
    projectId
  )
}

/** After cloning a session, update signals and persist the new session as active. */
async function activateClonedSession(
  newSession: Session,
  stats: Pick<SessionWithStats, 'messageCount' | 'totalTokens' | 'lastPreview'>,
  projectId: string | undefined
): Promise<void> {
  const sessionWithStats: SessionWithStats = {
    ...newSession,
    messageCount: stats.messageCount,
    totalTokens: stats.totalTokens,
    lastPreview: stats.lastPreview,
  }
  setSessions((prev) => [sessionWithStats, ...prev])

  setCurrentSession(newSession)
  setIsLoadingMessages(true)
  try {
    const dbMessages = await getMessages(newSession.id)
    setMessages(dbMessages)
  } catch {
    setMessages([])
  } finally {
    setIsLoadingMessages(false)
  }
  localStorage.setItem(STORAGE_KEYS.LAST_SESSION, newSession.id)
  setLastSessionForProject(projectId, newSession.id)
}

// ============================================================================
// Public API
// ============================================================================

export async function duplicateSession(sourceSessionId: string): Promise<void> {
  const source = sessions().find((s) => s.id === sourceSessionId)
  if (!source) return

  const { currentProject } = useProject()
  const projectId = currentProject()?.id
  const newName = `${source.name} (copy)`

  if (!isTauri()) {
    // Web mode: use the dedicated backend API to duplicate with messages
    return duplicateViaApi(sourceSessionId, newName, projectId)
  }

  const sourceMessages = await getMessages(sourceSessionId)
  const newSession = await dbCreateSession(newName, projectId)

  if (sourceMessages.length > 0) {
    await dbInsertMessages(
      sourceMessages.map((m) => ({
        ...m,
        id: crypto.randomUUID(),
        sessionId: newSession.id,
      }))
    )
  }

  const totalTokens = sourceMessages.reduce((sum, m) => sum + (m.tokensUsed || 0), 0)
  const lastPreview =
    sourceMessages.length > 0
      ? sourceMessages[sourceMessages.length - 1]!.content.slice(0, 100)
      : source.lastPreview

  await activateClonedSession(
    newSession,
    {
      messageCount: sourceMessages.length,
      totalTokens,
      lastPreview: lastPreview || '',
    },
    projectId
  )
}

export async function forkSession(sourceSessionId: string, name?: string): Promise<void> {
  const source = sessions().find((s) => s.id === sourceSessionId)
  if (!source) return

  const { currentProject } = useProject()
  const projectId = currentProject()?.id
  const forkName = name || `${source.name} (fork)`

  if (!isTauri()) {
    // Web mode: use the dedicated backend API to fork with messages
    return duplicateViaApi(sourceSessionId, forkName, projectId)
  }

  const sourceMessages = await getMessages(sourceSessionId)
  const newSession = await dbCreateSession(forkName, projectId, sourceSessionId)

  if (sourceMessages.length > 0) {
    await dbInsertMessages(
      sourceMessages.map((m) => ({
        ...m,
        id: crypto.randomUUID(),
        sessionId: newSession.id,
      }))
    )
  }

  const totalTokens = sourceMessages.reduce((sum, m) => sum + (m.tokensUsed || 0), 0)
  const lastPreview =
    sourceMessages.length > 0
      ? sourceMessages[sourceMessages.length - 1]!.content.slice(0, 100)
      : source.lastPreview

  await activateClonedSession(
    newSession,
    {
      messageCount: sourceMessages.length,
      totalTokens,
      lastPreview: lastPreview || '',
    },
    projectId
  )
}

export async function branchAtMessage(messageId: string): Promise<void> {
  const session = currentSession()
  if (!session) return

  const msgs = messages()
  const index = msgs.findIndex((m) => m.id === messageId)
  if (index === -1) return

  const { currentProject } = useProject()
  const projectId = currentProject()?.id

  const messagesToCopy = msgs.slice(0, index + 1)
  const branchName = `${session.name} (branch)`
  const newSession = await dbCreateSession(branchName, projectId, session.id)

  await dbInsertMessages(messagesToCopy.map((m) => ({ ...m, sessionId: newSession.id })))

  const totalTokens = messagesToCopy.reduce((sum, m) => sum + (m.tokensUsed || 0), 0)
  const sessionWithStats: SessionWithStats = {
    ...newSession,
    messageCount: messagesToCopy.length,
    totalTokens,
    lastPreview: messagesToCopy[messagesToCopy.length - 1]?.content.slice(0, 100) || '',
  }
  setSessions((prev) => [sessionWithStats, ...prev])

  setCurrentSession(newSession)
  setMessages(messagesToCopy.map((m) => ({ ...m, sessionId: newSession.id })))
  localStorage.setItem(STORAGE_KEYS.LAST_SESSION, newSession.id)
  setLastSessionForProject(projectId, newSession.id)

  logInfo('Session', `Branched at message ${index + 1}/${msgs.length} → ${newSession.id}`)
}
