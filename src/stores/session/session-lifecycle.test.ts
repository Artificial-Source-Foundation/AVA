import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { STORAGE_KEYS } from '../../config/constants'
import type { Session, SessionWithStats } from '../../types'

const notifySessionOpenedMock = vi.fn()
const dbArchiveSessionMock = vi.fn()
const dbCreateSessionMock = vi.fn()
const dbDeleteSessionMock = vi.fn()
const getMessagesMock = vi.fn()
const getAgentsMock = vi.fn()
const getFileOperationsMock = vi.fn()
const getTerminalExecutionsMock = vi.fn()
const getMemoryItemsMock = vi.fn()
const getCheckpointsMock = vi.fn()
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
  notifySessionOpened: (...args: unknown[]) => notifySessionOpenedMock(...args),
}))

vi.mock('../../services/database', () => ({
  archiveSession: (...args: unknown[]) => dbArchiveSessionMock(...args),
  createSession: (...args: unknown[]) => dbCreateSessionMock(...args),
  deleteSession: (...args: unknown[]) => dbDeleteSessionMock(...args),
  getAgents: (...args: unknown[]) => getAgentsMock(...args),
  getArchivedSessions: vi.fn(),
  getCheckpoints: (...args: unknown[]) => getCheckpointsMock(...args),
  getFileOperations: (...args: unknown[]) => getFileOperationsMock(...args),
  getMemoryItems: (...args: unknown[]) => getMemoryItemsMock(...args),
  getMessages: (...args: unknown[]) => getMessagesMock(...args),
  getSessionsWithStats: vi.fn(),
  getTerminalExecutions: (...args: unknown[]) => getTerminalExecutionsMock(...args),
  updateSession: vi.fn(),
}))

vi.mock('../../services/logger', () => ({
  logDebug: vi.fn(),
  logError: vi.fn(),
  logInfo: vi.fn(),
  logWarn: vi.fn(),
}))

vi.mock('../../services/rust-bridge', () => ({
  rustAgent: {
    cancel: (...args: unknown[]) => cancelMock(...args),
    status: (...args: unknown[]) => statusMock(...args),
  },
}))

vi.mock('../project', () => ({
  useProject: () => ({
    currentProject: () => mockProject,
  }),
}))

vi.mock('../session-persistence', () => ({
  getLastSessionForProject: vi.fn(),
  setLastSessionForProject: vi.fn(),
}))

import {
  archiveSession,
  createNewSession,
  deleteSessionPermanently,
  restoreForCurrentProject,
  switchSession,
} from './session-lifecycle'
import {
  currentSession,
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
    vi.clearAllMocks()
    localStorage.clear()
    resetSessionState()
    mockProject = {
      id: 'project-1',
      name: 'Workspace',
      directory: '/workspace',
      createdAt: 0,
      updatedAt: 0,
      lastOpenedAt: 0,
    }

    getMessagesMock.mockResolvedValue([])
    getAgentsMock.mockResolvedValue([])
    getFileOperationsMock.mockResolvedValue([])
    getTerminalExecutionsMock.mockResolvedValue([])
    getMemoryItemsMock.mockResolvedValue([])
    getCheckpointsMock.mockResolvedValue([])
    dbDeleteSessionMock.mockResolvedValue(undefined)
    statusMock.mockResolvedValue({ running: false, provider: 'openai', model: 'gpt-5' })
  })

  afterEach(() => {
    localStorage.clear()
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
      '/other-workspace',
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
    }

    notifySessionOpenedMock.mockResolvedValue({
      sessionId: target.id,
      exists: true,
      messageCount: 2,
    })
    setSessions([target])
    getMessagesMock.mockResolvedValueOnce([userMessage, assistantMessage])

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
        },
      ],
    })
  })

  it('best-effort cancels an active run before switching sessions', async () => {
    const source = makeSessionWithStats('session-active', 'Active')
    const target = makeSessionWithStats('session-target', 'Target')

    setSessions([source, target])
    setCurrentSession(source)

    statusMock
      .mockResolvedValueOnce({ running: true, provider: 'openai', model: 'gpt-5' })
      .mockResolvedValueOnce({ running: false, provider: 'openai', model: 'gpt-5' })

    await switchSession(target.id)

    expect(cancelMock).toHaveBeenCalledTimes(1)
    expect(statusMock).toHaveBeenNthCalledWith(1, { sessionId: source.id })
    expect(cancelMock).toHaveBeenCalledWith({ sessionId: source.id })
    expect(statusMock).toHaveBeenNthCalledWith(2, { sessionId: source.id })
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

  it('does not switch sessions when the backend stays running after cancel confirmation', async () => {
    vi.useFakeTimers()

    const source = makeSessionWithStats('session-active', 'Active')
    const target = makeSessionWithStats('session-target', 'Target')

    setSessions([source, target])
    setCurrentSession(source)
    statusMock.mockResolvedValue({ running: true, provider: 'openai', model: 'gpt-5' })

    const switchPromise = switchSession(target.id)
    await vi.runAllTimersAsync()
    await switchPromise

    expect(cancelMock).toHaveBeenCalledTimes(1)
    expect(statusMock).toHaveBeenCalledWith({ sessionId: source.id })
    expect(cancelMock).toHaveBeenCalledWith({ sessionId: source.id })
    expect(currentSession()?.id).toBe(source.id)
    expect(getMessagesMock).not.toHaveBeenCalled()
    expect(notifySessionOpenedMock).not.toHaveBeenCalled()

    vi.useRealTimers()
  })

  it('does not create a new session when the backend stays running after cancel confirmation', async () => {
    vi.useFakeTimers()

    const existing = makeSession('session-active', 'Active')

    setCurrentSession(existing)
    setSessions([makeSessionWithStats(existing.id, existing.name)])
    statusMock.mockResolvedValue({ running: true, provider: 'openai', model: 'gpt-5' })

    const createPromise = expect(createNewSession('Another session')).rejects.toThrow(
      'Cannot change sessions while the backend run remains active after cancel confirmation'
    )
    await vi.runAllTimersAsync()

    await createPromise
    expect(cancelMock).toHaveBeenCalledTimes(1)
    expect(statusMock).toHaveBeenCalledWith({ sessionId: existing.id })
    expect(cancelMock).toHaveBeenCalledWith({ sessionId: existing.id })
    expect(dbCreateSessionMock).not.toHaveBeenCalled()
    expect(currentSession()?.id).toBe(existing.id)
    expect(notifySessionOpenedMock).not.toHaveBeenCalled()

    vi.useRealTimers()
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
})
