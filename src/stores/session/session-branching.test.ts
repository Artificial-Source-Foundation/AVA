import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { STORAGE_KEYS } from '../../config/constants'
import type { Message, Session, SessionWithStats } from '../../types'

let isTauriRuntime = false
const dbCreateSessionMock = vi.fn()
const dbInsertMessagesMock = vi.fn()
const getMessagesMock = vi.fn()
const setLastSessionForProjectMock =
  vi.fn<(projectId: string | undefined, sessionId: string) => void>()
const cloneSessionInWebModeMock = vi.fn()
const branchSessionAtMessageInWebModeMock = vi.fn()

let mockProject = {
  id: 'project-1',
  name: 'Workspace',
  directory: '/workspace',
  createdAt: 0,
  updatedAt: 0,
  lastOpenedAt: 0,
}

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => isTauriRuntime,
}))

vi.mock('../../services/database', () => ({
  createSession: (...args: unknown[]) => dbCreateSessionMock(...args),
  getMessages: (...args: unknown[]) => getMessagesMock(...args),
  insertMessages: (...args: unknown[]) => dbInsertMessagesMock(...args),
}))

vi.mock('../../services/web-session-mutations', () => ({
  cloneSessionInWebMode: (...args: unknown[]) => cloneSessionInWebModeMock(...args),
  branchSessionAtMessageInWebMode: (...args: unknown[]) =>
    branchSessionAtMessageInWebModeMock(...args),
}))

vi.mock('../project', () => ({
  useProject: () => ({
    currentProject: () => mockProject,
  }),
}))

vi.mock('../session-persistence', () => ({
  setLastSessionForProject: (projectId: string | undefined, sessionId: string) =>
    setLastSessionForProjectMock(projectId, sessionId),
}))

import {
  branchAtMessage,
  canBranchAtMessage,
  duplicateSession,
  forkSession,
} from './session-branching'
import {
  currentSession,
  isLoadingMessages,
  messages,
  sessions,
  setCurrentSession,
  setIsLoadingMessages,
  setMessages,
  setSessions,
} from './session-state'

function makeSession(
  id: string,
  name = `Session ${id}`,
  projectId: string | undefined = 'project-1',
  parentSessionId?: string
): Session {
  return {
    id,
    name,
    projectId,
    parentSessionId,
    createdAt: 1,
    updatedAt: 1,
    status: 'active',
    metadata: {},
  }
}

function makeSessionWithStats(
  id: string,
  name = `Session ${id}`,
  projectId: string | undefined = 'project-1',
  parentSessionId?: string
): SessionWithStats {
  return {
    ...makeSession(id, name, projectId, parentSessionId),
    messageCount: 0,
    totalTokens: 0,
    lastPreview: '',
  }
}

function makeMessage(id: string, sessionId: string, content = `Message ${id}`): Message {
  return {
    id,
    sessionId,
    role: 'user',
    content,
    createdAt: 1,
  }
}

function makeLinkedMessages(sessionId: string): Message[] {
  return [
    makeMessage('m-1', sessionId, 'Root message'),
    {
      ...makeMessage('m-2', sessionId, 'Child message'),
      metadata: {
        parentId: 'm-1',
        parent_id: 'm-1',
      },
    },
  ]
}

function resetSessionState(): void {
  setCurrentSession(null)
  setSessions([])
  setMessages([])
  setIsLoadingMessages(false)
}

describe('canBranchAtMessage capability', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    isTauriRuntime = false
  })

  afterEach(() => {
    isTauriRuntime = false
  })

  it('returns true in Tauri mode', () => {
    isTauriRuntime = true
    expect(canBranchAtMessage()).toBe(true)
  })

  it('returns false in web mode', () => {
    isTauriRuntime = false
    expect(canBranchAtMessage()).toBe(false)
  })
})

describe('session-branching web mode', () => {
  beforeEach(() => {
    isTauriRuntime = false
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
    cloneSessionInWebModeMock.mockReset()
    branchSessionAtMessageInWebModeMock.mockReset()
  })

  afterEach(() => {
    resetSessionState()
    localStorage.clear()
  })

  it('duplicates through the web mutation adapter and keeps duplicate sessions root-level', async () => {
    const source = makeSessionWithStats('source', 'Source Session')
    const duplicatedMessages = [makeMessage('copied-1', 'copy-1', 'Copied from backend')]
    setSessions([source])

    getMessagesMock.mockResolvedValue(duplicatedMessages)
    cloneSessionInWebModeMock.mockResolvedValue({
      session: {
        id: 'copy-1',
        name: 'Source Session (copy)',
        projectId: 'project-1',
        parentSessionId: undefined,
        createdAt: 1,
        updatedAt: 1,
        status: 'active',
        metadata: {},
      },
      stats: {
        messageCount: 1,
        totalTokens: 0,
        lastPreview: 'Copied from backend',
      },
    })

    await duplicateSession(source.id)

    expect(cloneSessionInWebModeMock).toHaveBeenCalledWith({
      kind: 'duplicate',
      sourceSessionId: source.id,
      sourceSessionName: source.name,
      projectId: 'project-1',
    })
    expect(dbCreateSessionMock).not.toHaveBeenCalled()
    expect(dbInsertMessagesMock).not.toHaveBeenCalled()

    expect(sessions()[0]).toMatchObject({
      id: 'copy-1',
      name: 'Source Session (copy)',
      parentSessionId: undefined,
      messageCount: 1,
      lastPreview: 'Copied from backend',
    })
    expect(currentSession()?.id).toBe('copy-1')
    expect(messages()).toEqual(duplicatedMessages)
    expect(getMessagesMock).toHaveBeenCalledWith('copy-1')
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe('copy-1')
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-1', 'copy-1')
  })

  it('forks through the web mutation adapter and records the source as parent', async () => {
    const source = makeSessionWithStats('source', 'Source Session')
    setSessions([source])

    cloneSessionInWebModeMock.mockResolvedValue({
      session: {
        id: 'fork-1',
        name: 'Source Session (fork)',
        projectId: 'project-1',
        parentSessionId: source.id,
        createdAt: 1,
        updatedAt: 1,
        status: 'active',
        metadata: {},
      },
      stats: {
        messageCount: 0,
        totalTokens: 0,
        lastPreview: 'Fork preview',
      },
    })

    await forkSession(source.id)

    expect(cloneSessionInWebModeMock).toHaveBeenCalledWith({
      kind: 'fork',
      sourceSessionId: source.id,
      sourceSessionName: source.name,
      projectId: 'project-1',
    })
    expect(dbCreateSessionMock).not.toHaveBeenCalled()
    expect(dbInsertMessagesMock).not.toHaveBeenCalled()

    expect(sessions()[0]).toMatchObject({
      id: 'fork-1',
      name: 'Source Session (fork)',
      parentSessionId: source.id,
      lastPreview: 'Fork preview',
    })
    expect(currentSession()?.id).toBe('fork-1')
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe('fork-1')
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-1', 'fork-1')
  })

  it('duplicates using the source session project when ambient project differs', async () => {
    const source = makeSessionWithStats('source', 'Source Session', 'project-source')
    setSessions([source])
    mockProject = {
      ...mockProject,
      id: 'project-ambient',
    }

    cloneSessionInWebModeMock.mockResolvedValue({
      session: {
        id: 'copy-1',
        name: 'Source Session (copy)',
        projectId: 'project-source',
        parentSessionId: undefined,
        createdAt: 1,
        updatedAt: 1,
        status: 'active',
        metadata: {},
      },
      stats: {
        messageCount: 0,
        totalTokens: 0,
        lastPreview: '',
      },
    })

    await duplicateSession(source.id)

    expect(cloneSessionInWebModeMock).toHaveBeenCalledWith(
      expect.objectContaining({
        sourceSessionId: source.id,
        projectId: 'project-source',
      })
    )
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-source', 'copy-1')
  })

  it('forks using the source session project when ambient project differs', async () => {
    const source = makeSessionWithStats('source', 'Source Session', 'project-source')
    setSessions([source])
    mockProject = {
      ...mockProject,
      id: 'project-ambient',
    }

    cloneSessionInWebModeMock.mockResolvedValue({
      session: {
        id: 'fork-1',
        name: 'Source Session (fork)',
        projectId: 'project-source',
        parentSessionId: source.id,
        createdAt: 1,
        updatedAt: 1,
        status: 'active',
        metadata: {},
      },
      stats: {
        messageCount: 0,
        totalTokens: 0,
        lastPreview: '',
      },
    })

    await forkSession(source.id)

    expect(cloneSessionInWebModeMock).toHaveBeenCalledWith(
      expect.objectContaining({
        sourceSessionId: source.id,
        projectId: 'project-source',
      })
    )
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-source', 'fork-1')
  })

  it.each([
    {
      name: 'duplicateSession',
      action: duplicateSession,
      clonedSession: {
        id: 'copy-1',
        name: 'Source Session (copy)',
        parentSessionId: undefined,
      },
    },
    {
      name: 'forkSession',
      action: forkSession,
      clonedSession: {
        id: 'fork-1',
        name: 'Source Session (fork)',
        parentSessionId: 'source',
      },
    },
  ])('activates the cloned session even when $name cannot load hydrated messages', async ({
    action,
    clonedSession,
  }) => {
    const source = makeSessionWithStats('source', 'Source Session')
    setSessions([source])

    cloneSessionInWebModeMock.mockResolvedValue({
      session: {
        ...clonedSession,
        projectId: 'project-1',
        createdAt: 1,
        updatedAt: 1,
        status: 'active',
        metadata: {},
      },
      stats: {
        messageCount: 1,
        totalTokens: 0,
        lastPreview: 'Preview from backend',
      },
    })
    getMessagesMock.mockRejectedValueOnce(new Error('load failed'))

    await action(source.id)

    expect(currentSession()?.id).toBe(clonedSession.id)
    expect(messages()).toEqual([])
    expect(isLoadingMessages()).toBe(false)
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe(clonedSession.id)
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-1', clonedSession.id)
  })

  it.each([
    {
      name: 'duplicateSession',
      action: duplicateSession,
      cloneKind: 'duplicate',
    },
    {
      name: 'forkSession',
      action: forkSession,
      cloneKind: 'fork',
    },
  ])('preserves store state when $name fails to clone in web mode', async ({
    action,
    cloneKind,
  }) => {
    const source = makeSessionWithStats('source', 'Source Session')
    const sourceMessages = [makeMessage('m-1', source.id), makeMessage('m-2', source.id)]
    const cloneError = new Error('clone failed')

    setSessions([source])
    setCurrentSession(source)
    setMessages(sourceMessages)

    const setItemSpy = vi.spyOn(Storage.prototype, 'setItem')
    localStorage.setItem(STORAGE_KEYS.LAST_SESSION, 'source')

    const baselineSessions = structuredClone(sessions())
    const baselineCurrentSession = currentSession() ? structuredClone(currentSession()) : null
    const baselineMessages = structuredClone(messages())
    const baselineIsLoadingMessages = isLoadingMessages()

    cloneSessionInWebModeMock.mockRejectedValueOnce(cloneError)

    try {
      await expect(action(source.id)).rejects.toBe(cloneError)

      expect(cloneSessionInWebModeMock).toHaveBeenCalledTimes(1)
      expect(cloneSessionInWebModeMock).toHaveBeenCalledWith(
        expect.objectContaining({
          kind: cloneKind,
          sourceSessionId: source.id,
          sourceSessionName: source.name,
          projectId: 'project-1',
        })
      )

      expect(dbCreateSessionMock).not.toHaveBeenCalled()
      expect(dbInsertMessagesMock).not.toHaveBeenCalled()
      expect(getMessagesMock).not.toHaveBeenCalled()
      expect(setLastSessionForProjectMock).not.toHaveBeenCalled()

      expect(sessions()).toEqual(baselineSessions)
      expect(currentSession()).toEqual(baselineCurrentSession)
      expect(messages()).toEqual(baselineMessages)
      expect(isLoadingMessages()).toBe(baselineIsLoadingMessages)
      expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe('source')

      expect(setItemSpy).toHaveBeenCalledWith(STORAGE_KEYS.LAST_SESSION, 'source')
      expect(setItemSpy).toHaveBeenCalledTimes(1)
    } finally {
      setItemSpy.mockRestore()
    }
  })

  it('rejects branch-at-message in web mode instead of creating a local-only branch', async () => {
    const source = makeSessionWithStats('source', 'Source Session')
    const sourceMessages = [makeMessage('m-1', source.id), makeMessage('m-2', source.id)]
    const expectedError = new Error(
      'Branching from a specific message is not supported in web mode yet.'
    )

    setSessions([source])
    setCurrentSession(source)
    setMessages(sourceMessages)
    localStorage.setItem(STORAGE_KEYS.LAST_SESSION, 'source')
    branchSessionAtMessageInWebModeMock.mockRejectedValueOnce(expectedError)

    await expect(branchAtMessage('m-1')).rejects.toThrow(expectedError.message)

    expect(branchSessionAtMessageInWebModeMock).toHaveBeenCalledWith({
      sessionId: source.id,
      messageId: 'm-1',
    })
    expect(dbCreateSessionMock).not.toHaveBeenCalled()
    expect(dbInsertMessagesMock).not.toHaveBeenCalled()
    expect(cloneSessionInWebModeMock).not.toHaveBeenCalled()
    expect(currentSession()?.id).toBe(source.id)
    expect(messages()).toEqual(sourceMessages)
    expect(sessions()).toEqual([source])
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe('source')
  })
})

describe('session-branching desktop mode', () => {
  beforeEach(() => {
    isTauriRuntime = true
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
  })

  afterEach(() => {
    isTauriRuntime = false
    resetSessionState()
    localStorage.clear()
  })

  it('duplicates across projects using the source session ownership in desktop mode', async () => {
    const source = makeSessionWithStats('source', 'Source Session', 'project-source')
    const sourceMessages = [makeMessage('m-1', source.id, 'Source message')]
    const duplicated = makeSession('copy-1', 'Source Session (copy)', 'project-source')
    const duplicatedMessages = [makeMessage('copied-1', duplicated.id, 'Copied message')]

    mockProject = {
      ...mockProject,
      id: 'project-ambient',
    }

    setSessions([source])
    getMessagesMock.mockResolvedValueOnce(sourceMessages).mockResolvedValueOnce(duplicatedMessages)
    dbCreateSessionMock.mockResolvedValue(duplicated)
    dbInsertMessagesMock.mockResolvedValue(undefined)

    await duplicateSession(source.id)

    expect(dbCreateSessionMock).toHaveBeenCalledWith('Source Session (copy)', 'project-source')
    expect(dbInsertMessagesMock).toHaveBeenCalledWith([
      expect.objectContaining({ sessionId: duplicated.id, content: 'Source message' }),
    ])
    expect(getMessagesMock).toHaveBeenNthCalledWith(1, source.id)
    expect(getMessagesMock).toHaveBeenNthCalledWith(2, duplicated.id)
    expect(currentSession()?.id).toBe(duplicated.id)
    expect(sessions()[0]).toMatchObject({ id: duplicated.id, projectId: 'project-source' })
    expect(messages()).toEqual(duplicatedMessages)
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-source', duplicated.id)
    expect(setLastSessionForProjectMock).not.toHaveBeenCalledWith('project-ambient', duplicated.id)
  })

  it('remaps linked message references when duplicating and forking in desktop mode', async () => {
    const source = makeSessionWithStats('source', 'Source Session', 'project-source')
    const sourceMessages = makeLinkedMessages(source.id)
    const duplicated = makeSession('copy-1', 'Source Session (copy)', 'project-source')
    const forked = makeSession('fork-1', 'Source Session (fork)', 'project-source', source.id)

    setSessions([source])
    getMessagesMock.mockResolvedValue(sourceMessages)
    dbInsertMessagesMock.mockResolvedValue(undefined)

    dbCreateSessionMock.mockResolvedValueOnce(duplicated)
    await duplicateSession(source.id)

    const duplicatedMessages = dbInsertMessagesMock.mock.calls[0]?.[0] as Message[]
    expect(duplicatedMessages).toHaveLength(2)
    expect(duplicatedMessages[0]?.id).not.toBe('m-1')
    expect(duplicatedMessages[1]?.id).not.toBe('m-2')
    expect(duplicatedMessages[1]?.metadata).toEqual({
      parentId: duplicatedMessages[0]?.id,
      parent_id: duplicatedMessages[0]?.id,
    })

    dbCreateSessionMock.mockResolvedValueOnce(forked)
    await forkSession(source.id)

    const forkedMessages = dbInsertMessagesMock.mock.calls[1]?.[0] as Message[]
    expect(forkedMessages).toHaveLength(2)
    expect(forkedMessages[0]?.id).not.toBe('m-1')
    expect(forkedMessages[1]?.id).not.toBe('m-2')
    expect(forkedMessages[1]?.metadata).toEqual({
      parentId: forkedMessages[0]?.id,
      parent_id: forkedMessages[0]?.id,
    })
  })

  it('forks across projects using the source session ownership in desktop mode', async () => {
    const source = makeSessionWithStats('source', 'Source Session', 'project-source')
    const sourceMessages = [makeMessage('m-1', source.id, 'Source message')]
    const forked = makeSession('fork-1', 'Source Session (fork)', 'project-source', source.id)
    const forkedMessages = [makeMessage('forked-1', forked.id, 'Forked message')]

    mockProject = {
      ...mockProject,
      id: 'project-ambient',
    }

    setSessions([source])
    getMessagesMock.mockResolvedValueOnce(sourceMessages).mockResolvedValueOnce(forkedMessages)
    dbCreateSessionMock.mockResolvedValue(forked)
    dbInsertMessagesMock.mockResolvedValue(undefined)

    await forkSession(source.id)

    expect(dbCreateSessionMock).toHaveBeenCalledWith(
      'Source Session (fork)',
      'project-source',
      source.id
    )
    expect(dbInsertMessagesMock).toHaveBeenCalledWith([
      expect.objectContaining({ sessionId: forked.id, content: 'Source message' }),
    ])
    expect(getMessagesMock).toHaveBeenNthCalledWith(1, source.id)
    expect(getMessagesMock).toHaveBeenNthCalledWith(2, forked.id)
    expect(currentSession()?.id).toBe(forked.id)
    expect(sessions()[0]).toMatchObject({
      id: forked.id,
      projectId: 'project-source',
      parentSessionId: source.id,
    })
    expect(messages()).toEqual(forkedMessages)
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-source', forked.id)
    expect(setLastSessionForProjectMock).not.toHaveBeenCalledWith('project-ambient', forked.id)
  })

  it('branches at a message by regenerating copied message ids for persistence and active state', async () => {
    const source = makeSessionWithStats('source', 'Source Session', 'project-source')
    const sourceMessages = [
      makeMessage('m-1', source.id, 'First message'),
      makeMessage('m-2', source.id, 'Second message'),
    ]
    const branched = makeSession('branch-1', 'Source Session (branch)', 'project-source', source.id)

    mockProject = {
      ...mockProject,
      id: 'project-ambient',
    }

    dbCreateSessionMock.mockResolvedValue(branched)
    dbInsertMessagesMock.mockResolvedValue(undefined)

    setSessions([source])
    setCurrentSession(source)
    setMessages(sourceMessages)
    setIsLoadingMessages(true)

    await branchAtMessage('m-1')

    expect(dbCreateSessionMock).toHaveBeenCalledWith(
      'Source Session (branch)',
      'project-source',
      source.id
    )
    const insertedMessages = dbInsertMessagesMock.mock.calls[0]?.[0] as Message[]
    expect(insertedMessages).toEqual([
      expect.objectContaining({ sessionId: branched.id, content: 'First message' }),
    ])
    expect(insertedMessages[0]?.id).toBeDefined()
    expect(insertedMessages[0]?.id).not.toBe('m-1')
    expect(sourceMessages[0]?.id).toBe('m-1')
    expect(currentSession()?.id).toBe(branched.id)
    expect(messages()).toEqual(insertedMessages)
    expect(messages()[0]?.id).toBe(insertedMessages[0]?.id)
    expect(messages()[0]?.id).not.toBe('m-1')
    expect(isLoadingMessages()).toBe(false)
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe(branched.id)
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-source', branched.id)
  })

  it('remaps linked message references when branching in desktop mode', async () => {
    const source = makeSessionWithStats('source', 'Source Session', 'project-source')
    const sourceMessages = makeLinkedMessages(source.id)
    const branched = makeSession('branch-1', 'Source Session (branch)', 'project-source', source.id)

    dbCreateSessionMock.mockResolvedValue(branched)
    dbInsertMessagesMock.mockResolvedValue(undefined)

    setSessions([source])
    setCurrentSession(source)
    setMessages(sourceMessages)

    await branchAtMessage('m-2')

    const insertedMessages = dbInsertMessagesMock.mock.calls[0]?.[0] as Message[]
    expect(insertedMessages).toHaveLength(2)
    expect(insertedMessages[0]?.id).not.toBe('m-1')
    expect(insertedMessages[1]?.id).not.toBe('m-2')
    expect(insertedMessages[1]?.metadata).toEqual({
      parentId: insertedMessages[0]?.id,
      parent_id: insertedMessages[0]?.id,
    })
    expect(messages()[1]?.metadata).toEqual({
      parentId: insertedMessages[0]?.id,
      parent_id: insertedMessages[0]?.id,
    })
  })
})
