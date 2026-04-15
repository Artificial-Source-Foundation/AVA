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

  it('posts nested tool introspection context for list_agent_tools', async () => {
    const json = vi.fn().mockResolvedValue([])
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      headers: { get: () => 'application/json' },
      json,
    })
    vi.stubGlobal('fetch', fetchMock)

    await apiInvoke('list_agent_tools', {
      context: {
        sessionId: 'sess-2',
        history: [{ role: 'user', content: 'show tools', agentVisible: false }],
      },
    })

    expect(fetchMock).toHaveBeenCalledWith('/api/tools/agent', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        context: {
          session_id: 'sess-2',
          history: [{ role: 'user', content: 'show tools', agent_visible: false }],
        },
      }),
    })
  })

  it('maps resolve_plan to the shared web control-plane route', async () => {
    const json = vi.fn().mockResolvedValue({ ok: true })
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      headers: { get: () => 'application/json' },
      json,
    })
    vi.stubGlobal('fetch', fetchMock)

    await apiInvoke('resolve_plan', {
      args: {
        requestId: 'plan-1',
        response: 'approved',
        feedback: 'looks good',
      },
    })

    expect(fetchMock).toHaveBeenCalledWith('/api/agent/resolve-plan', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ request_id: 'plan-1', response: 'approved', feedback: 'looks good' }),
    })
  })
})
