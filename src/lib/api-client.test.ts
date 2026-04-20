import { afterEach, describe, expect, it, vi } from 'vitest'
import { apiInvoke, createEventSocket } from './api-client'

describe('apiInvoke', () => {
  afterEach(() => {
    vi.restoreAllMocks()
    window.history.replaceState(null, '', '/')
    window.sessionStorage.clear()
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

  it('snake-cases submit_goal per-run thinking and compaction fields for browser mode', async () => {
    const json = vi.fn().mockResolvedValue({ success: true, turns: 1, sessionId: 'sess-3' })
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      headers: { get: () => 'application/json' },
      json,
    })
    vi.stubGlobal('fetch', fetchMock)

    await apiInvoke('submit_goal', {
      args: {
        goal: 'ship it',
        provider: 'openai',
        model: 'gpt-5.4',
        thinkingLevel: 'high',
        sessionId: 'sess-3',
        runId: 'web-run-1',
        autoCompact: false,
        compactionThreshold: 72,
        compactionProvider: 'anthropic',
        compactionModel: 'claude-sonnet-4.6',
      },
    })

    expect(fetchMock).toHaveBeenCalledWith('/api/agent/submit', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        goal: 'ship it',
        provider: 'openai',
        model: 'gpt-5.4',
        thinking_level: 'high',
        session_id: 'sess-3',
        run_id: 'web-run-1',
        auto_compact: false,
        compaction_threshold: 72,
        compaction_provider: 'anthropic',
        compaction_model: 'claude-sonnet-4.6',
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

  it('promotes ava_token from the URL into bearer auth and clears it from the address bar', async () => {
    window.history.replaceState(null, '', '/?ava_token=query-secret')

    const json = vi.fn().mockResolvedValue({ ok: true })
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      headers: { get: () => 'application/json' },
      json,
    })
    vi.stubGlobal('fetch', fetchMock)

    await apiInvoke('submit_goal', { args: { goal: 'ship it' } })

    expect(fetchMock).toHaveBeenCalledWith('/api/agent/submit', {
      method: 'POST',
      headers: {
        Authorization: 'Bearer query-secret',
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({ goal: 'ship it' }),
    })
    expect(window.location.search).toBe('')
    expect(window.sessionStorage.getItem('ava_web_server_token')).toBe('query-secret')
  })

  it('adds the control token to the WebSocket URL when present', () => {
    window.sessionStorage.setItem('ava_web_server_token', 'ws-secret')

    const webSocketMock = vi.fn()
    vi.stubGlobal('WebSocket', webSocketMock)

    createEventSocket('/ws')

    expect(webSocketMock).toHaveBeenCalledWith('ws://localhost:3000/ws?token=ws-secret')
  })

  it('adds bearer auth to sensitive GET routes when a control token is stored', async () => {
    window.sessionStorage.setItem('ava_web_server_token', 'stored-secret')

    const json = vi.fn().mockResolvedValue({ running: false })
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      headers: { get: () => 'application/json' },
      json,
    })
    vi.stubGlobal('fetch', fetchMock)

    await apiInvoke('get_agent_status', { args: { sessionId: 'sess-1' } })

    expect(fetchMock).toHaveBeenCalledWith('/api/agent/status?session_id=sess-1', {
      method: 'GET',
      headers: { Authorization: 'Bearer stored-secret' },
    })
  })
})
