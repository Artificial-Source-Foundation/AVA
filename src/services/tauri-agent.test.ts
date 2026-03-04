import { describe, expect, it, vi } from 'vitest'

const mockInvoke = vi.fn()
const mockListen = vi.fn()

vi.mock('@tauri-apps/api/core', () => ({
  invoke: (...args: unknown[]) => mockInvoke(...args),
}))

vi.mock('@tauri-apps/api/event', () => ({
  listen: (...args: unknown[]) => mockListen(...args),
}))

const { agentRun, agentStream, executeTool, listTools, listenToAgentEvents } = await import(
  './tauri-agent.js'
)

describe('tauri-agent bridge', () => {
  it('invokes execute_tool with payload', async () => {
    mockInvoke.mockResolvedValueOnce({ content: 'ok', is_error: false })

    const result = await executeTool('read_file', { path: 'README.md' })

    expect(mockInvoke).toHaveBeenCalledWith('execute_tool', {
      tool: 'read_file',
      args: { path: 'README.md' },
    })
    expect(result).toEqual({ content: 'ok', is_error: false })
  })

  it('invokes agent_run command', async () => {
    mockInvoke.mockResolvedValueOnce({
      id: 'mock-session-1',
      messages: [],
      completed: true,
    })

    const session = await agentRun('Test goal')

    expect(mockInvoke).toHaveBeenCalledWith('agent_run', { goal: 'Test goal' })
    expect(session.completed).toBe(true)
  })

  it('invokes agent_stream command', async () => {
    mockInvoke.mockResolvedValueOnce(undefined)

    await agentStream('Stream this goal')

    expect(mockInvoke).toHaveBeenCalledWith('agent_stream', { goal: 'Stream this goal' })
  })

  it('invokes list_tools command', async () => {
    mockInvoke.mockResolvedValueOnce([{ name: 'read_file', description: 'Read a file' }])

    const tools = await listTools()

    expect(mockInvoke).toHaveBeenCalledWith('list_tools')
    expect(tools).toHaveLength(1)
  })

  it('subscribes to agent-event stream', async () => {
    const unlisten = vi.fn()
    mockListen.mockImplementationOnce(async (_eventName, callback) => {
      callback({ payload: { type: 'token', content: 'Hello' } })
      return unlisten
    })

    const received: string[] = []
    const stopListening = await listenToAgentEvents((event) => {
      if (event.type === 'token') {
        received.push(event.content)
      }
    })

    expect(mockListen).toHaveBeenCalledWith('agent-event', expect.any(Function))
    expect(received).toEqual(['Hello'])
    expect(stopListening).toBe(unlisten)
  })
})
