import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { clearAllSessionIdMappings, registerBackendSessionId } from './web-session-identity'

vi.mock('./web-session-write-client', () => ({
  writeBrowserSession: vi.fn(),
}))

vi.mock('./logger', () => ({
  logWarn: vi.fn(),
}))

import { logWarn } from './logger'
import {
  branchSessionAtMessageInWebMode,
  cloneSessionInWebMode,
  UnsupportedWebSessionMutationError,
  WebSessionMutationError,
} from './web-session-mutations'
import { writeBrowserSession } from './web-session-write-client'

interface DuplicateSessionResponse {
  id: string
  title: string
  message_count: number
  parent_session_id?: string | null
  last_preview?: string | null
  created_at: string
  updated_at: string
}

const mockedWriteBrowserSession = vi.mocked(writeBrowserSession)
const mockedLogWarn = vi.mocked(logWarn)

describe('web-session-mutations', () => {
  beforeEach(() => {
    clearAllSessionIdMappings()
    mockedWriteBrowserSession.mockReset()
    mockedLogWarn.mockReset()
  })

  afterEach(() => {
    vi.restoreAllMocks()
    clearAllSessionIdMappings()
  })

  it('maps duplicate response payload into frontend session and forwards duplicate kind', async () => {
    mockedWriteBrowserSession.mockResolvedValueOnce({
      ok: true,
      status: 200,
      statusText: 'OK',
      data: {
        id: 'backend-session-id',
        title: 'Copied session',
        message_count: 4,
        parent_session_id: null,
        last_preview: 'Hello world',
        created_at: '2026-01-01T00:00:00.000Z',
        updated_at: '2026-01-02T00:00:00.000Z',
      } satisfies DuplicateSessionResponse,
    })

    const result = await cloneSessionInWebMode({
      kind: 'duplicate',
      sourceSessionId: 'source-session',
      sourceSessionName: 'Session Alpha',
      projectId: 'project-1',
    })

    expect(mockedWriteBrowserSession).toHaveBeenCalledWith(
      expect.objectContaining({
        frontendSessionId: 'source-session',
        action: 'duplicate',
        parseJson: true,
        jsonBody: expect.objectContaining({
          kind: 'duplicate',
          name: 'Session Alpha (copy)',
          id: expect.any(String),
        }),
      })
    )

    expect(result).toEqual(
      expect.objectContaining({
        session: {
          id: 'backend-session-id',
          name: 'Copied session',
          projectId: 'project-1',
          parentSessionId: undefined,
          createdAt: Date.parse('2026-01-01T00:00:00.000Z'),
          updatedAt: Date.parse('2026-01-02T00:00:00.000Z'),
          status: 'active',
          metadata: {},
        },
        stats: {
          messageCount: 4,
          totalTokens: 0,
          lastPreview: 'Hello world',
        },
      })
    )
  })

  it('uses fork naming, explicit-name overrides, and canonicalizes parent linkage', async () => {
    registerBackendSessionId('frontend-parent', 'backend-parent')

    mockedWriteBrowserSession
      .mockResolvedValueOnce({
        ok: true,
        status: 200,
        statusText: 'OK',
        data: {
          id: 'backend-fork-session',
          title: 'Forked session',
          message_count: 2,
          parent_session_id: 'backend-parent',
          last_preview: 'Preview',
          created_at: '2026-02-01T00:00:00.000Z',
          updated_at: '2026-02-02T00:00:00.000Z',
        } satisfies DuplicateSessionResponse,
      })
      .mockResolvedValueOnce({
        ok: true,
        status: 200,
        statusText: 'OK',
        data: {
          id: 'backend-fork-session-explicit',
          title: 'Forked session explicit',
          message_count: 1,
          parent_session_id: 'backend-parent',
          created_at: '2026-02-03T00:00:00.000Z',
          updated_at: '2026-02-04T00:00:00.000Z',
        } satisfies DuplicateSessionResponse,
      })

    const defaultForkResult = await cloneSessionInWebMode({
      kind: 'fork',
      sourceSessionId: 'source-session',
      sourceSessionName: 'Session Bravo',
      projectId: 'project-1',
    })

    const explicitForkResult = await cloneSessionInWebMode({
      kind: 'fork',
      sourceSessionId: 'source-session',
      sourceSessionName: 'Session Bravo',
      projectId: 'project-1',
      name: 'Explicit Fork',
    })

    expect(mockedWriteBrowserSession).toHaveBeenNthCalledWith(
      1,
      expect.objectContaining({
        jsonBody: expect.objectContaining({
          kind: 'fork',
          name: 'Session Bravo (fork)',
        }),
      })
    )

    expect(mockedWriteBrowserSession).toHaveBeenNthCalledWith(
      2,
      expect.objectContaining({
        jsonBody: expect.objectContaining({
          kind: 'fork',
          name: 'Explicit Fork',
        }),
      })
    )

    expect(defaultForkResult.session.parentSessionId).toBe('frontend-parent')
    expect(explicitForkResult.session.parentSessionId).toBe('frontend-parent')
    expect(defaultForkResult.session.name).toBe('Forked session')
    expect(explicitForkResult.session.name).toBe('Forked session explicit')
  })

  it('throws WebSessionMutationError when write result is not ok', async () => {
    mockedWriteBrowserSession.mockResolvedValueOnce({
      ok: false,
      status: 422,
      statusText: 'Unprocessable Entity',
      errorText: 'validation failed',
    })

    await expect(
      cloneSessionInWebMode({
        kind: 'duplicate',
        sourceSessionId: 'source-session',
        sourceSessionName: 'Session Alpha',
      })
    ).rejects.toMatchObject({
      name: WebSessionMutationError.name,
      message: 'Web duplicate failed (422): validation failed',
    })
  })

  it('converts normalized network-failure write results into WebSessionMutationError', async () => {
    mockedWriteBrowserSession.mockResolvedValueOnce({
      ok: false,
      status: 0,
      statusText: 'Network Error',
      errorText: 'Failed to fetch',
    })

    await expect(
      cloneSessionInWebMode({
        kind: 'duplicate',
        sourceSessionId: 'source-session',
        sourceSessionName: 'Session Alpha',
      })
    ).rejects.toMatchObject({
      name: WebSessionMutationError.name,
      message: 'Web duplicate failed (0): Failed to fetch',
    })
  })

  it('propagates parseJson response decoding failures from writeBrowserSession', async () => {
    mockedWriteBrowserSession.mockRejectedValueOnce(new Error('Invalid JSON payload'))

    await expect(
      cloneSessionInWebMode({
        kind: 'duplicate',
        sourceSessionId: 'source-session',
        sourceSessionName: 'Session Alpha',
      })
    ).rejects.toMatchObject({
      name: WebSessionMutationError.name,
      message: 'Web duplicate failed: Invalid JSON payload',
    })

    expect(mockedWriteBrowserSession).toHaveBeenCalledWith(
      expect.objectContaining({
        parseJson: true,
      })
    )
  })

  it('throws WebSessionMutationError when payload is truthy but malformed', async () => {
    mockedWriteBrowserSession.mockResolvedValueOnce({
      ok: true,
      status: 200,
      statusText: 'OK',
      data: {
        id: 'backend-session-id',
      } as unknown as DuplicateSessionResponse,
    })

    await expect(
      cloneSessionInWebMode({
        kind: 'duplicate',
        sourceSessionId: 'source-session',
        sourceSessionName: 'Session Alpha',
      })
    ).rejects.toMatchObject({
      name: WebSessionMutationError.name,
      message: 'Web duplicate failed: malformed response payload',
    })
  })

  it('throws WebSessionMutationError on OK result with missing response data', async () => {
    mockedWriteBrowserSession.mockResolvedValueOnce({
      ok: true,
      status: 204,
      statusText: 'No Content',
    })

    await expect(
      cloneSessionInWebMode({
        kind: 'fork',
        sourceSessionId: 'source-session',
        sourceSessionName: 'Session Alpha',
      })
    ).rejects.toMatchObject({
      name: WebSessionMutationError.name,
      message: 'Web fork failed: empty response payload',
    })
  })

  it('throws UnsupportedWebSessionMutationError for branch-at-message operations in web mode', async () => {
    await expect(
      branchSessionAtMessageInWebMode({
        sessionId: 'session-1',
        messageId: 'message-1',
      })
    ).rejects.toMatchObject({
      name: UnsupportedWebSessionMutationError.name,
      message: 'Branching from a specific message is not supported in web mode yet.',
    })

    expect(mockedLogWarn).toHaveBeenCalledTimes(1)
    expect(mockedLogWarn).toHaveBeenCalledWith(
      'Session',
      'Web branchAtMessage requires a backend-backed endpoint and is disabled',
      {
        sessionId: 'session-1',
        messageId: 'message-1',
      }
    )
  })
})
