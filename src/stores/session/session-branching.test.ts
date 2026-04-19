import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { STORAGE_KEYS } from '../../config/constants'
import type { Message, Session, SessionWithStats } from '../../types'

let isTauriRuntime = false
const buildSessionBaseEndpointMock = vi.fn(
  (frontendSessionId: string, suffix?: string) =>
    `/api/sessions/backend-${frontendSessionId}${suffix ? `/${suffix}` : ''}`
)
const dbCreateSessionMock = vi.fn()
const dbInsertMessagesMock = vi.fn()
const getMessagesMock = vi.fn()
const logInfoMock = vi.fn()
const logWarnMock = vi.fn()
const setLastSessionForProjectMock =
  vi.fn<(projectId: string | undefined, sessionId: string) => void>()

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

vi.mock('../../services/logger', () => ({
  logInfo: (...args: unknown[]) => logInfoMock(...args),
  logWarn: (...args: unknown[]) => logWarnMock(...args),
}))

vi.mock('../../services/web-session-identity', () => ({
  buildSessionBaseEndpoint: (frontendSessionId: string, suffix?: string) =>
    buildSessionBaseEndpointMock(frontendSessionId, suffix),
  canonicalizeSessionId: (sessionId: string) =>
    sessionId === 'backend-source' ? 'source' : sessionId,
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

import { branchAtMessage, duplicateSession, forkSession } from './session-branching'
import {
  currentSession,
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

function resetSessionState(): void {
  setCurrentSession(null)
  setSessions([])
  setMessages([])
  setIsLoadingMessages(false)
}

describe('session-branching web mode', () => {
  const fetchMock = vi.fn<typeof fetch>()

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
    vi.stubGlobal('fetch', fetchMock)
    getMessagesMock.mockResolvedValue([])
  })

  afterEach(() => {
    vi.unstubAllGlobals()
    resetSessionState()
    localStorage.clear()
  })

  it('duplicates through the web mutation adapter and keeps duplicate sessions root-level', async () => {
    const source = makeSessionWithStats('source', 'Source Session')
    const duplicatedMessages = [makeMessage('copied-1', 'copy-1', 'Copied from backend')]
    setSessions([source])

    fetchMock.mockResolvedValue({
      ok: true,
      json: async () => ({
        id: 'copy-1',
        title: 'Source Session (copy)',
        message_count: 1,
        last_preview: 'Copied from backend',
        created_at: '2026-04-18T19:00:00Z',
        updated_at: '2026-04-18T19:00:01Z',
      }),
    } as Response)
    getMessagesMock.mockResolvedValue(duplicatedMessages)

    await duplicateSession(source.id)

    expect(buildSessionBaseEndpointMock).toHaveBeenCalledWith(source.id, 'duplicate')
    expect(fetchMock).toHaveBeenCalledWith(
      '/api/sessions/backend-source/duplicate',
      expect.objectContaining({ method: 'POST' })
    )

    const request = fetchMock.mock.calls[0]?.[1]
    const payload = JSON.parse(String(request?.body)) as {
      id: string
      kind: string
      name: string
    }
    expect(payload.id).toEqual(expect.any(String))
    expect(payload.kind).toBe('duplicate')
    expect(payload.name).toBe('Source Session (copy)')

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

    fetchMock.mockResolvedValue({
      ok: true,
      json: async () => ({
        id: 'fork-1',
        title: 'Source Session (fork)',
        parent_session_id: 'backend-source',
        message_count: 0,
        last_preview: 'Fork preview',
        created_at: '2026-04-18T19:05:00Z',
        updated_at: '2026-04-18T19:05:01Z',
      }),
    } as Response)

    await forkSession(source.id)

    expect(buildSessionBaseEndpointMock).toHaveBeenCalledWith(source.id, 'duplicate')

    const request = fetchMock.mock.calls[0]?.[1]
    const payload = JSON.parse(String(request?.body)) as {
      id: string
      kind: string
      name: string
    }
    expect(payload.id).toEqual(expect.any(String))
    expect(payload.kind).toBe('fork')
    expect(payload.name).toBe('Source Session (fork)')

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

  it('rejects branch-at-message in web mode instead of creating a local-only branch', async () => {
    const source = makeSessionWithStats('source', 'Source Session')
    const sourceMessages = [makeMessage('m-1', source.id), makeMessage('m-2', source.id)]

    setSessions([source])
    setCurrentSession(source)
    setMessages(sourceMessages)

    await expect(branchAtMessage('m-1')).rejects.toThrow(
      'Branching from a specific message is not supported in web mode yet.'
    )

    expect(dbCreateSessionMock).not.toHaveBeenCalled()
    expect(dbInsertMessagesMock).not.toHaveBeenCalled()
    expect(fetchMock).not.toHaveBeenCalled()
    expect(logWarnMock).toHaveBeenCalledWith(
      'Session',
      'Web branchAtMessage requires a backend-backed endpoint and is disabled',
      { sessionId: source.id, messageId: 'm-1' }
    )
    expect(currentSession()?.id).toBe(source.id)
    expect(messages()).toEqual(sourceMessages)
    expect(sessions()).toEqual([source])
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBeNull()
  })
})
