/**
 * Session Lifecycle Actions
 * Create, switch, archive, delete sessions; stats updates.
 * Branching operations (duplicate/fork/branch) are in session-branching.ts.
 */

import { isTauri } from '@tauri-apps/api/core'
import { batch, createMemo } from 'solid-js'
import { DEFAULTS, STORAGE_KEYS } from '../../config/constants'
import { clearTodos } from '../../hooks/use-rust-agent'
import { log } from '../../lib/logger'
import { normalizeToolCalls } from '../../lib/tool-call-state'
import {
  clearSessionNeedsAuthoritativeRecovery,
  markSessionNeedsAuthoritativeRecovery,
  notifySessionOpened,
  sessionNeedsAuthoritativeRecovery,
} from '../../services/core-bridge'
import {
  archiveSession as dbArchiveSession,
  createSession as dbCreateSession,
  deleteSession as dbDeleteSession,
  getArchivedSessions as dbGetArchivedSessions,
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
import { rustBackend } from '../../services/rust-bridge'
import { unregisterBackendSessionId } from '../../services/web-session-identity'
import type { Message, Session, SessionWithStats, ToolCall } from '../../types'
import type { ActiveSessionSyncSnapshot } from '../../types/rust-ipc'
import { useProject } from '../project'
import { getLastSessionForProject, setLastSessionForProject } from '../session-persistence'
import { createLatestRequestGate } from './request-gate'
import {
  cacheSessionArtifacts,
  deleteCachedSessionArtifacts,
  getCachedSessionArtifacts,
} from './session-artifact-cache'
import { replaceMessagesFromBackendForSession } from './session-messages'
import {
  agents,
  archivedSessions,
  checkpoints,
  currentSession,
  fileOperations,
  memoryItems,
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
  terminalExecutions,
} from './session-state'

// Re-export branching operations so existing `import * as lifecycle` still works
export {
  branchAtMessage,
  canBranchAtMessage,
  duplicateSession,
  forkSession,
} from './session-branching'

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

const sessionListGate = createLatestRequestGate()
const sessionSwitchGate = createLatestRequestGate()
const createSessionInFlightByKey = new Map<string, Promise<Session>>()

function asRecord(value: unknown): Record<string, unknown> | null {
  return value && typeof value === 'object' && !Array.isArray(value)
    ? (value as Record<string, unknown>)
    : null
}

function asString(value: unknown): string | undefined {
  return typeof value === 'string' ? value : undefined
}

function parseSessionMessageRole(value: unknown): Message['role'] | null {
  const normalized = asString(value)?.toLowerCase()
  if (normalized === 'user' || normalized === 'assistant' || normalized === 'system') {
    return normalized
  }
  return null
}

function parseSessionMessageImages(value: unknown): Message['images'] | undefined {
  if (!Array.isArray(value) || value.length === 0) {
    return undefined
  }

  const normalized = value
    .map((entry) => {
      const record = asRecord(entry)
      const data = asString(record?.data)
      const mimeType = asString(record?.mimeType) ?? asString(record?.media_type)
      const name = asString(record?.name)
      if (!record || !data || !mimeType) {
        return null
      }
      return {
        data,
        mimeType,
        ...(name ? { name } : {}),
      }
    })
    .filter((image): image is NonNullable<typeof image> => image !== null)

  return normalized.length > 0 ? normalized : undefined
}

function parseSessionMessageToolCalls(value: unknown): ToolCall[] | undefined {
  if (!Array.isArray(value) || value.length === 0) {
    return undefined
  }

  const normalized = normalizeToolCalls(value)

  return normalized && normalized.length > 0 ? normalized : undefined
}

function mapDesktopBackendSessionMessages(sessionId: string, rawSession: unknown): Message[] {
  const session = asRecord(rawSession)
  const rawMessages = Array.isArray(session?.messages) ? session.messages : []

  return rawMessages
    .map((rawMessage): Message | null => {
      const record = asRecord(rawMessage)
      const id = asString(record?.id)
      const role = parseSessionMessageRole(record?.role)
      if (!record || !id || !role) {
        return null
      }

      const metadata = asRecord(record.metadata) ?? undefined
      const toolCalls =
        parseSessionMessageToolCalls(record.tool_calls) ?? normalizeToolCalls(metadata?.toolCalls)
      const mergedMetadata =
        metadata || toolCalls
          ? {
              ...(metadata ?? {}),
              ...(toolCalls ? { toolCalls } : {}),
            }
          : undefined

      return {
        id,
        sessionId,
        role,
        content: asString(record.content) ?? '',
        createdAt:
          typeof record.timestamp === 'string'
            ? new Date(record.timestamp).getTime()
            : typeof record.created_at === 'number'
              ? record.created_at
              : Date.now(),
        ...(mergedMetadata ? { metadata: mergedMetadata } : {}),
        toolCalls,
        images: parseSessionMessageImages(record.images),
      }
    })
    .filter((message): message is Message => message !== null)
}

async function recoverDesktopSessionFromBackend(sessionId: string): Promise<Message[]> {
  const rawSession = await rustBackend.loadSession(sessionId)
  return mapDesktopBackendSessionMessages(sessionId, rawSession)
}

async function recoverWebSessionFromBackendIfNeeded(sessionId: string): Promise<boolean> {
  if (isTauri() || !sessionId || !sessionNeedsAuthoritativeRecovery(sessionId)) {
    return false
  }

  try {
    const authoritativeMessages = await getMessages(sessionId)
    if (authoritativeMessages.length === 0) {
      return false
    }

    await replaceMessagesFromBackendForSession(sessionId, authoritativeMessages)
    clearSessionNeedsAuthoritativeRecovery(sessionId)
    return true
  } catch (err) {
    markSessionNeedsAuthoritativeRecovery(sessionId)
    log.warn('session', 'Failed to recover authoritative detached web session', {
      sessionId,
      error: err instanceof Error ? err.message : String(err),
    })
    return false
  }
}

export async function recoverDetachedDesktopSessionIfNeeded(sessionId: string): Promise<boolean> {
  if (!isTauri() || !sessionId || !sessionNeedsAuthoritativeRecovery(sessionId)) {
    return false
  }

  try {
    const status = await rustBackend.getAgentStatus({ sessionId })
    if (status.running) {
      return false
    }

    const authoritativeMessages = await recoverDesktopSessionFromBackend(sessionId)
    await replaceMessagesFromBackendForSession(sessionId, authoritativeMessages)
    clearSessionNeedsAuthoritativeRecovery(sessionId)
    return true
  } catch (err) {
    markSessionNeedsAuthoritativeRecovery(sessionId)
    log.warn('session', 'Failed to recover authoritative detached desktop session', {
      sessionId,
      error: err instanceof Error ? err.message : String(err),
    })
    return false
  }
}

interface SessionTransitionOptions {
  preserveActiveRun?: boolean
}

function resetSessionArtifacts(): void {
  batch(() => {
    setMessages([])
    setAgents([])
    setFileOperations([])
    setTerminalExecutions([])
    setMemoryItems([])
    setCheckpoints([])
    clearTodos()
  })
}

function cacheCurrentSessionArtifacts(sessionId: string): void {
  cacheSessionArtifacts(sessionId, {
    messages: messages(),
    agents: agents(),
    fileOps: fileOperations(),
    terminalExecutions: terminalExecutions(),
    memoryItems: memoryItems(),
    checkpoints: checkpoints(),
  })
}

function hydrateSessionArtifacts(result: {
  messages: Awaited<ReturnType<typeof getMessages>>
  agents: Awaited<ReturnType<typeof getAgents>>
  fileOps: Awaited<ReturnType<typeof getFileOperations>>
  terminalExecutions: Awaited<ReturnType<typeof getTerminalExecutions>>
  memoryItems: Awaited<ReturnType<typeof getMemoryItems>>
  checkpoints: Awaited<ReturnType<typeof getCheckpoints>>
}): void {
  batch(() => {
    setMessages(result.messages)
    setAgents(result.agents)
    setFileOperations(result.fileOps)
    setTerminalExecutions(result.terminalExecutions)
    setMemoryItems(result.memoryItems)
    setCheckpoints(result.checkpoints)
  })
}

function buildDesktopSessionSnapshot(
  session: Pick<Session, 'name'>,
  messages: Awaited<ReturnType<typeof getMessages>>
): ActiveSessionSyncSnapshot {
  return {
    title: session.name,
    messages: messages.map((message) => ({
      id: message.id,
      role: message.role,
      content: message.content,
      createdAt: message.createdAt,
      images: message.images?.map((image) => ({
        data: image.data,
        mediaType: image.mimeType,
      })),
    })),
  }
}

export async function loadSessionsForCurrentProject(): Promise<void> {
  const { currentProject } = useProject()
  const projectId = currentProject()?.id
  const requestToken = sessionListGate.begin()
  setIsLoadingSessions(true)
  try {
    const dbSessions = await getSessionsWithStats(projectId)
    if (!sessionListGate.isCurrent(requestToken) || currentProject()?.id !== projectId) {
      return
    }
    setSessions(dbSessions)
    logDebug('session', 'Loaded sessions', { count: dbSessions.length })
  } catch (err) {
    if (!sessionListGate.isCurrent(requestToken) || currentProject()?.id !== projectId) {
      return
    }
    logError('Session', 'Failed to load sessions', err)
    setSessions([])
  } finally {
    if (sessionListGate.isCurrent(requestToken)) {
      setIsLoadingSessions(false)
    }
  }
}

export async function restoreForCurrentProject(
  options: SessionTransitionOptions = {}
): Promise<void> {
  const { currentProject } = useProject()
  const projectId = currentProject()?.id
  const projectSessions = sessions()

  if (projectSessions.length === 0) {
    await createNewSession(undefined, undefined, options)
    return
  }

  const lastProjectSessionId = getLastSessionForProject(projectId)
  const globalLastSessionId = localStorage.getItem(STORAGE_KEYS.LAST_SESSION)
  const restoreTarget =
    projectSessions.find((session) => session.id === lastProjectSessionId) ||
    projectSessions.find((session) => session.id === globalLastSessionId) ||
    projectSessions[0]

  if (!restoreTarget) {
    await createNewSession(undefined, undefined, options)
    return
  }
  await switchSession(restoreTarget.id, options)
}

export async function createNewSession(
  name?: string,
  projectIdOverride?: string,
  _options: SessionTransitionOptions = {}
): Promise<Session> {
  const requestedName = name || DEFAULTS.SESSION_NAME
  const requestedProjectId = projectIdOverride ?? useProject().currentProject()?.id
  const requestKey = `${requestedProjectId ?? '<no-project>'}::${requestedName}`

  const inFlightForKey = createSessionInFlightByKey.get(requestKey)
  if (inFlightForKey) {
    return inFlightForKey
  }

  const existing = currentSession()
  const existingStillPresent =
    !!existing && sessions().some((session) => session.id === existing.id)
  if (
    existing &&
    existingStillPresent &&
    existing.name === DEFAULTS.SESSION_NAME &&
    requestedName === DEFAULTS.SESSION_NAME &&
    existing.projectId === requestedProjectId &&
    messages().length === 0
  ) {
    return existing
  }

  const createPromise = (async () => {
    const { currentProject } = useProject()
    const project = currentProject()
    const projectId = projectIdOverride ?? requestedProjectId ?? project?.id
    const session = await dbCreateSession(requestedName, projectId)
    const sessionWithStats: SessionWithStats = { ...session, messageCount: 0, totalTokens: 0 }
    const fromSessionId = currentSession()?.id

    if (fromSessionId && fromSessionId !== session.id) {
      cacheCurrentSessionArtifacts(fromSessionId)
    }

    setSessions((prev) => [sessionWithStats, ...prev])
    setCurrentSession(session)
    resetSessionArtifacts()
    cacheSessionArtifacts(session.id, {
      messages: [],
      agents: [],
      fileOps: [],
      terminalExecutions: [],
      memoryItems: [],
      checkpoints: [],
    })
    log.info('session', 'Session created', { id: session.id, name: session.name })
    logInfo('session', 'Session created', {
      id: session.id,
      name: session.name,
      project: project?.name ?? 'unknown',
    })

    const { currentProject: getProject } = useProject()
    const cwd = getProject()?.directory || '.'
    await notifySessionOpened(session.id, cwd, buildDesktopSessionSnapshot(session, []))
    if (currentSession()?.id !== session.id) {
      return session
    }
    localStorage.setItem(STORAGE_KEYS.LAST_SESSION, session.id)
    setLastSessionForProject(projectId, session.id)
    return session
  })()
  createSessionInFlightByKey.set(requestKey, createPromise)

  try {
    return await createPromise
  } finally {
    if (createSessionInFlightByKey.get(requestKey) === createPromise) {
      createSessionInFlightByKey.delete(requestKey)
    }
  }
}

export async function switchSession(
  id: string,
  _options: SessionTransitionOptions = {}
): Promise<void> {
  const fromSessionId = currentSession()?.id
  if (fromSessionId === id) {
    return
  }

  const session = sessions().find((s) => s.id === id)
  if (!session) {
    logWarn('session', 'Session not found', { id })
    return
  }

  const requestToken = sessionSwitchGate.begin()
  let loadedMessages: Awaited<ReturnType<typeof getMessages>> = []
  if (fromSessionId && fromSessionId !== id) {
    cacheCurrentSessionArtifacts(fromSessionId)
  }
  const cachedArtifacts = getCachedSessionArtifacts(id)
  loadedMessages = cachedArtifacts?.messages ?? []
  setEditingMessageId(null)
  setRetryingMessageId(null)
  setCurrentSession(session)
  if (cachedArtifacts) {
    hydrateSessionArtifacts(cachedArtifacts)
  } else {
    resetSessionArtifacts()
  }

  // Switch to per-session log file
  import('../../lib/logger').then((m) => m.setSessionLogFile(id)).catch(() => {})

  setIsLoadingMessages(!cachedArtifacts)
  try {
    const [dbMessages, dbAgents, dbFileOps, dbTerminalExecs, dbMemItems, dbCheckpoints] =
      await Promise.all([
        getMessages(id),
        getAgents(id),
        getFileOperations(id),
        getTerminalExecutions(id),
        getMemoryItems(id),
        getCheckpoints(id),
      ])
    if (!sessionSwitchGate.isCurrent(requestToken) || currentSession()?.id !== id) {
      return
    }
    loadedMessages = dbMessages
    log.debug('session', `switchSession: loaded ${dbMessages.length} messages from DB for ${id}`)
    const freshArtifacts = {
      messages: dbMessages,
      agents: dbAgents,
      fileOps: dbFileOps,
      terminalExecutions: dbTerminalExecs,
      memoryItems: dbMemItems,
      checkpoints: dbCheckpoints,
    }
    hydrateSessionArtifacts(freshArtifacts)
    cacheSessionArtifacts(id, freshArtifacts)
    log.info('session', 'Session loaded', { id, messageCount: dbMessages.length })
    logInfo('session', 'Session switched', {
      from: fromSessionId ?? 'none',
      to: id,
      messageCount: dbMessages.length,
    })
  } catch (err) {
    if (!sessionSwitchGate.isCurrent(requestToken) || currentSession()?.id !== id) {
      return
    }
    logError('Session', 'Failed to load messages', err)
    resetSessionArtifacts()
  } finally {
    if (sessionSwitchGate.isCurrent(requestToken) && currentSession()?.id === id) {
      setIsLoadingMessages(false)
    }
  }

  // Perform detached session recovery BEFORE notifying the backend, so the
  // snapshot sent to notifySessionOpened() contains the authoritative recovered
  // transcript rather than stale pre-recovery messages.
  if (isTauri()) {
    const recovered = await recoverDetachedDesktopSessionIfNeeded(id)
    if (recovered && sessionSwitchGate.isCurrent(requestToken) && currentSession()?.id === id) {
      // Update loadedMessages to reflect the recovered authoritative state
      loadedMessages = messages()
    }
    if (!sessionSwitchGate.isCurrent(requestToken) || currentSession()?.id !== id) {
      return
    }
  } else {
    const recovered = await recoverWebSessionFromBackendIfNeeded(id)
    if (recovered && sessionSwitchGate.isCurrent(requestToken) && currentSession()?.id === id) {
      // Update loadedMessages to reflect the recovered authoritative state
      loadedMessages = messages()
    }
    if (!sessionSwitchGate.isCurrent(requestToken) || currentSession()?.id !== id) {
      return
    }
  }

  const { currentProject: getProject } = useProject()
  const cwd = getProject()?.directory || '.'
  await notifySessionOpened(id, cwd, buildDesktopSessionSnapshot(session, loadedMessages))
  if (!sessionSwitchGate.isCurrent(requestToken) || currentSession()?.id !== id) {
    return
  }

  localStorage.setItem(STORAGE_KEYS.LAST_SESSION, id)
  const { currentProject } = useProject()
  setLastSessionForProject(currentProject()?.id, id)
}

/** Switch to most-recent or create new session after removal */
async function switchAfterRemoval(projectId?: string): Promise<void> {
  const remaining = sessions()
  if (remaining.length > 0) {
    const replacement = projectId
      ? remaining.find((session) => session.projectId === projectId)
      : remaining.find((session) => session.projectId === undefined)

    if (replacement) {
      await switchSession(replacement.id)
      return
    }

    await createNewSession(undefined, projectId)
    return
  }

  await createNewSession(undefined, projectId)
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
  log.info('session', 'Session archived', { id })
  const projectId = currentSession()?.id === id ? currentSession()?.projectId : undefined
  await dbArchiveSession(id)
  // NOTE: Do NOT unregister the web-mode session alias mapping here.
  // The backend session still exists and may be accessed later (e.g., viewing
  // archived sessions, unarchiving). Alias cleanup is tied to permanent
  // deletion in deleteSessionPermanently() only.
  deleteCachedSessionArtifacts(id)
  setSessions((prev) => prev.filter((s) => s.id !== id))
  if (currentSession()?.id === id) await switchAfterRemoval(projectId)
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
  log.info('session', 'Session deleted permanently', { id })
  const projectId = currentSession()?.id === id ? currentSession()?.projectId : undefined
  await dbDeleteSession(id)
  // Clean up web-mode session alias mapping when session is deleted
  unregisterBackendSessionId(id)
  deleteCachedSessionArtifacts(id)
  setSessions((prev) => prev.filter((s) => s.id !== id))
  if (currentSession()?.id === id) await switchAfterRemoval(projectId)
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
