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

import { archiveSession, deleteSessionPermanently, switchSession } from './session-lifecycle'
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
})
