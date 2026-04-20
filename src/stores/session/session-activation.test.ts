import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { STORAGE_KEYS } from '../../config/constants'
import type { Message, Session } from '../../types'

const setCurrentSessionMock = vi.fn<(session: Session | null) => void>()
const setIsLoadingMessagesMock = vi.fn<(isLoading: boolean) => void>()
const setMessagesMock = vi.fn<(messages: Message[]) => void>()
const setLastSessionForProjectMock =
  vi.fn<(projectId: string | null | undefined, sessionId: string) => void>()

vi.mock('./session-state', () => ({
  setCurrentSession: (session: Session | null) => setCurrentSessionMock(session),
  setIsLoadingMessages: (isLoading: boolean) => setIsLoadingMessagesMock(isLoading),
  setMessages: (messages: Message[]) => setMessagesMock(messages),
  setAgents: vi.fn(),
  setFileOperations: vi.fn(),
  setTerminalExecutions: vi.fn(),
  setMemoryItems: vi.fn(),
}))

vi.mock('../session-persistence', () => ({
  setLastSessionForProject: (projectId: string | null | undefined, sessionId: string) =>
    setLastSessionForProjectMock(projectId, sessionId),
}))

import {
  activatePersistedSession,
  activatePersistedSessionMessages,
  finalizeSessionActivation,
  persistSelectedSession,
} from './session-activation'

function makeSession(overrides: Partial<Session> = {}): Session {
  return {
    id: 'session-1',
    name: 'Session 1',
    createdAt: 1,
    updatedAt: 2,
    status: 'active',
    ...overrides,
  }
}

describe('session-activation helpers', () => {
  beforeEach(() => {
    localStorage.clear()
    vi.clearAllMocks()
  })

  afterEach(() => {
    localStorage.clear()
    vi.clearAllMocks()
  })

  it('persistSelectedSession writes global and per-project selection', () => {
    const session = makeSession({ id: 'global-session' })

    persistSelectedSession('project-1', session.id)

    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-1', session.id)
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe(session.id)
  })

  it('finalizeSessionActivation applies state and persists by default while settling loading', () => {
    const session = makeSession()
    const applyActiveState = vi.fn()

    finalizeSessionActivation(session, { projectId: 'project-1', applyActiveState })

    expect(setCurrentSessionMock).toHaveBeenCalledWith(session)
    expect(applyActiveState).toHaveBeenCalledOnce()
    expect(setIsLoadingMessagesMock).toHaveBeenCalledWith(false)
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-1', session.id)
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe(session.id)
  })

  it('finalizeSessionActivation respects opt-out flags for persistence and loading settle', () => {
    const session = makeSession({ id: 'session-no-opt', projectId: 'project-2' })
    const applyActiveState = vi.fn()

    finalizeSessionActivation(session, {
      projectId: 'project-2',
      persistSelection: false,
      settleLoading: false,
      applyActiveState,
    })

    expect(setCurrentSessionMock).toHaveBeenCalledWith(session)
    expect(applyActiveState).toHaveBeenCalledOnce()
    expect(setIsLoadingMessagesMock).not.toHaveBeenCalled()
    expect(setLastSessionForProjectMock).not.toHaveBeenCalled()
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBeNull()
  })

  it('activatePersistedSession hydrates, applies loaded state, and returns loaded payload', async () => {
    const session = makeSession()
    const loaded = { payload: 'loaded' }
    const beforeLoad = vi.fn()
    const applyLoaded = vi.fn<(loadedValue: { payload: string }) => void>()
    const applyLoadFallback = vi.fn()
    const onLoadError = vi.fn()
    const load = vi.fn(async () => loaded)

    const result = await activatePersistedSession(session, {
      projectId: 'project-1',
      beforeLoad,
      load,
      applyLoaded,
      applyLoadFallback,
      onLoadError,
    })

    expect(setCurrentSessionMock).toHaveBeenCalledWith(session)
    expect(load).toHaveBeenCalledWith(session.id)
    expect(beforeLoad).toHaveBeenCalledOnce()
    expect(applyLoaded).toHaveBeenCalledWith(loaded)
    expect(applyLoadFallback).not.toHaveBeenCalled()
    expect(onLoadError).not.toHaveBeenCalled()
    expect(setIsLoadingMessagesMock).toHaveBeenCalledWith(true)
    expect(setIsLoadingMessagesMock).toHaveBeenCalledWith(false)
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-1', session.id)
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBe(session.id)
    expect(result).toBe(loaded)
  })

  it('activatePersistedSession keeps loading settled when startLoading is false', async () => {
    const session = makeSession({ id: 'session-no-initial-loading' })
    const loaded = { payload: 'loaded' }
    const load = vi.fn(async () => loaded)
    const applyLoaded = vi.fn<(loadedValue: { payload: string }) => void>()

    const result = await activatePersistedSession(session, {
      projectId: 'project-1',
      startLoading: false,
      load,
      applyLoaded,
    })

    expect(load).toHaveBeenCalledWith(session.id)
    expect(applyLoaded).toHaveBeenCalledWith(loaded)
    expect(setIsLoadingMessagesMock).toHaveBeenCalledWith(false)
    expect(setIsLoadingMessagesMock).not.toHaveBeenCalledWith(true)
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-1', session.id)
    expect(result).toBe(loaded)
  })

  it('activatePersistedSessionMessages passes loaded messages through setMessages', async () => {
    const session = makeSession({ id: 'session-messages' })
    const loadedMessages: Message[] = [
      {
        id: 'msg-1',
        sessionId: session.id,
        role: 'user',
        content: 'hello',
        createdAt: 171234,
      },
    ]
    const loadMessages = vi.fn(async () => loadedMessages)

    const result = await activatePersistedSessionMessages(session, 'project-1', loadMessages)

    expect(loadMessages).toHaveBeenCalledWith(session.id)
    expect(result).toBe(loadedMessages)
    expect(setMessagesMock).toHaveBeenCalledWith(loadedMessages)
    expect(setCurrentSessionMock).toHaveBeenCalledWith(session)
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-1', session.id)
  })

  it('activatePersistedSession runs fallback path when load fails and still returns undefined', async () => {
    const session = makeSession({ id: 'session-fallback' })
    const error = new Error('load failed')
    const load = vi.fn(async () => {
      throw error
    })
    const applyLoaded = vi.fn()
    const applyLoadFallback = vi.fn()
    const onLoadError = vi.fn()

    const result = await activatePersistedSession(session, {
      projectId: 'project-1',
      load,
      applyLoaded,
      applyLoadFallback,
      onLoadError,
    })

    expect(load).toHaveBeenCalledWith(session.id)
    expect(onLoadError).toHaveBeenCalledWith(error)
    expect(applyLoaded).not.toHaveBeenCalled()
    expect(applyLoadFallback).toHaveBeenCalledOnce()
    expect(setIsLoadingMessagesMock).toHaveBeenCalledWith(true)
    expect(setIsLoadingMessagesMock).toHaveBeenCalledWith(false)
    expect(setLastSessionForProjectMock).toHaveBeenCalledWith('project-1', session.id)
    expect(result).toBeUndefined()
  })

  it('activatePersistedSession respects shouldSettle=false to skip final loading settle', async () => {
    const session = makeSession()
    const loaded = { payload: 'loaded' }
    const load = vi.fn(async () => loaded)
    const applyLoaded = vi.fn<(loadedValue: { payload: string }) => void>()
    const shouldSettle = vi.fn(() => false)

    await activatePersistedSession(session, {
      projectId: 'project-1',
      load,
      applyLoaded,
      shouldSettle,
    })

    expect(shouldSettle).toHaveBeenCalledOnce()
    expect(load).toHaveBeenCalledWith(session.id)
    expect(applyLoaded).toHaveBeenCalledWith(loaded)
    expect(setIsLoadingMessagesMock).toHaveBeenCalledTimes(1)
    expect(setIsLoadingMessagesMock).toHaveBeenCalledWith(true)
  })

  it('activatePersistedSession skips finalization when the load is no longer current', async () => {
    const session = makeSession({ id: 'session-stale' })
    const loaded = { payload: 'loaded' }
    const load = vi.fn(async () => loaded)
    const applyLoaded = vi.fn<(loadedValue: { payload: string }) => void>()
    const isCurrent = vi.fn(() => false)

    const result = await activatePersistedSession(session, {
      projectId: 'project-1',
      load,
      applyLoaded,
      isCurrent,
    })

    expect(result).toBe(loaded)
    expect(load).toHaveBeenCalledWith(session.id)
    expect(applyLoaded).not.toHaveBeenCalled()
    expect(setCurrentSessionMock).toHaveBeenCalledTimes(1)
    expect(setCurrentSessionMock).toHaveBeenCalledWith(session)
    expect(setIsLoadingMessagesMock).toHaveBeenCalledTimes(1)
    expect(setIsLoadingMessagesMock).toHaveBeenCalledWith(true)
    expect(setLastSessionForProjectMock).not.toHaveBeenCalled()
    expect(localStorage.getItem(STORAGE_KEYS.LAST_SESSION)).toBeNull()
  })
})
