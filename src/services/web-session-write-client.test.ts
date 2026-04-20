import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { clearAllSessionIdMappings, registerBackendSessionId } from './web-session-identity'
import { writeBrowserSession, writeBrowserSessionCollection } from './web-session-write-client'

describe('web-session-write-client', () => {
  beforeEach(() => {
    clearAllSessionIdMappings()
  })

  afterEach(() => {
    vi.unstubAllGlobals()
    clearAllSessionIdMappings()
  })

  it('builds collection write endpoint and JSON request payload', async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      statusText: 'OK',
    })
    vi.stubGlobal('fetch', fetchMock)

    await writeBrowserSessionCollection({
      action: 'create',
      method: 'POST',
      jsonBody: {
        name: 'Session from test',
      },
    })

    expect(fetchMock).toHaveBeenCalledWith(
      '/api/sessions/create',
      expect.objectContaining({
        method: 'POST',
        headers: expect.objectContaining({ 'Content-Type': 'application/json' }),
        body: JSON.stringify({ name: 'Session from test' }),
      })
    )
  })

  it('resolves frontend session aliases and parses JSON responses', async () => {
    registerBackendSessionId('frontend-1', 'backend-1')

    const expected = { id: 'backend-1', status: 'renamed' }
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      statusText: 'OK',
      json: async () => expected,
    })
    vi.stubGlobal('fetch', fetchMock)

    const result = await writeBrowserSession({
      frontendSessionId: 'frontend-1',
      action: 'rename',
      method: 'PATCH',
      parseJson: true,
      jsonBody: {
        name: 'Renamed',
      },
    })

    expect(fetchMock).toHaveBeenCalledWith(
      '/api/sessions/backend-1/rename',
      expect.objectContaining({
        method: 'PATCH',
        headers: expect.objectContaining({ 'Content-Type': 'application/json' }),
        body: JSON.stringify({ name: 'Renamed' }),
      })
    )
    expect(result).toMatchObject({
      ok: true,
      status: 200,
      statusText: 'OK',
      data: expected,
    })
  })

  it('returns normalized error details and omits JSON request metadata with no payload', async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 422,
      statusText: 'Unprocessable Entity',
      text: async () => 'invalid input',
    })
    vi.stubGlobal('fetch', fetchMock)

    const result = await writeBrowserSession({
      frontendSessionId: 'frontend-3',
      action: 'delete',
    })

    expect(fetchMock).toHaveBeenCalledWith(
      '/api/sessions/frontend-3/delete',
      expect.objectContaining({
        method: 'POST',
        headers: expect.any(Object),
      })
    )
    expect(result).toMatchObject({
      ok: false,
      status: 422,
      statusText: 'Unprocessable Entity',
      errorText: 'invalid input',
    })
  })

  it('returns normalized success with undefined data when parseJson is omitted on empty body response', async () => {
    const jsonMock = vi.fn().mockRejectedValue(new Error('should not parse json'))

    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 204,
      statusText: 'No Content',
      json: jsonMock,
    })
    vi.stubGlobal('fetch', fetchMock)

    const result = await writeBrowserSession({
      frontendSessionId: 'frontend-empty-body',
      action: 'archive',
    })

    expect(fetchMock).toHaveBeenCalledWith(
      '/api/sessions/frontend-empty-body/archive',
      expect.objectContaining({
        method: 'POST',
        headers: expect.any(Object),
      })
    )
    expect(result).toMatchObject({
      ok: true,
      status: 204,
      statusText: 'No Content',
      data: undefined,
    })
    expect(jsonMock).not.toHaveBeenCalled()
  })

  it('normalizes transport-level fetch failures into BrowserSessionWriteResult', async () => {
    const fetchMock = vi.fn().mockRejectedValue(new TypeError('Failed to fetch'))
    vi.stubGlobal('fetch', fetchMock)

    const result = await writeBrowserSessionCollection({
      action: 'create',
      jsonBody: { name: 'Session from test' },
    })

    expect(fetchMock).toHaveBeenCalledWith(
      '/api/sessions/create',
      expect.objectContaining({
        method: 'POST',
        headers: expect.objectContaining({ 'Content-Type': 'application/json' }),
      })
    )
    expect(result).toMatchObject({
      ok: false,
      status: 0,
      statusText: 'Network Error',
      errorText: 'Failed to fetch',
    })
  })

  it('surfaces response.json failures for parseJson=true instead of normalizing as network errors', async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      statusText: 'OK',
      json: vi.fn().mockRejectedValue(new Error('Invalid JSON payload')),
    })
    vi.stubGlobal('fetch', fetchMock)

    await expect(
      writeBrowserSession({
        frontendSessionId: 'frontend-json-failure',
        action: 'duplicate',
        method: 'POST',
        parseJson: true,
      })
    ).rejects.toThrow('Invalid JSON payload')
  })
})
