import { afterEach, describe, expect, it, vi } from 'vitest'
import { apiInvoke } from './api-client'

describe('apiInvoke', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('unwraps Tauri-style GET args and snake-cases query params', async () => {
    const json = vi.fn().mockResolvedValue([{ id: 'm1' }])
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      headers: { get: () => 'application/json' },
      json,
    })
    vi.stubGlobal('fetch', fetchMock)

    await apiInvoke('get_session_messages', {
      args: { id: 'sess-1', includeMetadata: true, maxTurns: 2 },
    })

    expect(fetchMock).toHaveBeenCalledWith(
      '/api/sessions/sess-1/messages?include_metadata=true&max_turns=2',
      { method: 'GET', headers: {} }
    )
  })
})
