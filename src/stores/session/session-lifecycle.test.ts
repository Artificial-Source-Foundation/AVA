import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { STORAGE_KEYS } from '../../config/constants'
import type { Message, Session, SessionWithStats } from '../../types'

let isTauriRuntime = false
const notifySessionOpenedMock = vi.fn()
const unregisterBackendSessionIdMock = vi.fn<(sessionId: string) => void>()
const sessionNeedsAuthoritativeRecoveryMock = vi.fn<(sessionId: string) => boolean>(() => false)
const clearSessionNeedsAuthoritativeRecoveryMock = vi.fn<(sessionId: string) => void>()
const markSessionNeedsAuthoritativeRecoveryMock = vi.fn<(sessionId: string) => void>()
const dbArchiveSessionMock = vi.fn()
const dbCreateSessionMock = vi.fn()
const dbDeleteSessionMock = vi.fn()
const dbDeleteSessionMessagesMock = vi.fn()
const getMessagesMock = vi.fn()
const getAgentsMock = vi.fn()
const getFileOperationsMock = vi.fn()
const getTerminalExecutionsMock = vi.fn()
const getMemoryItemsMock = vi.fn()
const getCheckpointsMock = vi.fn()
const insertMessagesMock = vi.fn()
const loadSessionMock = vi.fn()
const setLastSessionForProjectMock =
  vi.fn<(projectId: string | null | undefined, sessionId: string) => void>()
const cancelMock = vi.fn().mockResolvedValue(undefined)
const statusMock = vi.fn().mockResolvedValue({ running: false, provider: 'openai', model: 'gpt-5' })
let mockProject = {
  id: 'project-1',
  name: 'Workspace',
  directory: '/workspace',
  createdAt: 0,
  updatedAt: 0,
  lastOpenedAt: 0,
}
let mockProjects = [mockProject]

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => isTauriRuntime,
}))

vi.mock('../../hooks/use-rust-agent', () => ({
  clearTodos: vi.fn(),
}))

vi.mock('../../lib/logger', () => ({
  log: {
    info: vi.fn(),
    debug: vi.fn(),
    warn: vi.fn(),
    error: vi.fn(),
  },
}))

vi.mock('../../services/core-bridge', () => ({
  getCoreBudget: () => null,
  sessionNeedsAuthoritativeRecovery: (sessionId: string) =>
    sessionNeedsAuthoritativeRecoveryMock(sessionId),
  clearSessionNeedsAuthoritativeRecovery: (sessionId: string) =>
    clearSessionNeedsAuthoritativeRecoveryMock(sessionId),
  markSessionNeedsAuthoritativeRecovery: (sessionId: string) =>
    markSessionNeedsAuthoritativeRecoveryMock(sessionId),
  notifySessionOpened: (sessionId: string, cwd?: string, snapshot?: unknown) =>
    notifySessionOpenedMock(sessionId, cwd, snapshot),
}))

vi.mock('../../services/database', () => ({
  archiveSession: (...args: unknown[]) => dbArchiveSessionMock(...args),
  createSession: (...args: unknown[]) => dbCreateSessionMock(...args),
  deleteSession: (...args: unknown[]) => dbDeleteSessionMock(...args),
  deleteSessionMessages: (...args: unknown[]) => dbDeleteSessionMessagesMock(...args),
  getAgents: (...args: unknown[]) => getAgentsMock(...args),
  getArchivedSessions: vi.fn(),
  getCheckpoints: (...args: unknown[]) => getCheckpointsMock(...args),
  getFileOperations: (...args: unknown[]) => getFileOperationsMock(...args),
  getMemoryItems: (...args: unknown[]) => getMemoryItemsMock(...args),
  getMessages: (...args: unknown[]) => getMessagesMock(...args),
  getSessionsWithStats: vi.fn(),
  getTerminalExecutions: (...args: unknown[]) => getTerminalExecutionsMock(...args),
  insertMessages: (...args: unknown[]) => insertMessagesMock(...args),
  updateSession: vi.fn(),
}))

vi.mock('../../services/logger', () => ({
  logDebug: vi.fn(),
  logError: vi.fn(),
  logInfo: vi.fn(),
  logWarn: vi.fn(),
}))

vi.mock('../../services/rust-bridge', () => ({
  rustBackend: {
    getAgentStatus: (...args: unknown[]) => statusMock(...args),
    loadSession: (...args: unknown[]) => loadSessionMock(...args),
  },
  rustAgent: {
    cancel: (...args: unknown[]) => cancelMock(...args),
    status: (...args: unknown[]) => statusMock(...args),
  },
}))

vi.mock('../../services/web-session-identity', () => ({
  unregisterBackendSessionId: (sessionId: string) => unregisterBackendSessionIdMock(sessionId),
}))

vi.mock('../project', () => ({
  useProject: () => ({
    currentProject: () => mockProject,
    projects: () => mockProjects,
  }),
}))

vi.mock('../session-persistence', () => ({
  getLastSessionForProject: vi.fn(),
  setLastSessionForProject: (projectId: string | null | undefined, sessionId: string) =>
    setLastSessionForProjectMock(projectId, sessionId),
}))

import { clearSessionArtifactCache } from './session-artifact-cache'
import {
  archiveSession,
  createNewSession,
  deleteSessionPermanently,
  getSessionTree,
  restoreForCurrentProject,
  switchSession,
} from './session-lifecycle'
import { replaceMessagesFromBackendForSession, updateMessageInSession } from './session-messages'
import {
  agents,
  archivedSessions,
  currentSession,
  fileOperations,
  messages,
  sessions,
  setAgents,
  setArchivedSessions,
  setCheckpoints,
  setCurrentSession,
  setEditingMessageId,
  setFileOperations,
  setIsLoadingMessages,
  setMemoryItems,
  setMessages,
  setRetryingMessageId,
  setSessions,
  setTerminalExecutions,
} from './session-state'

function makeSession(
  id: string,
  name = `Session ${id}`,
  projectId: string | undefined = 'project-1'
): Session {
  return {
    id,
    name,
    projectId,
    createdAt: Date.now(),
    updatedAt: Date.now(),
    status: 'active',
    metadata: {},
  }
}

function makeSessionWithStats(
  id: string,
  name = `Session ${id}`,
  projectId: string | undefined = 'project-1'
): SessionWithStats {
  return {
    ...makeSession(id, name, projectId),
    messageCount: 0,
    totalTokens: 0,
    lastPreview: '',
  }
}

function createDeferred<T>() {
  let resolve!: (value: T) => void
  let reject!: (reason?: unknown) => void
  const promise = new Promise<T>((resolvePromise, rejectPromise) => {
    resolve = resolvePromise
    reject = rejectPromise
  })
  return { promise, resolve, reject }
}

function resetSessionState(): void {
  setCurrentSession(null)
  setSessions([])
  setMessages([])
  setAgents([])
  setFileOperations([])
  setTerminalExecutions([])
  setMemoryItems([])
  setCheckpoints([])
  setArchivedSessions([])
  setEditingMessageId(null)
  setRetryingMessageId(null)
  setIsLoadingMessages(false)
}

describe('session removal fallback rebinding', () => {
  beforeEach(() => {
    isTauriRuntime = false
    vi.clearAllMocks()
    localStorage.clear()
    clearSessionArtifactCache()
    resetSessionState()
    mockProject = {
      id: 'project-1',
      name: 'Workspace',
      directory: '/workspace',
      createdAt: 0,
      updatedAt: 0,
      lastOpenedAt: 0,
    }
    mockProjects = [mockProject]

    getMessagesMock.mockResolvedValue([])
    getAgentsMock.mockResolvedValue([])
    getFileOperationsMock.mockResolvedValue([])
    getTerminalExecutionsMock.mockResolvedValue([])
    getMemoryItemsMock.mockResolvedValue([])
    getCheckpointsMock.mockResolvedValue([])
    dbDeleteSessionMessagesMock.mockResolvedValue(undefined)
    insertMessagesMock.mockResolvedValue(undefined)
    dbDeleteSessionMock.mockResolvedValue(undefined)
    sessionNeedsAuthoritativeRecoveryMock.mockReturnValue(false)
    clearSessionNeedsAuthoritativeRecoveryMock.mockReset()
    markSessionNeedsAuthoritativeRecoveryMock.mockReset()
    statusMock.mockResolvedValue({ running: false, provider: 'openai', model: 'gpt-5' })
    loadSessionMock.mockResolvedValue({ messages: [] })
  })

  it('groups sessions into roots and children by parentSessionId', () => {
    const rootA = makeSessionWithStats('root-a', 'Root A')
    const rootB = makeSessionWithStats('root-b', 'Root B')
    const rootChildOne = {
      ...makeSessionWithStats('root-a-child-1', 'Root A Child 1'),
      parentSessionId: rootA.id,
    }
    const rootChildTwo = {
      ...makeSessionWithStats('root-a-child-2', 'Root A Child 2'),
      parentSessionId: rootA.id,
    }
    const orphanedChild = {
      ...makeSessionWithStats('root-b-child', 'Root B Child'),
      parentSessionId: rootB.id,
    }

    setSessions([rootA, rootB, rootChildTwo, rootChildOne, orphanedChild])

    const { roots, childMap } = getSessionTree()

    expect(roots.map((session) => session.id)).toEqual([rootA.id, rootB.id])
    expect(childMap.get(rootA.id)?.map((session) => session.id)).toEqual([
      rootChildTwo.id,
      rootChildOne.id,
    ])
    expect(childMap.get(rootB.id)?.map((session) => session.id)).toEqual([orphanedChild.id])
  })

  it('recomputes session tree for fresh session snapshots', () => {
    const firstRoot = makeSessionWithStats('first-root', 'First Root')
    const firstChild = {
      ...makeSessionWithStats('shared-child', 'Shared Child 1'),
      parentSessionId: firstRoot.id,
    }

    setSessions([firstRoot, firstChild])

    const initialTree = getSessionTree()
    expect(initialTree.roots.map((session) => session.id)).toEqual([firstRoot.id])
    expect(initialTree.childMap.get(firstRoot.id)?.map((session) => session.id)).toEqual([
      firstChild.id,
    ])

    const secondRoot = makeSessionWithStats('second-root', 'Second Root')
    const secondChild = {
      ...makeSessionWithStats('shared-child', 'Shared Child 2'),
      parentSessionId: secondRoot.id,
    }

    setSessions([secondRoot, secondChild])

    const refreshedTree = getSessionTree()
    expect(refreshedTree.roots.map((session) => session.id)).toEqual([secondRoot.id])
    expect(refreshedTree.childMap.get(secondRoot.id)?.map((session) => session.id)).toEqual([
      secondChild.id,
    ])
    expect(refreshedTree.childMap.has(firstRoot.id)).toBe(false)
  })

  afterEach(() => {
    localStorage.clear()
    clearSessionArtifactCache()
    resetSessionState()
  })

  it('rebinds the desktop backend when archiving the active session switches to another session', async () => {
    const archived = makeSession('session-1', 'Archived session')
    const fallback = makeSessionWithStats('session-2', 'Fallback session')

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: fallback.id,
      exists: true,
      messageCount: 0,
    })

    setSessions([fallback, makeSessionWithStats(archived.id, archived.name)])
    setCurrentSession(archived)

    await archiveSession(archived.id)

    expect(dbArchiveSessionMock).toHaveBeenCalledWith(archived.id)
    expect(currentSession()?.id).toBe(fallback.id)
    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      fallback.id,
      '/workspace',
      expect.objectContaining({ title: fallback.name, messages: [] })
    )
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe(fallback.id)
  })

  it('preserves frontend→backend session alias when archiving (for later operations)', async () => {
    // This test verifies that archiving does NOT unregister the web-mode session alias.
    // The backend session still exists and may be accessed later (viewing archived sessions,
    // unarchiving, etc.), so the alias mapping must be preserved.
    const archived = makeSession('session-archive-alias', 'Session to archive')
    const fallback = makeSessionWithStats('session-fallback', 'Fallback')

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: fallback.id,
      exists: true,
      messageCount: 0,
    })

    setSessions([fallback, makeSessionWithStats(archived.id, archived.name)])
    setCurrentSession(archived)

    await archiveSession(archived.id)

    // Alias should NOT be unregistered on archive
    expect(unregisterBackendSessionIdMock).not.toHaveBeenCalledWith(archived.id)
    // But cache cleanup should still happen
    expect(currentSession()?.id).toBe(fallback.id)
  })

  it('unregisters frontend→backend session alias on permanent deletion only', async () => {
    // This test verifies that alias cleanup is tied to permanent deletion only,
    // not archive. The backend session is gone at deletion time.
    const deleted = makeSession('session-delete-alias', 'Session to delete')
    const replacement = makeSession('session-replacement', 'New session')

    dbCreateSessionMock.mockResolvedValue(replacement)
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: replacement.id,
      exists: true,
      messageCount: 0,
    })

    setSessions([makeSessionWithStats(deleted.id, deleted.name)])
    setCurrentSession(deleted)

    await deleteSessionPermanently(deleted.id)

    // Alias SHOULD be unregistered on permanent deletion
    expect(unregisterBackendSessionIdMock).toHaveBeenCalledWith(deleted.id)
    expect(dbDeleteSessionMock).toHaveBeenCalledWith(deleted.id)
    expect(currentSession()?.id).toBe(replacement.id)
  })

  it('removes deleted archived sessions from archived client state', async () => {
    const archived = {
      ...makeSessionWithStats('session-archived-delete', 'Archived delete target'),
      status: 'archived' as const,
    }
    const survivor = {
      ...makeSessionWithStats('session-archived-keep', 'Archived survivor'),
      status: 'archived' as const,
    }

    setSessions([makeSessionWithStats('session-active', 'Active session')])
    setArchivedSessions([archived, survivor])

    await deleteSessionPermanently(archived.id)

    expect(dbDeleteSessionMock).toHaveBeenCalledWith(archived.id)
    expect(archivedSessions().map((session) => session.id)).toEqual([survivor.id])
    expect(unregisterBackendSessionIdMock).toHaveBeenCalledWith(archived.id)
  })

  it('rebinds the desktop backend when deleting the last active session creates a replacement', async () => {
    const deleted = makeSession('session-1', 'Deleted session')
    const replacement = makeSession('session-3', 'New session')

    dbCreateSessionMock.mockResolvedValue(replacement)
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: replacement.id,
      exists: true,
      messageCount: 0,
    })

    setSessions([makeSessionWithStats(deleted.id, deleted.name)])
    setCurrentSession(deleted)

    await deleteSessionPermanently(deleted.id)

    expect(dbDeleteSessionMock).toHaveBeenCalledWith(deleted.id)
    expect(dbCreateSessionMock).toHaveBeenCalled()
    expect(currentSession()?.id).toBe(replacement.id)
    expect(sessions().map((session) => session.id)).toEqual([replacement.id])
    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      replacement.id,
      '/workspace',
      expect.objectContaining({ title: replacement.name, messages: [] })
    )
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe(replacement.id)
  })

  it('preserves the originating project when fallback replacement creates a new session', async () => {
    const deleted = makeSession('session-1', 'Deleted session')
    const replacement = makeSession('session-3', 'New session')
    let resolveDelete: (value?: unknown) => void = () => {}

    dbCreateSessionMock.mockResolvedValue(replacement)
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: replacement.id,
      exists: true,
      messageCount: 0,
    })
    dbDeleteSessionMock.mockImplementation(
      () =>
        new Promise((resolve) => {
          resolveDelete = resolve
        })
    )

    setSessions([makeSessionWithStats(deleted.id, deleted.name)])
    setCurrentSession(deleted)

    const deletePromise = deleteSessionPermanently(deleted.id)

    mockProject = {
      id: 'project-2',
      name: 'Second Workspace',
      directory: '/other-workspace',
      createdAt: 0,
      updatedAt: 0,
      lastOpenedAt: 0,
    }

    resolveDelete()
    await deletePromise

    expect(dbDeleteSessionMock).toHaveBeenCalledWith(deleted.id)
    expect(dbCreateSessionMock).toHaveBeenCalledWith('New Chat', 'project-1')
    expect(currentSession()?.id).toBe(replacement.id)
    expect(sessions().map((session) => session.id)).toEqual([replacement.id])
    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      replacement.id,
      '/workspace',
      expect.objectContaining({ title: replacement.name, messages: [] })
    )
  })

  it('switches to an existing projectless session when deleting active projectless session', async () => {
    const deleted = makeSession('session-1', 'Deleted session', undefined)
    const remaining = makeSessionWithStats('session-2', 'Projectless fallback', undefined)

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: remaining.id,
      exists: true,
      messageCount: 0,
    })

    setSessions([
      remaining,
      makeSessionWithStats('session-3', 'Projected replacement', 'project-1'),
    ])
    setCurrentSession(deleted)

    await deleteSessionPermanently(deleted.id)

    expect(dbDeleteSessionMock).toHaveBeenCalledWith(deleted.id)
    expect(dbCreateSessionMock).not.toHaveBeenCalled()
    expect(currentSession()?.id).toBe(remaining.id)
    expect(sessions().map((session) => session.id)).toEqual([remaining.id, 'session-3'])
    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      remaining.id,
      '/workspace',
      expect.objectContaining({ title: remaining.name, messages: [] })
    )
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe(remaining.id)
  })

  it('passes restored session messages into desktop backend sync when switching sessions', async () => {
    const target = makeSessionWithStats('session-10', 'Recovered session')
    const toolCalls = [
      {
        id: 'tool-call-1',
        name: 'bash',
        args: { command: 'pwd' },
        status: 'success' as const,
        output: '/workspace',
        startedAt: 1_762_806_001_500,
      },
    ]
    const userMessage = {
      id: '00000000-0000-0000-0000-000000000001',
      sessionId: target.id,
      role: 'user' as const,
      content: 'restore me',
      createdAt: 1_762_806_000_000,
      images: [{ data: 'base64-image', mimeType: 'image/png', name: 'screenshot.png' }],
    }
    const assistantMessage = {
      id: '00000000-0000-0000-0000-000000000002',
      sessionId: target.id,
      role: 'assistant' as const,
      content: 'restored reply',
      createdAt: 1_762_806_001_000,
      toolCalls,
      metadata: {
        toolCalls: [
          { id: 'stale-tool', name: 'read', args: { path: 'ignored' }, status: 'success' },
        ],
        agentVisible: false,
      },
    }
    const toolMessage: Message = {
      id: '00000000-0000-0000-0000-000000000003',
      sessionId: target.id,
      role: 'tool',
      content: '/workspace',
      createdAt: 1_762_806_002_000,
      metadata: {
        toolCallId: 'tool-call-1',
        userVisible: false,
      },
    }

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: target.id,
      exists: true,
      messageCount: 3,
    })
    setSessions([target])
    getMessagesMock.mockResolvedValueOnce([userMessage, assistantMessage, toolMessage])

    await switchSession(target.id)

    expect(notifySessionOpenedMock).toHaveBeenCalledWith(target.id, '/workspace', {
      title: target.name,
      messages: [
        {
          id: userMessage.id,
          role: userMessage.role,
          content: userMessage.content,
          createdAt: userMessage.createdAt,
          images: [{ data: 'base64-image', mediaType: 'image/png' }],
        },
        {
          id: assistantMessage.id,
          role: assistantMessage.role,
          content: assistantMessage.content,
          createdAt: assistantMessage.createdAt,
          metadata: {
            agentVisible: false,
            toolCalls,
          },
        },
        {
          id: toolMessage.id,
          role: 'tool',
          content: toolMessage.content,
          createdAt: toolMessage.createdAt,
          metadata: {
            toolCallId: 'tool-call-1',
            userVisible: false,
          },
        },
      ],
    })
  })

  it('skips storage reload work when reselecting the already active session', async () => {
    const active = makeSessionWithStats('session-active', 'Active session')

    setSessions([active])
    setCurrentSession(active)
    setMessages([
      {
        id: 'existing-message',
        sessionId: active.id,
        role: 'assistant',
        content: 'still here',
        createdAt: 10,
      },
    ])

    await switchSession(active.id)

    expect(getMessagesMock).not.toHaveBeenCalled()
    expect(notifySessionOpenedMock).not.toHaveBeenCalled()
    expect(messages()).toEqual([
      {
        id: 'existing-message',
        sessionId: active.id,
        role: 'assistant',
        content: 'still here',
        createdAt: 10,
      },
    ])
  })

  it('hydrates recent cached session artifacts immediately while refreshing from storage in the background', async () => {
    const sessionOne = makeSessionWithStats('session-1', 'First session')
    const sessionTwo = makeSessionWithStats('session-2', 'Second session')
    const cachedMessage = {
      id: 'cached-message',
      sessionId: sessionOne.id,
      role: 'assistant' as const,
      content: 'cached reply',
      createdAt: 10,
    }
    const refreshedMessage = {
      id: 'refreshed-message',
      sessionId: sessionOne.id,
      role: 'assistant' as const,
      content: 'refreshed reply',
      createdAt: 11,
    }
    const deferredMessages = createDeferred<Array<typeof refreshedMessage>>()

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: sessionOne.id,
      exists: true,
      messageCount: 1,
    })
    setSessions([sessionOne, sessionTwo])
    setCurrentSession(sessionOne)
    setMessages([cachedMessage])

    getMessagesMock.mockResolvedValueOnce([])
    getMessagesMock.mockImplementationOnce(() => deferredMessages.promise)

    await switchSession(sessionTwo.id)

    const switchBackPromise = switchSession(sessionOne.id)

    expect(currentSession()?.id).toBe(sessionOne.id)
    expect(messages()).toEqual([cachedMessage])

    deferredMessages.resolve([refreshedMessage])
    await switchBackPromise

    expect(messages()).toEqual([refreshedMessage])
  })

  it('refreshes hidden-session cached messages when authoritative recovery replaces them off-screen', async () => {
    const sessionOne = makeSessionWithStats('session-1', 'First session')
    const sessionTwo = makeSessionWithStats('session-2', 'Second session')
    const cachedMessage = {
      id: 'cached-message',
      sessionId: sessionOne.id,
      role: 'assistant' as const,
      content: 'stale cached reply',
      createdAt: 10,
    }
    const authoritativeMessage = {
      id: 'authoritative-message',
      sessionId: sessionOne.id,
      role: 'assistant' as const,
      content: 'fresh authoritative reply',
      createdAt: 11,
    }
    const deferredMessages = createDeferred<Array<typeof authoritativeMessage>>()

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: sessionOne.id,
      exists: true,
      messageCount: 1,
    })
    setSessions([sessionOne, sessionTwo])
    setCurrentSession(sessionOne)
    setMessages([cachedMessage])

    getMessagesMock.mockResolvedValueOnce([])

    await switchSession(sessionTwo.id)
    await replaceMessagesFromBackendForSession(sessionOne.id, [authoritativeMessage])

    getMessagesMock.mockImplementationOnce(() => deferredMessages.promise)

    const switchBackPromise = switchSession(sessionOne.id)

    expect(messages()).toEqual([authoritativeMessage])

    deferredMessages.resolve([authoritativeMessage])
    await switchBackPromise

    expect(messages()).toEqual([authoritativeMessage])
  })

  it('ignores a stale late load when a newer overlapping session switch wins', async () => {
    const sessionOne = makeSessionWithStats('session-1', 'First session')
    const sessionTwo = makeSessionWithStats('session-2', 'Second session')
    const sessionThree = makeSessionWithStats('session-3', 'Third session')
    const olderMessage = {
      id: 'older-message',
      sessionId: sessionTwo.id,
      role: 'assistant' as const,
      content: 'older session reply',
      createdAt: 10,
    }
    const newerMessage = {
      id: 'newer-message',
      sessionId: sessionThree.id,
      role: 'assistant' as const,
      content: 'newer session reply',
      createdAt: 11,
    }
    const olderDeferred = createDeferred<Array<typeof olderMessage>>()
    const newerDeferred = createDeferred<Array<typeof newerMessage>>()

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: sessionThree.id,
      exists: true,
      messageCount: 1,
    })
    setSessions([sessionOne, sessionTwo, sessionThree])
    setCurrentSession(sessionOne)
    setMessages([
      {
        id: 'session-one-message',
        sessionId: sessionOne.id,
        role: 'assistant' as const,
        content: 'first session reply',
        createdAt: 9,
      },
    ])
    localStorage.setItem(STORAGE_KEYS.LAST_SESSION, sessionOne.id)

    getMessagesMock
      .mockImplementationOnce(() => olderDeferred.promise)
      .mockImplementationOnce(() => newerDeferred.promise)

    const olderSwitchPromise = switchSession(sessionTwo.id)
    const newerSwitchPromise = switchSession(sessionThree.id)

    expect(currentSession()?.id).toBe(sessionThree.id)

    newerDeferred.resolve([newerMessage])
    await newerSwitchPromise

    expect(currentSession()?.id).toBe(sessionThree.id)
    expect(messages()).toEqual([newerMessage])
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe(sessionThree.id)

    olderDeferred.resolve([olderMessage])
    await olderSwitchPromise

    expect(currentSession()?.id).toBe(sessionThree.id)
    expect(messages()).toEqual([newerMessage])
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe(sessionThree.id)
    expect(notifySessionOpenedMock).toHaveBeenCalledTimes(1)
    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      sessionThree.id,
      '/workspace',
      expect.objectContaining({
        title: sessionThree.name,
        messages: expect.arrayContaining([
          expect.objectContaining({ id: newerMessage.id, content: newerMessage.content }),
        ]),
      })
    )
  })

  it('persists last-session under the target project even if current project changes mid-switch', async () => {
    const source = makeSessionWithStats('session-source', 'Source session', 'project-1')
    const target = makeSessionWithStats('session-target', 'Target session', 'project-1')
    const deferredMessages =
      createDeferred<
        Array<{
          id: string
          sessionId: string
          role: 'assistant'
          content: string
          createdAt: number
        }>
      >()

    setSessions([source, target])
    setCurrentSession(source)
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: target.id,
      exists: true,
      messageCount: 1,
    })
    getMessagesMock.mockImplementationOnce(() => deferredMessages.promise)

    const switchPromise = switchSession(target.id)

    mockProject = {
      ...mockProject,
      id: 'project-2',
      directory: '/workspace-2',
      name: 'Workspace 2',
    }

    deferredMessages.resolve([
      {
        id: 'target-message',
        sessionId: target.id,
        role: 'assistant',
        content: 'loaded after context change',
        createdAt: 1,
      },
    ])

    await switchPromise

    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-1', target.id)
    expect(setLastSessionForProjectMock).not.toHaveBeenCalledWith('project-2', target.id)
    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      target.id,
      '/workspace',
      expect.objectContaining({ title: target.name })
    )
  })

  it('restores cached non-message artifacts immediately and corrects them from storage', async () => {
    const sessionOne = makeSessionWithStats('session-1', 'First session')
    const sessionTwo = makeSessionWithStats('session-2', 'Second session')
    const cachedAgent = {
      id: 'agent-cached',
      sessionId: sessionOne.id,
      type: 'operator' as const,
      status: 'thinking' as const,
      model: 'gpt-5.4',
      createdAt: 1,
    }
    const refreshedAgent = {
      ...cachedAgent,
      id: 'agent-fresh',
      status: 'completed' as const,
      completedAt: 2,
    }
    const cachedFileOperation = {
      id: 'file-op-cached',
      sessionId: sessionOne.id,
      filePath: '/tmp/old.txt',
      type: 'edit' as const,
      timestamp: 1,
    }
    const refreshedFileOperation = {
      ...cachedFileOperation,
      id: 'file-op-fresh',
      filePath: '/tmp/fresh.txt',
      timestamp: 2,
    }
    const deferredAgents = createDeferred<Array<typeof refreshedAgent>>()

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: sessionOne.id,
      exists: true,
      messageCount: 0,
    })
    setSessions([sessionOne, sessionTwo])
    setCurrentSession(sessionOne)
    setAgents([cachedAgent])
    setFileOperations([cachedFileOperation])

    getMessagesMock.mockResolvedValueOnce([])
    getAgentsMock.mockResolvedValueOnce([])
    getFileOperationsMock.mockResolvedValueOnce([])

    await switchSession(sessionTwo.id)

    getMessagesMock.mockResolvedValueOnce([])
    getAgentsMock.mockImplementationOnce(() => deferredAgents.promise)
    getFileOperationsMock.mockResolvedValueOnce([refreshedFileOperation])

    const switchBackPromise = switchSession(sessionOne.id)

    expect(agents()).toEqual([cachedAgent])
    expect(fileOperations()).toEqual([cachedFileOperation])

    deferredAgents.resolve([refreshedAgent])
    await switchBackPromise

    expect(agents()).toEqual([refreshedAgent])
    expect(fileOperations()).toEqual([refreshedFileOperation])
  })

  it('uses the target session project cwd when switching to a non-current-project session', async () => {
    const source = makeSessionWithStats('session-source', 'Source session', 'project-1')
    const target = makeSessionWithStats('session-target', 'Target session', 'project-2')

    mockProjects = [
      mockProject,
      {
        id: 'project-2',
        name: 'Workspace 2',
        directory: '/workspace-2',
        createdAt: 0,
        updatedAt: 0,
        lastOpenedAt: 0,
      },
    ]

    setSessions([source, target])
    setCurrentSession(source)
    getMessagesMock.mockResolvedValueOnce([])
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: target.id,
      exists: true,
      messageCount: 0,
    })

    await switchSession(target.id)

    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      target.id,
      '/workspace-2',
      expect.objectContaining({ title: target.name, messages: [] })
    )
  })

  it('does not fall back to the ambient project cwd when switching to an unbound session', async () => {
    const source = makeSessionWithStats('session-source', 'Source session', 'project-1')
    const target = {
      ...makeSessionWithStats('session-target', 'Target session'),
      projectId: undefined,
    }

    setSessions([source, target])
    setCurrentSession(source)
    getMessagesMock.mockResolvedValueOnce([])
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: target.id,
      exists: true,
      messageCount: 0,
    })

    await switchSession(target.id)

    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      target.id,
      undefined,
      expect.objectContaining({ title: target.name, messages: [] })
    )
  })

  it('marks missing project resolution explicitly when switching to a previously bound session', async () => {
    const source = makeSessionWithStats('session-source', 'Source session', 'project-1')
    const target = makeSessionWithStats('session-target', 'Target session', 'project-missing')

    mockProjects = [mockProject]
    setSessions([source, target])
    setCurrentSession(source)
    getMessagesMock.mockResolvedValueOnce([])
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: target.id,
      exists: true,
      messageCount: 0,
    })

    await switchSession(target.id)

    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      target.id,
      '',
      expect.objectContaining({ title: target.name, messages: [] })
    )
  })

  it('caches the active session before creating a new chat so hidden web completions survive switch-back', async () => {
    const sessionOne = makeSessionWithStats('session-1', 'First session')
    const created = makeSession('session-2', 'New Chat')
    const assistantMessage = {
      id: 'assistant-live',
      sessionId: sessionOne.id,
      role: 'assistant' as const,
      content: '',
      createdAt: 10,
    }
    const completedToolCall = {
      id: 'tool-1',
      name: 'bash',
      args: { command: 'pwd' },
      status: 'success' as const,
      startedAt: 11,
      completedAt: 12,
      output: '/workspace',
    }
    const refreshedMessage = {
      ...assistantMessage,
      content: 'completed off-screen reply',
      toolCalls: [completedToolCall],
    }
    const deferredMessages = createDeferred<Array<typeof refreshedMessage>>()

    dbCreateSessionMock.mockResolvedValue(created)
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: created.id,
      exists: true,
      messageCount: 0,
    })
    setSessions([sessionOne])
    setCurrentSession(sessionOne)
    setMessages([assistantMessage])

    const createdSession = await createNewSession('New Chat')

    expect(createdSession.id).toBe(created.id)
    expect(currentSession()?.id).toBe(created.id)

    updateMessageInSession(sessionOne.id, assistantMessage.id, {
      content: refreshedMessage.content,
      toolCalls: refreshedMessage.toolCalls,
    })

    getMessagesMock.mockImplementationOnce(() => deferredMessages.promise)

    const switchBackPromise = switchSession(sessionOne.id)

    expect(currentSession()?.id).toBe(sessionOne.id)
    expect(messages()).toEqual([refreshedMessage])

    deferredMessages.resolve([refreshedMessage])
    await switchBackPromise

    expect(messages()).toEqual([refreshedMessage])
  })

  it('recovers authoritative backend messages when reopening a detached desktop session', async () => {
    isTauriRuntime = true
    const target = makeSessionWithStats('session-10', 'Recovered session')
    const partialAssistant = {
      id: 'backend-assistant-final',
      sessionId: target.id,
      role: 'assistant' as const,
      content: 'partial output',
      createdAt: 1_762_806_001_000,
      toolCalls: [
        {
          id: 'tool-rich-1',
          name: 'bash',
          args: { command: 'pwd' },
          status: 'success' as const,
          output: '/workspace',
          startedAt: 10,
          completedAt: 20,
          contentOffset: 42,
        },
      ],
    }

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: target.id,
      exists: true,
      messageCount: 3,
    })
    sessionNeedsAuthoritativeRecoveryMock.mockReturnValueOnce(true)
    loadSessionMock.mockResolvedValue({
      id: target.id,
      messages: [
        {
          id: 'backend-user',
          role: 'User',
          content: 'finish the task',
          timestamp: '2026-04-17T10:00:00Z',
        },
        {
          id: 'backend-assistant-final',
          role: 'Assistant',
          content: 'authoritative final answer',
          timestamp: '2026-04-17T10:00:10Z',
          agent_visible: false,
          tool_calls: [
            {
              id: 'tool-rich-1',
              name: 'bash',
              arguments: { command: 'pwd' },
              status: 'success',
            },
          ],
        },
        {
          id: 'backend-tool-final',
          role: 'Tool',
          content: '/workspace',
          timestamp: '2026-04-17T10:00:11Z',
          tool_call_id: 'tool-rich-1',
          user_visible: false,
        },
      ],
      metadata: {},
    })

    setSessions([target])
    getMessagesMock.mockResolvedValueOnce([partialAssistant])

    await switchSession(target.id)

    expect(loadSessionMock).toHaveBeenCalledWith(target.id)
    expect(clearSessionNeedsAuthoritativeRecoveryMock).toHaveBeenCalledWith(target.id)
    expect(dbDeleteSessionMessagesMock).toHaveBeenCalledWith(target.id)
    expect(insertMessagesMock).toHaveBeenCalledWith([
      expect.objectContaining({
        id: 'backend-user',
        sessionId: target.id,
        role: 'user',
        content: 'finish the task',
      }),
      expect.objectContaining({
        id: 'backend-assistant-final',
        sessionId: target.id,
        role: 'assistant',
        content: 'authoritative final answer',
      }),
      expect.objectContaining({
        id: 'backend-tool-final',
        sessionId: target.id,
        role: 'tool',
        content: '/workspace',
        metadata: expect.objectContaining({ toolCallId: 'tool-rich-1', userVisible: false }),
      }),
    ])
    expect(currentSession()?.id).toBe(target.id)
    expect(sessions().find((session) => session.id === target.id)?.messageCount).toBe(3)
    expect(messages().map((message) => message.content)).toEqual([
      'finish the task',
      'authoritative final answer',
      '/workspace',
    ])
    expect(messages()[1]?.toolCalls).toEqual([
      expect.objectContaining({
        id: 'tool-rich-1',
        name: 'bash',
        args: { command: 'pwd' },
        output: '/workspace',
        startedAt: 10,
        completedAt: 20,
        contentOffset: 42,
      }),
    ])
    expect(messages()[1]?.metadata).toEqual(
      expect.objectContaining({
        agentVisible: false,
        toolCalls: [expect.objectContaining({ id: 'tool-rich-1', name: 'bash' })],
      })
    )
    expect(messages()[2]).toEqual(
      expect.objectContaining({
        id: 'backend-tool-final',
        role: 'tool',
        content: '/workspace',
        metadata: expect.objectContaining({ toolCallId: 'tool-rich-1', userVisible: false }),
      })
    )
  })

  it('keeps detached recovery pending when reopening a desktop session before the backend run finishes', async () => {
    isTauriRuntime = true
    const target = makeSessionWithStats('session-20', 'Detached still running')

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: target.id,
      exists: true,
      messageCount: 1,
    })
    sessionNeedsAuthoritativeRecoveryMock.mockReturnValueOnce(true)
    statusMock.mockResolvedValueOnce({ running: true, provider: 'openai', model: 'gpt-5' })

    setSessions([target])
    getMessagesMock.mockResolvedValueOnce([
      {
        id: 'assistant-partial',
        sessionId: target.id,
        role: 'assistant' as const,
        content: 'partial output',
        createdAt: 1,
      },
    ])

    await switchSession(target.id)

    expect(loadSessionMock).not.toHaveBeenCalled()
    expect(clearSessionNeedsAuthoritativeRecoveryMock).not.toHaveBeenCalled()
    expect(messages().map((message) => message.content)).toEqual(['partial output'])
  })

  it('recovers authoritative backend messages when reopening a detached web session', async () => {
    const sessionOne = makeSessionWithStats('session-web-1', 'Web session one')
    const sessionTwo = makeSessionWithStats('session-web-2', 'Web session two')
    const partialAssistant = {
      id: 'assistant-final',
      sessionId: sessionOne.id,
      role: 'assistant' as const,
      content: 'partial output',
      createdAt: 10,
      toolCalls: [
        {
          id: 'tool-rich-1',
          name: 'bash',
          args: { command: 'pwd' },
          status: 'success' as const,
          output: '/workspace',
          startedAt: 10,
          completedAt: 20,
          contentOffset: 42,
        },
      ],
    }
    const authoritativeUser = {
      id: 'user-final',
      sessionId: sessionOne.id,
      role: 'user' as const,
      content: 'finish the task',
      createdAt: 11,
    }
    const authoritativeAssistant = {
      id: 'assistant-final',
      sessionId: sessionOne.id,
      role: 'assistant' as const,
      content: 'authoritative final answer',
      createdAt: 12,
      toolCalls: [
        {
          id: 'tool-rich-1',
          name: 'bash',
          args: { command: 'pwd' },
          status: 'success' as const,
        },
      ],
    }

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: sessionOne.id,
      exists: true,
      messageCount: 2,
    })
    sessionNeedsAuthoritativeRecoveryMock.mockImplementation(
      (sessionId) => sessionId === sessionOne.id
    )

    setSessions([sessionOne, sessionTwo])
    setCurrentSession(sessionOne)
    setMessages([partialAssistant])

    getMessagesMock.mockResolvedValueOnce([])
    await switchSession(sessionTwo.id)

    getMessagesMock.mockResolvedValueOnce([partialAssistant])
    getMessagesMock.mockResolvedValueOnce([authoritativeUser, authoritativeAssistant])

    await switchSession(sessionOne.id)

    expect(clearSessionNeedsAuthoritativeRecoveryMock).toHaveBeenCalledWith(sessionOne.id)
    expect(messages().map((message) => message.content)).toEqual([
      'finish the task',
      'authoritative final answer',
    ])
    expect(messages()[1]?.toolCalls).toEqual([
      expect.objectContaining({
        id: 'tool-rich-1',
        name: 'bash',
        args: { command: 'pwd' },
        output: '/workspace',
        startedAt: 10,
        completedAt: 20,
        contentOffset: 42,
      }),
    ])
  })

  it('switches sessions without cancelling an active run', async () => {
    const source = makeSessionWithStats('session-active', 'Active')
    const target = makeSessionWithStats('session-target', 'Target')

    setSessions([source, target])
    setCurrentSession(source)

    await switchSession(target.id)

    expect(statusMock).not.toHaveBeenCalled()
    expect(cancelMock).not.toHaveBeenCalled()
    expect(currentSession()?.id).toBe(target.id)
  })

  it('preserves an active backend run during startup restore', async () => {
    const restored = makeSessionWithStats('session-restored', 'Restored')

    setSessions([restored])
    localStorage.setItem(STORAGE_KEYS.LAST_SESSION, restored.id)
    statusMock.mockResolvedValue({ running: true, provider: 'openai', model: 'gpt-5' })
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: restored.id,
      exists: true,
      messageCount: 0,
    })

    await restoreForCurrentProject({ preserveActiveRun: true })

    expect(cancelMock).not.toHaveBeenCalled()
    expect(currentSession()?.id).toBe(restored.id)
    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      restored.id,
      '/workspace',
      expect.objectContaining({ title: restored.name, messages: [] })
    )
  })

  it('switches sessions even if the backend still reports a running session', async () => {
    const source = makeSessionWithStats('session-active', 'Active')
    const target = makeSessionWithStats('session-target', 'Target')

    setSessions([source, target])
    setCurrentSession(source)
    statusMock.mockResolvedValue({ running: true, provider: 'openai', model: 'gpt-5' })

    await switchSession(target.id)

    expect(statusMock).not.toHaveBeenCalled()
    expect(cancelMock).not.toHaveBeenCalled()
    expect(currentSession()?.id).toBe(target.id)
  })

  it('creates a new session without cancelling an active run', async () => {
    const existing = makeSession('session-active', 'Active')
    const created = makeSession('session-created', 'Another session')

    setCurrentSession(existing)
    setSessions([makeSessionWithStats(existing.id, existing.name)])
    statusMock.mockResolvedValue({ running: true, provider: 'openai', model: 'gpt-5' })
    dbCreateSessionMock.mockResolvedValue(created)
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: created.id,
      exists: true,
      messageCount: 0,
    })

    const result = await createNewSession('Another session')

    expect(statusMock).not.toHaveBeenCalled()
    expect(cancelMock).not.toHaveBeenCalled()
    expect(dbCreateSessionMock).toHaveBeenCalledWith('Another session', 'project-1')
    expect(result.id).toBe(created.id)
    expect(currentSession()?.id).toBe(created.id)
  })

  it('uses override project metadata for createNewSession project-scoped effects', async () => {
    const existing = makeSession('session-active', 'Active', 'project-1')
    const created = makeSession('session-created', 'Override session', 'project-2')

    mockProject = {
      id: 'project-1',
      name: 'Workspace 1',
      directory: '/workspace-1',
      createdAt: 0,
      updatedAt: 0,
      lastOpenedAt: 0,
    }
    mockProjects = [
      mockProject,
      {
        id: 'project-2',
        name: 'Workspace 2',
        directory: '/workspace-2',
        createdAt: 0,
        updatedAt: 0,
        lastOpenedAt: 0,
      },
    ]

    setCurrentSession(existing)
    setSessions([makeSessionWithStats(existing.id, existing.name, existing.projectId)])
    dbCreateSessionMock.mockResolvedValue(created)
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: created.id,
      exists: true,
      messageCount: 0,
    })

    await createNewSession('Override session', 'project-2')

    expect(dbCreateSessionMock).toHaveBeenCalledWith('Override session', 'project-2')
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-2', created.id)
    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      created.id,
      '/workspace-2',
      expect.objectContaining({ title: created.name, messages: [] })
    )
  })

  it('uses captured project cwd when notifying createNewSession after mid-flight project change', async () => {
    const created = makeSession('session-created', 'Another session')
    const deferredCreate = createDeferred<Session>()

    dbCreateSessionMock.mockImplementationOnce(() => deferredCreate.promise)
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: created.id,
      exists: true,
      messageCount: 0,
    })

    const createPromise = createNewSession('Another session')

    mockProject = {
      ...mockProject,
      id: 'project-2',
      directory: '/workspace-2',
      name: 'Workspace 2',
    }

    deferredCreate.resolve(created)
    await createPromise

    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      created.id,
      '/workspace',
      expect.objectContaining({ title: created.name, messages: [] })
    )
  })

  it('does not fall back to the ambient project cwd when a created session is unbound', async () => {
    const created = { ...makeSession('session-created', 'Detached session'), projectId: undefined }

    dbCreateSessionMock.mockResolvedValue(created)
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: created.id,
      exists: true,
      messageCount: 0,
    })

    await createNewSession('Detached session')

    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      created.id,
      undefined,
      expect.objectContaining({ title: created.name, messages: [] })
    )
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith(undefined, created.id)
  })

  it('reuses the current empty untitled session instead of creating duplicates', async () => {
    const existing = makeSession('session-empty', 'New Chat')

    setCurrentSession(existing)
    setSessions([makeSessionWithStats(existing.id, existing.name)])
    setMessages([])

    const result = await createNewSession()

    expect(result.id).toBe(existing.id)
    expect(dbCreateSessionMock).not.toHaveBeenCalled()
    expect(notifySessionOpenedMock).not.toHaveBeenCalled()
  })

  it('does not reuse stale current empty session when it is no longer in session list', async () => {
    const stale = makeSession('session-stale', 'New Chat')
    const created = makeSession('session-created', 'New Chat')

    dbCreateSessionMock.mockResolvedValue(created)
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: created.id,
      exists: true,
      messageCount: 0,
    })

    setCurrentSession(stale)
    setSessions([])
    setMessages([])

    const result = await createNewSession()

    expect(result.id).toBe(created.id)
    expect(dbCreateSessionMock).toHaveBeenCalledTimes(1)
    expect(dbCreateSessionMock).toHaveBeenCalledWith('New Chat', 'project-1')
    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      created.id,
      '/workspace',
      expect.objectContaining({ title: created.name, messages: [] })
    )
  })

  it('deduplicates concurrent new-session requests', async () => {
    const created = makeSession('session-new', 'New Chat')
    let resolveCreate: ((value: Session) => void) | undefined

    dbCreateSessionMock.mockImplementation(
      () =>
        new Promise<Session>((resolve) => {
          resolveCreate = resolve
        })
    )
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: created.id,
      exists: true,
      messageCount: 0,
    })

    const first = createNewSession()
    const second = createNewSession()

    await Promise.resolve()
    await Promise.resolve()

    expect(dbCreateSessionMock).toHaveBeenCalledTimes(1)

    resolveCreate?.(created)

    const [firstResult, secondResult] = await Promise.all([first, second])
    expect(firstResult.id).toBe(created.id)
    expect(secondResult.id).toBe(created.id)
  })

  it('creates separate sessions when creation targets differ', async () => {
    const createdProject1 = makeSession('session-project-1', 'New Chat', 'project-1')
    const createdProject2 = makeSession('session-project-2', 'New Chat', 'project-2')
    let resolveProject1: ((value: Session) => void) | undefined
    let resolveProject2: ((value: Session) => void) | undefined

    dbCreateSessionMock
      .mockImplementationOnce(
        () =>
          new Promise<Session>((resolve) => {
            resolveProject1 = resolve
          })
      )
      .mockImplementationOnce(
        () =>
          new Promise<Session>((resolve) => {
            resolveProject2 = resolve
          })
      )

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: createdProject1.id,
      exists: true,
      messageCount: 0,
    })

    const first = createNewSession('New Chat', 'project-1')
    const second = createNewSession('New Chat', 'project-2')

    await Promise.resolve()
    await Promise.resolve()

    expect(dbCreateSessionMock).toHaveBeenCalledTimes(2)
    expect(dbCreateSessionMock).toHaveBeenNthCalledWith(1, 'New Chat', 'project-1')
    expect(dbCreateSessionMock).toHaveBeenNthCalledWith(2, 'New Chat', 'project-2')

    resolveProject1?.(createdProject1)
    resolveProject2?.(createdProject2)

    const [firstResult, secondResult] = await Promise.all([first, second])

    expect(firstResult.id).toBe(createdProject1.id)
    expect(secondResult.id).toBe(createdProject2.id)
  })

  it('deduplicates by request key even while another key is in flight', async () => {
    const createdProject1 = makeSession('session-project-1', 'New Chat', 'project-1')
    const createdProject2 = makeSession('session-project-2', 'New Chat', 'project-2')
    let resolveProject1: ((value: Session) => void) | undefined
    let resolveProject2: ((value: Session) => void) | undefined

    dbCreateSessionMock
      .mockImplementationOnce(
        () =>
          new Promise<Session>((resolve) => {
            resolveProject1 = resolve
          })
      )
      .mockImplementationOnce(
        () =>
          new Promise<Session>((resolve) => {
            resolveProject2 = resolve
          })
      )

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: createdProject1.id,
      exists: true,
      messageCount: 0,
    })

    const firstProject1 = createNewSession('New Chat', 'project-1')
    const firstProject2 = createNewSession('New Chat', 'project-2')
    const secondProject1 = createNewSession('New Chat', 'project-1')

    await Promise.resolve()
    await Promise.resolve()

    expect(dbCreateSessionMock).toHaveBeenCalledTimes(2)
    expect(dbCreateSessionMock).toHaveBeenNthCalledWith(1, 'New Chat', 'project-1')
    expect(dbCreateSessionMock).toHaveBeenNthCalledWith(2, 'New Chat', 'project-2')

    resolveProject1?.(createdProject1)
    resolveProject2?.(createdProject2)

    const [firstProject1Result, firstProject2Result, secondProject1Result] = await Promise.all([
      firstProject1,
      firstProject2,
      secondProject1,
    ])

    expect(firstProject1Result.id).toBe(createdProject1.id)
    expect(secondProject1Result.id).toBe(createdProject1.id)
    expect(firstProject2Result.id).toBe(createdProject2.id)
  })

  it('recovers detached session before notifySessionOpened so backend sees authoritative transcript (regression)', async () => {
    // This test verifies the fix for: detached desktop session recovery must happen BEFORE
    // notifySessionOpened() is called, so the snapshot sent to the backend contains the
    // authoritative recovered transcript rather than stale pre-recovery messages.
    isTauriRuntime = true
    const sessionOne = makeSessionWithStats('session-1', 'First session')
    const sessionTwo = makeSessionWithStats('session-2', 'Second session')

    const staleCachedMessage = {
      id: 'stale-cached',
      sessionId: sessionOne.id,
      role: 'assistant' as const,
      content: 'stale cached reply',
      createdAt: 10,
    }
    const recoveredAuthoritativeMessage = {
      id: 'authoritative-recovered',
      sessionId: sessionOne.id,
      role: 'assistant' as const,
      content: 'fresh authoritative reply from backend',
      createdAt: 11,
    }

    // Simulate that sessionOne needs recovery and has stale cached messages
    sessionNeedsAuthoritativeRecoveryMock.mockReturnValue(true)
    setSessions([sessionOne, sessionTwo])
    setCurrentSession(sessionTwo)
    setMessages([staleCachedMessage])

    // Mock the backend to return the authoritative recovered messages
    loadSessionMock.mockResolvedValue({
      id: sessionOne.id,
      messages: [recoveredAuthoritativeMessage],
    })

    getMessagesMock.mockResolvedValue([recoveredAuthoritativeMessage])
    notifySessionOpenedMock.mockResolvedValue({
      sessionId: sessionOne.id,
      exists: true,
      messageCount: 1,
    })

    await switchSession(sessionOne.id)

    // Verify that notifySessionOpened was called with the RECOVERED authoritative message
    // NOT the stale cached message
    expect(notifySessionOpenedMock).toHaveBeenCalledWith(
      sessionOne.id,
      '/workspace',
      expect.objectContaining({
        title: sessionOne.name,
        messages: expect.arrayContaining([
          expect.objectContaining({
            id: 'authoritative-recovered',
            content: 'fresh authoritative reply from backend',
          }),
        ]),
      })
    )

    // Verify the stale message is NOT in the snapshot
    const snapshot = notifySessionOpenedMock.mock.calls[0]?.[2]
    expect(snapshot?.messages).not.toEqual(
      expect.arrayContaining([expect.objectContaining({ id: 'stale-cached' })])
    )

    // Verify recovery was cleared
    expect(clearSessionNeedsAuthoritativeRecoveryMock).toHaveBeenCalledWith(sessionOne.id)
  })
})
