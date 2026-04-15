import { batch, createSignal } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { Message, Session, SessionWithStats } from '../../types'
import type { AgentToolInfo } from '../../types/rust-ipc'

const listAgentToolsMock = vi.fn()
const [mockSession, setMockSession] = createSignal<Session | undefined>(undefined)
const [mockSessions, setMockSessions] = createSignal<SessionWithStats[]>([])
const [mockMessages, setMockMessages] = createSignal<Message[]>([])

vi.mock('../../services/rust-bridge', () => ({
  rustBackend: {
    listAgentTools: (...args: unknown[]) => listAgentToolsMock(...args),
  },
}))

vi.mock('../../stores/session', () => ({
  useSession: () => ({
    currentSession: mockSession,
    messages: mockMessages,
    sessions: mockSessions,
  }),
}))

import { ToolListDialog } from './ToolListDialog'

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

describe('ToolListDialog', () => {
  let container: HTMLDivElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    listAgentToolsMock.mockReset()
    listAgentToolsMock.mockResolvedValue([])
    setMockSession({
      id: 'session-1',
      name: 'Session 1',
      createdAt: 1,
      updatedAt: 10,
      status: 'active',
    })
    setMockSessions([
      {
        id: 'session-1',
        name: 'Session 1',
        createdAt: 1,
        updatedAt: 10,
        status: 'active',
        messageCount: 1,
        totalTokens: 0,
      },
    ])
    setMockMessages([
      {
        id: 'message-1',
        sessionId: 'session-1',
        role: 'user',
        content: 'use read',
        createdAt: 1,
      },
    ])
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    container.remove()
    setMockSession(undefined)
    setMockSessions([])
    setMockMessages([])
    vi.clearAllMocks()
  })

  function tool(name: string, source = 'builtin'): AgentToolInfo {
    return {
      name,
      description: `${name} description`,
      source,
    }
  }

  it('refetches tool visibility when the active session changes while open', async () => {
    listAgentToolsMock
      .mockResolvedValueOnce([tool('read')])
      .mockResolvedValueOnce([tool('mcp-search', 'mcp')])

    dispose = render(() => <ToolListDialog open onClose={() => undefined} />, container)
    await flush()

    expect(listAgentToolsMock).toHaveBeenCalledTimes(1)
    expect(listAgentToolsMock).toHaveBeenLastCalledWith({
      sessionId: 'session-1',
      goal: 'use read',
      history: [],
      images: [],
    })
    expect(container.textContent).toContain('read')

    batch(() => {
      setMockSession({
        id: 'session-2',
        name: 'Session 2',
        createdAt: 2,
        updatedAt: 20,
        status: 'active',
      })
      setMockSessions([
        {
          id: 'session-1',
          name: 'Session 1',
          createdAt: 1,
          updatedAt: 10,
          status: 'active',
          messageCount: 1,
          totalTokens: 0,
        },
        {
          id: 'session-2',
          name: 'Session 2',
          createdAt: 2,
          updatedAt: 20,
          status: 'active',
          messageCount: 2,
          totalTokens: 0,
        },
      ])
      setMockMessages([
        {
          id: 'message-2',
          sessionId: 'session-2',
          role: 'user',
          content: 'use mcp-search',
          createdAt: 2,
        },
      ])
    })
    await flush()

    expect(listAgentToolsMock).toHaveBeenCalledTimes(2)
    expect(listAgentToolsMock).toHaveBeenLastCalledWith({
      sessionId: 'session-2',
      goal: 'use mcp-search',
      history: [],
      images: [],
    })
    expect(container.textContent).toContain('mcp-search')
  })

  it('refetches tool visibility when the active conversation changes without a session version bump', async () => {
    listAgentToolsMock
      .mockResolvedValueOnce([tool('read')])
      .mockResolvedValueOnce([tool('delegate')])

    dispose = render(() => <ToolListDialog open onClose={() => undefined} />, container)
    await flush()

    expect(listAgentToolsMock).toHaveBeenCalledTimes(1)
    expect(container.textContent).toContain('read')

    batch(() => {
      setMockSession({
        id: 'session-1',
        name: 'Session 1',
        createdAt: 1,
        updatedAt: 10,
        status: 'active',
      })
      setMockSessions([
        {
          id: 'session-1',
          name: 'Session 1',
          createdAt: 1,
          updatedAt: 10,
          status: 'active',
          messageCount: 2,
          totalTokens: 0,
        },
      ])
      setMockMessages([
        {
          id: 'message-1',
          sessionId: 'session-1',
          role: 'user',
          content: 'use read',
          createdAt: 1,
        },
        {
          id: 'message-1b',
          sessionId: 'session-1',
          role: 'assistant',
          content: 'read is available',
          createdAt: 1.5,
          metadata: { agentVisible: false },
        },
        {
          id: 'message-2',
          sessionId: 'session-1',
          role: 'user',
          content: 'delegate this task',
          createdAt: 2,
          images: [{ data: 'abc123', mimeType: 'image/png', name: 'goal.png' }],
        },
      ])
    })
    await flush()

    expect(listAgentToolsMock).toHaveBeenCalledTimes(2)
    expect(listAgentToolsMock).toHaveBeenLastCalledWith({
      sessionId: 'session-1',
      goal: 'delegate this task',
      history: [
        { role: 'user', content: 'use read' },
        { role: 'assistant', content: 'read is available', agentVisible: false },
      ],
      images: [{ data: 'abc123', mediaType: 'image/png' }],
    })
    expect(container.textContent).toContain('delegate')
  })
})
