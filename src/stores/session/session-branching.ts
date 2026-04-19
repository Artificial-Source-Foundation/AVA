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
import {
  branchSessionAtMessageInWebMode,
  cloneSessionInWebMode,
} from '../../services/web-session-mutations'
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

// ============================================================================
// Helpers
// ============================================================================

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

  if (!isTauri()) {
    const clone = await cloneSessionInWebMode({
      kind: 'duplicate',
      sourceSessionId,
      sourceSessionName: source.name,
      projectId,
    })
    return activateClonedSession(clone.session, clone.stats, projectId)
  }

  const sourceMessages = await getMessages(sourceSessionId)
  const newName = `${source.name} (copy)`
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

  if (!isTauri()) {
    const clone = await cloneSessionInWebMode({
      kind: 'fork',
      sourceSessionId,
      sourceSessionName: source.name,
      projectId,
      name,
    })
    return activateClonedSession(clone.session, clone.stats, projectId)
  }

  const forkName = name || `${source.name} (fork)`

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

  if (!isTauri()) {
    await branchSessionAtMessageInWebMode({ sessionId: session.id, messageId })
    return
  }

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
