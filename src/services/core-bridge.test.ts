import { beforeEach, describe, expect, it, vi } from 'vitest'

let isTauriRuntime = false
const setActiveSessionMock = vi.fn()

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => isTauriRuntime,
}))

vi.mock('./rust-bridge', () => ({
  rustBackend: {
    setActiveSession: (...args: unknown[]) => setActiveSessionMock(...args),
  },
}))

import {
  type BackendSessionSyncError,
  ensureActiveSessionSynced,
  initCoreBridge,
  notifySessionOpened,
} from './core-bridge'

describe('notifySessionOpened', () => {
  beforeEach(async () => {
    isTauriRuntime = false
    ;(await initCoreBridge())()
    vi.clearAllMocks()
  })

  it('syncs the active session into the Tauri backend bridge', async () => {
    isTauriRuntime = true
    setActiveSessionMock.mockResolvedValue({
      sessionId: 'session-123',
      exists: true,
      messageCount: 4,
    })

    await expect(notifySessionOpened('session-123', '/workspace')).resolves.toEqual({
      sessionId: 'session-123',
      exists: true,
      messageCount: 4,
    })

    expect(setActiveSessionMock).toHaveBeenCalledWith('session-123', '/workspace')
  })

  it('forwards restored frontend session snapshots when opening a desktop session', async () => {
    isTauriRuntime = true
    setActiveSessionMock.mockResolvedValue({
      sessionId: 'session-123',
      exists: true,
      messageCount: 2,
    })

    await expect(
      notifySessionOpened('session-123', '/workspace', {
        title: 'Recovered session',
        messages: [
          {
            id: '00000000-0000-0000-0000-000000000001',
            role: 'user',
            content: 'hello',
            createdAt: 1_762_806_000_000,
          },
          {
            id: '00000000-0000-0000-0000-000000000002',
            role: 'assistant',
            content: 'hi',
            createdAt: 1_762_806_001_000,
          },
        ],
      })
    ).resolves.toEqual({
      sessionId: 'session-123',
      exists: true,
      messageCount: 2,
    })

    expect(setActiveSessionMock).toHaveBeenCalledWith('session-123', '/workspace', {
      title: 'Recovered session',
      messages: [
        {
          id: '00000000-0000-0000-0000-000000000001',
          role: 'user',
          content: 'hello',
          createdAt: 1_762_806_000_000,
        },
        {
          id: '00000000-0000-0000-0000-000000000002',
          role: 'assistant',
          content: 'hi',
          createdAt: 1_762_806_001_000,
        },
      ],
    })
  })

  it('is a no-op outside Tauri', async () => {
    await expect(notifySessionOpened('session-123', '/workspace')).resolves.toEqual({
      sessionId: 'session-123',
      exists: true,
      messageCount: 0,
    })
    expect(setActiveSessionMock).not.toHaveBeenCalled()
  })

  it('reuses the in-flight sync before retry-like flows continue', async () => {
    isTauriRuntime = true
    let resolveSync:
      | ((value: { sessionId: string; exists: boolean; messageCount: number }) => void)
      | null = null
    setActiveSessionMock.mockImplementation(
      () =>
        new Promise((resolve) => {
          resolveSync = resolve
        })
    )

    const openingPromise = notifySessionOpened('session-123', '/workspace')
    const ensuredPromise = ensureActiveSessionSynced('session-123')

    expect(setActiveSessionMock).toHaveBeenCalledTimes(1)
    expect(setActiveSessionMock).toHaveBeenCalledWith('session-123', '/workspace')
    if (!resolveSync) {
      throw new Error('Active session sync promise was not captured')
    }
    const resolveCaptured: (value: {
      sessionId: string
      exists: boolean
      messageCount: number
    }) => void = resolveSync
    resolveCaptured({ sessionId: 'session-123', exists: true, messageCount: 2 })

    await expect(openingPromise).resolves.toEqual({
      sessionId: 'session-123',
      exists: true,
      messageCount: 2,
    })
    await expect(ensuredPromise).resolves.toEqual({
      sessionId: 'session-123',
      exists: true,
      messageCount: 2,
    })
  })

  it('re-applies the latest session when an older sync finishes last', async () => {
    isTauriRuntime = true
    const resolvers: Array<
      (value: { sessionId: string; exists: boolean; messageCount: number }) => void
    > = []
    setActiveSessionMock.mockImplementation(
      () =>
        new Promise((resolve) => {
          resolvers.push(resolve)
        })
    )

    const firstPromise = notifySessionOpened('session-a', '/workspace')
    const secondPromise = notifySessionOpened('session-b', '/workspace')

    expect(setActiveSessionMock).toHaveBeenNthCalledWith(1, 'session-a', '/workspace')
    expect(setActiveSessionMock).toHaveBeenNthCalledWith(2, 'session-b', '/workspace')

    resolvers[1]?.({ sessionId: 'session-b', exists: true, messageCount: 3 })
    await expect(secondPromise).resolves.toEqual({
      sessionId: 'session-b',
      exists: true,
      messageCount: 3,
    })

    resolvers[0]?.({ sessionId: 'session-a', exists: true, messageCount: 1 })
    await expect(firstPromise).resolves.toEqual({
      sessionId: 'session-a',
      exists: true,
      messageCount: 1,
    })

    await Promise.resolve()

    expect(setActiveSessionMock).toHaveBeenNthCalledWith(3, 'session-b', '/workspace')

    resolvers[2]?.({ sessionId: 'session-b', exists: true, messageCount: 3 })
    await expect(ensureActiveSessionSynced('session-b')).resolves.toEqual({
      sessionId: 'session-b',
      exists: true,
      messageCount: 3,
    })
  })

  it('re-applies the latest session when an older sync fails last', async () => {
    isTauriRuntime = true
    const settlements: Array<{
      resolve: (value: { sessionId: string; exists: boolean; messageCount: number }) => void
      reject: (error?: unknown) => void
    }> = []
    setActiveSessionMock.mockImplementation(
      () =>
        new Promise((resolve, reject) => {
          settlements.push({ resolve, reject })
        })
    )

    const firstPromise = notifySessionOpened('session-a', '/workspace')
    const secondPromise = notifySessionOpened('session-b', '/workspace')

    expect(setActiveSessionMock).toHaveBeenNthCalledWith(1, 'session-a', '/workspace')
    expect(setActiveSessionMock).toHaveBeenNthCalledWith(2, 'session-b', '/workspace')

    settlements[1]?.resolve({ sessionId: 'session-b', exists: true, messageCount: 5 })
    await expect(secondPromise).resolves.toEqual({
      sessionId: 'session-b',
      exists: true,
      messageCount: 5,
    })

    settlements[0]?.reject(new Error('stale sync failed late'))
    await expect(firstPromise).resolves.toEqual({
      sessionId: 'session-a',
      exists: false,
      messageCount: 0,
    })

    await Promise.resolve()

    expect(setActiveSessionMock).toHaveBeenNthCalledWith(3, 'session-b', '/workspace')

    settlements[2]?.resolve({ sessionId: 'session-b', exists: true, messageCount: 5 })
    await expect(ensureActiveSessionSynced('session-b')).resolves.toEqual({
      sessionId: 'session-b',
      exists: true,
      messageCount: 5,
    })
  })

  it('re-targets a stale repair when a newer session opened afterward', async () => {
    isTauriRuntime = true

    type SessionSyncCall = {
      sessionId: string
      resolve: (value: { sessionId: string; exists: boolean; messageCount: number }) => void
      reject: (error?: unknown) => void
    }
    const syncCalls: SessionSyncCall[] = []

    setActiveSessionMock.mockImplementation(
      (sessionId: string) =>
        new Promise((resolve, reject) => {
          syncCalls.push({
            sessionId,
            resolve,
            reject,
          })
        })
    )

    const firstPromise = notifySessionOpened('session-a', '/workspace')
    const secondPromise = notifySessionOpened('session-b', '/workspace')
    const thirdPromise = notifySessionOpened('session-c', '/workspace')

    expect(setActiveSessionMock).toHaveBeenNthCalledWith(1, 'session-a', '/workspace')
    expect(setActiveSessionMock).toHaveBeenNthCalledWith(2, 'session-b', '/workspace')
    expect(setActiveSessionMock).toHaveBeenNthCalledWith(3, 'session-c', '/workspace')

    const cCallsBeforeRepair = syncCalls.filter((call) => call.sessionId === 'session-c')
    expect(cCallsBeforeRepair).toHaveLength(1)

    syncCalls[1]?.resolve({
      sessionId: 'session-b',
      exists: true,
      messageCount: 2,
    })

    await secondPromise

    for (
      let i = 0;
      i < 20 && syncCalls.filter((call) => call.sessionId === 'session-c').length < 2;
      i++
    ) {
      await Promise.resolve()
    }

    expect(syncCalls.filter((call) => call.sessionId === 'session-c')).toHaveLength(
      cCallsBeforeRepair.length + 1
    )

    syncCalls[0]?.resolve({
      sessionId: 'session-a',
      exists: true,
      messageCount: 1,
    })

    syncCalls[2]?.resolve({
      sessionId: 'session-c',
      exists: true,
      messageCount: 4,
    })

    const repairedSessionCCall = syncCalls.filter((call) => call.sessionId === 'session-c')[1]
    repairedSessionCCall?.resolve({
      sessionId: 'session-c',
      exists: true,
      messageCount: 5,
    })

    await expect(firstPromise).resolves.toEqual({
      sessionId: 'session-a',
      exists: true,
      messageCount: 1,
    })
    await expect(secondPromise).resolves.toEqual({
      sessionId: 'session-b',
      exists: true,
      messageCount: 2,
    })
    await expect(thirdPromise).resolves.toEqual({
      sessionId: 'session-c',
      exists: true,
      messageCount: 4,
    })
  })

  it('surfaces missing backend sessions to retry-like flows', async () => {
    isTauriRuntime = true
    setActiveSessionMock.mockResolvedValue({
      sessionId: 'missing-session',
      exists: false,
      messageCount: 0,
    })

    await expect(notifySessionOpened('missing-session', '/workspace')).resolves.toEqual({
      sessionId: 'missing-session',
      exists: false,
      messageCount: 0,
    })

    await expect(ensureActiveSessionSynced('missing-session')).rejects.toMatchObject({
      name: 'BackendSessionSyncError',
      code: 'missing-session',
      sessionId: 'missing-session',
    } satisfies Partial<BackendSessionSyncError>)
  })

  it('treats sync transport failures as best-effort during session open but keeps preflight strict', async () => {
    isTauriRuntime = true
    setActiveSessionMock
      .mockRejectedValueOnce(new Error('desktop backend unavailable'))
      .mockRejectedValueOnce(new Error('desktop backend unavailable'))

    await expect(notifySessionOpened('session-123', '/workspace')).resolves.toEqual({
      sessionId: 'session-123',
      exists: false,
      messageCount: 0,
    })

    await expect(ensureActiveSessionSynced('session-123')).rejects.toMatchObject({
      name: 'BackendSessionSyncError',
      code: 'sync-failed',
      sessionId: 'session-123',
    } satisfies Partial<BackendSessionSyncError>)
  })

  it('retries same-session preflight after a failed sync without reopening the session', async () => {
    isTauriRuntime = true
    setActiveSessionMock
      .mockRejectedValueOnce(new Error('desktop backend unavailable'))
      .mockResolvedValueOnce({
        sessionId: 'session-123',
        exists: true,
        messageCount: 6,
      })

    await expect(notifySessionOpened('session-123', '/workspace')).resolves.toEqual({
      sessionId: 'session-123',
      exists: false,
      messageCount: 0,
    })

    await expect(ensureActiveSessionSynced('session-123')).resolves.toEqual({
      sessionId: 'session-123',
      exists: true,
      messageCount: 6,
    })

    expect(setActiveSessionMock).toHaveBeenCalledTimes(2)
    expect(setActiveSessionMock).toHaveBeenNthCalledWith(1, 'session-123', '/workspace')
    expect(setActiveSessionMock).toHaveBeenNthCalledWith(2, 'session-123', '/workspace')
  })

  it('retries same-session preflight after a missing-session result without reopening the session', async () => {
    isTauriRuntime = true
    setActiveSessionMock
      .mockResolvedValueOnce({
        sessionId: 'missing-session',
        exists: false,
        messageCount: 0,
      })
      .mockResolvedValueOnce({
        sessionId: 'missing-session',
        exists: true,
        messageCount: 11,
      })

    await expect(notifySessionOpened('missing-session', '/workspace')).resolves.toEqual({
      sessionId: 'missing-session',
      exists: false,
      messageCount: 0,
    })

    await expect(ensureActiveSessionSynced('missing-session')).resolves.toEqual({
      sessionId: 'missing-session',
      exists: true,
      messageCount: 11,
    })

    expect(setActiveSessionMock).toHaveBeenCalledTimes(2)
    expect(setActiveSessionMock).toHaveBeenNthCalledWith(1, 'missing-session', '/workspace')
    expect(setActiveSessionMock).toHaveBeenNthCalledWith(2, 'missing-session', '/workspace')
  })
})
