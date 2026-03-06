/**
 * Session Lifecycle Actions
 * Create, switch, archive, delete, duplicate, fork, and branch sessions.
 */

import { createMemo } from 'solid-js'
import { DEFAULTS, STORAGE_KEYS } from '../../config/constants'
import { notifySessionOpened } from '../../services/core-bridge'
import {
  archiveSession as dbArchiveSession,
  createSession as dbCreateSession,
  deleteSession as dbDeleteSession,
  duplicateSessionMessages as dbDuplicateSessionMessages,
  getArchivedSessions as dbGetArchivedSessions,
  insertMessages as dbInsertMessages,
  updateSession as dbUpdateSession,
  getAgents,
  getCheckpoints,
  getFileOperations,
  getMemoryItems,
  getMessages,
  getSessionsWithStats,
  getTerminalExecutions,
} from '../../services/database'
import { logDebug, logError, logInfo, logWarn } from '../../services/logger'
import type { Session, SessionWithStats } from '../../types'
import { useProject } from '../project'
import { getLastSessionForProject, setLastSessionForProject } from '../session-persistence'
import {
  archivedSessions,
  currentSession,
  messages,
  sessions,
  setAgents,
  setArchivedSessions,
  setCheckpoints,
  setCurrentSession,
  setEditingMessageId,
  setFileOperations,
  setIsLoadingMessages,
  setIsLoadingSessions,
  setMemoryItems,
  setMessages,
  setRetryingMessageId,
  setSessions,
  setTerminalExecutions,
} from './session-state'

// ============================================================================
// Session Tree
// ============================================================================

export const getSessionTree = createMemo(() => {
  const all = sessions()
  const childMap = new Map<string, SessionWithStats[]>()
  const roots: SessionWithStats[] = []

  for (const s of all) {
    if (s.parentSessionId) {
      const siblings = childMap.get(s.parentSessionId) ?? []
      siblings.push(s)
      childMap.set(s.parentSessionId, siblings)
    } else {
      roots.push(s)
    }
  }

  return { roots, childMap }
})

// ============================================================================
// Session List Management
// ============================================================================

export async function loadSessionsForCurrentProject(): Promise<void> {
  const { currentProject } = useProject()
  const projectId = currentProject()?.id

  setIsLoadingSessions(true)
  try {
    const dbSessions = await getSessionsWithStats(projectId)
    setSessions(dbSessions)
    logDebug('session', 'Loaded sessions', { count: dbSessions.length })
  } catch (err) {
    logError('Session', 'Failed to load sessions', err)
    setSessions([])
  } finally {
    setIsLoadingSessions(false)
  }
}

export async function restoreForCurrentProject(): Promise<void> {
  const { currentProject } = useProject()
  const projectId = currentProject()?.id
  const projectSessions = sessions()

  if (projectSessions.length === 0) {
    await createNewSession()
    return
  }

  const lastProjectSessionId = getLastSessionForProject(projectId)
  const globalLastSessionId = localStorage.getItem(STORAGE_KEYS.LAST_SESSION)
  const restoreTarget =
    projectSessions.find((session) => session.id === lastProjectSessionId) ||
    projectSessions.find((session) => session.id === globalLastSessionId) ||
    projectSessions[0]

  if (!restoreTarget) {
    await createNewSession()
    return
  }

  await switchSession(restoreTarget.id)
}

export async function createNewSession(name?: string): Promise<Session> {
  const { currentProject } = useProject()
  const project = currentProject()
  const projectId = project?.id

  const session = await dbCreateSession(name || DEFAULTS.SESSION_NAME, projectId)
  const sessionWithStats: SessionWithStats = {
    ...session,
    messageCount: 0,
    totalTokens: 0,
  }

  setSessions((prev) => [sessionWithStats, ...prev])
  setCurrentSession(session)
  setMessages([])
  setAgents([])

  logInfo('session', 'Session created', {
    id: session.id,
    name: session.name,
    project: project?.name ?? 'unknown',
  })

  const { currentProject: getProject } = useProject()
  const cwd = getProject()?.directory || '.'
  notifySessionOpened(session.id, cwd)

  localStorage.setItem(STORAGE_KEYS.LAST_SESSION, session.id)
  setLastSessionForProject(projectId, session.id)

  return session
}

export async function switchSession(id: string): Promise<void> {
  const fromSessionId = currentSession()?.id
  const session = sessions().find((s) => s.id === id)
  if (!session) {
    logWarn('session', 'Session not found', { id })
    return
  }

  setEditingMessageId(null)
  setRetryingMessageId(null)
  setCurrentSession(session)

  setIsLoadingMessages(true)
  try {
    const dbMessages = await getMessages(id)
    setMessages(dbMessages)
    logInfo('session', 'Session switched', {
      from: fromSessionId ?? 'none',
      to: id,
      messageCount: dbMessages.length,
    })
  } catch (err) {
    logError('Session', 'Failed to load messages', err)
    setMessages([])
  } finally {
    setIsLoadingMessages(false)
  }

  try {
    const [dbAgents, dbFileOps, dbTerminalExecs, dbMemItems, dbCheckpoints] = await Promise.all([
      getAgents(id),
      getFileOperations(id),
      getTerminalExecutions(id),
      getMemoryItems(id),
      getCheckpoints(id),
    ])
    setAgents(dbAgents)
    setFileOperations(dbFileOps)
    setTerminalExecutions(dbTerminalExecs)
    setMemoryItems(dbMemItems)
    setCheckpoints(dbCheckpoints)
  } catch (err) {
    logError('Session', 'Failed to load session data', err)
    setAgents([])
    setFileOperations([])
    setTerminalExecutions([])
    setMemoryItems([])
    setCheckpoints([])
  }

  const { currentProject: getProject } = useProject()
  const cwd = getProject()?.directory || '.'
  notifySessionOpened(id, cwd)

  localStorage.setItem(STORAGE_KEYS.LAST_SESSION, id)
  const { currentProject } = useProject()
  setLastSessionForProject(currentProject()?.id, id)
}

/** Helper: switch to most-recent or create new session after removal */
async function switchAfterRemoval(projectId: string | undefined): Promise<void> {
  const remaining = sessions()
  if (remaining.length > 0) {
    const mostRecent = remaining[0]
    setCurrentSession(mostRecent)
    setIsLoadingMessages(true)
    try {
      const dbMessages = await getMessages(mostRecent.id)
      setMessages(dbMessages)
    } catch {
      setMessages([])
    } finally {
      setIsLoadingMessages(false)
    }
    localStorage.setItem(STORAGE_KEYS.LAST_SESSION, mostRecent.id)
    setLastSessionForProject(projectId, mostRecent.id)
  } else {
    const newSession = await dbCreateSession(DEFAULTS.SESSION_NAME, projectId)
    const sessionWithStats: SessionWithStats = {
      ...newSession,
      messageCount: 0,
      totalTokens: 0,
    }
    setSessions([sessionWithStats])
    setCurrentSession(newSession)
    setMessages([])
    localStorage.setItem(STORAGE_KEYS.LAST_SESSION, newSession.id)
    setLastSessionForProject(projectId, newSession.id)
  }
}

export async function renameSession(id: string, newName: string): Promise<void> {
  const trimmedName = newName.trim()
  if (!trimmedName) return

  await dbUpdateSession(id, { name: trimmedName })

  setSessions((prev) =>
    prev.map((s) => (s.id === id ? { ...s, name: trimmedName, updatedAt: Date.now() } : s))
  )

  if (currentSession()?.id === id) {
    setCurrentSession((prev) =>
      prev ? { ...prev, name: trimmedName, updatedAt: Date.now() } : null
    )
  }

  logInfo('session', 'Session renamed', { id, name: trimmedName })
}

export async function archiveSession(id: string): Promise<void> {
  const { currentProject } = useProject()
  const projectId = currentProject()?.id

  await dbArchiveSession(id)
  setSessions((prev) => prev.filter((s) => s.id !== id))

  if (currentSession()?.id === id) {
    await switchAfterRemoval(projectId)
  }
}

export async function unarchiveSession(id: string): Promise<void> {
  await dbUpdateSession(id, { status: 'active' })

  const archived = archivedSessions().find((s) => s.id === id)
  if (archived) {
    const restored = { ...archived, status: 'active' as const }
    setArchivedSessions((prev) => prev.filter((s) => s.id !== id))
    setSessions((prev) => [restored, ...prev])
  }
}

export async function loadArchivedSessions(): Promise<void> {
  const { currentProject } = useProject()
  const projectId = currentProject()?.id
  try {
    const archived = await dbGetArchivedSessions(projectId)
    setArchivedSessions(archived)
  } catch (err) {
    logError('Session', 'Failed to load archived sessions', err)
    setArchivedSessions([])
  }
}

export async function updateSessionSlug(id: string, slug: string): Promise<void> {
  await dbUpdateSession(id, { slug })
  setSessions((prev) => prev.map((s) => (s.id === id ? { ...s, slug } : s)))
  if (currentSession()?.id === id) {
    setCurrentSession((prev) => (prev ? { ...prev, slug } : null))
  }
}

export async function deleteSessionPermanently(id: string): Promise<void> {
  const { currentProject } = useProject()
  const projectId = currentProject()?.id

  await dbDeleteSession(id)
  setSessions((prev) => prev.filter((s) => s.id !== id))

  if (currentSession()?.id === id) {
    await switchAfterRemoval(projectId)
  }
}

export async function duplicateSession(sourceSessionId: string): Promise<void> {
  const source = sessions().find((s) => s.id === sourceSessionId)
  if (!source) return

  const { currentProject } = useProject()
  const projectId = currentProject()?.id

  const newSession = await dbCreateSession(`${source.name} (copy)`, projectId)
  await dbDuplicateSessionMessages(sourceSessionId, newSession.id)

  const sessionWithStats: SessionWithStats = {
    ...newSession,
    messageCount: source.messageCount,
    totalTokens: source.totalTokens,
    lastPreview: source.lastPreview,
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

export async function forkSession(sourceSessionId: string, name?: string): Promise<void> {
  const source = sessions().find((s) => s.id === sourceSessionId)
  if (!source) return

  const { currentProject } = useProject()
  const projectId = currentProject()?.id

  const forkName = name || `${source.name} (fork)`
  const newSession = await dbCreateSession(forkName, projectId, sourceSessionId)
  await dbDuplicateSessionMessages(sourceSessionId, newSession.id)

  const sessionWithStats: SessionWithStats = {
    ...newSession,
    messageCount: source.messageCount,
    totalTokens: source.totalTokens,
    lastPreview: source.lastPreview,
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

export function updateSessionStats(
  sessionId: string,
  deltaMessages: number,
  deltaTokens: number
): void {
  setSessions((prev) =>
    prev.map((s) =>
      s.id === sessionId
        ? {
            ...s,
            messageCount: s.messageCount + deltaMessages,
            totalTokens: s.totalTokens + deltaTokens,
            updatedAt: Date.now(),
          }
        : s
    )
  )
}
