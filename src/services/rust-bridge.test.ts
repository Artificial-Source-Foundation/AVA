import { beforeEach, describe, expect, it, vi } from 'vitest'

const invokeMock = vi.fn()

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => true,
  invoke: (...args: unknown[]) => invokeMock(...args),
}))

vi.mock('../lib/api-client', () => ({
  apiInvoke: vi.fn(),
}))

import type { ActiveSessionSyncSnapshot, ToolIntrospectionContext } from '../types/rust-ipc'
import { rustAgent, rustBackend } from './rust-bridge'

describe('rustAgent.resolvePlan', () => {
  beforeEach(() => {
    invokeMock.mockReset()
    invokeMock.mockResolvedValue(undefined)
  })

  it('normalizes nested modifiedPlan keys to snake_case for desktop invokes', async () => {
    await rustAgent.resolvePlan(
      'plan-1',
      'modified',
      {
        summary: 'Ship polish',
        estimatedTurns: 2,
        steps: [
          {
            id: 'step-1',
            description: 'Do it',
            files: [],
            action: 'implement',
            dependsOn: ['step-0'],
            approved: true,
          },
        ],
        requestId: 'plan-1',
      },
      'feedback'
    )

    expect(invokeMock).toHaveBeenCalledWith('resolve_plan', {
      args: {
        requestId: 'plan-1',
        response: 'modified',
        modifiedPlan: {
          summary: 'Ship polish',
          estimated_turns: 2,
          steps: [
            {
              id: 'step-1',
              description: 'Do it',
              files: [],
              action: 'implement',
              depends_on: ['step-0'],
              approved: true,
            },
          ],
          request_id: 'plan-1',
        },
        feedback: 'feedback',
      },
    })
  })
})

describe('rustAgent resolveApproval/request handlers', () => {
  beforeEach(() => {
    invokeMock.mockReset()
    invokeMock.mockResolvedValue(undefined)
  })

  it('calls resolve_approval with desktop args shape', async () => {
    await rustAgent.resolveApproval('request-1', true, true)

    expect(invokeMock).toHaveBeenCalledWith('resolve_approval', {
      args: {
        requestId: 'request-1',
        approved: true,
        alwaysAllow: true,
      },
    })
  })

  it('calls resolve_question with desktop args shape', async () => {
    await rustAgent.resolveQuestion('request-2', 'yes')

    expect(invokeMock).toHaveBeenCalledWith('resolve_question', {
      args: {
        requestId: 'request-2',
        answer: 'yes',
      },
    })
  })
})

describe('rustBackend set/list argument shaping', () => {
  beforeEach(() => {
    invokeMock.mockReset()
    invokeMock.mockResolvedValue(undefined)
  })

  it('calls set_active_session without snapshot when omitted', async () => {
    await rustBackend.setActiveSession('session-1')

    expect(invokeMock).toHaveBeenCalledWith('set_active_session', {
      id: 'session-1',
    })
  })

  it('calls set_active_session with snapshot when provided', async () => {
    const snapshot: ActiveSessionSyncSnapshot = {
      title: 'Recovered desktop session',
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
          images: [{ data: 'base64', mediaType: 'image/png' }],
        },
      ],
    }

    await rustBackend.setActiveSession('session-2', snapshot)

    expect(invokeMock).toHaveBeenCalledWith('set_active_session', {
      id: 'session-2',
      snapshot,
    })
  })

  it('passes list_agent_tools without context as a top-level context key', async () => {
    await rustBackend.listAgentTools()

    expect(invokeMock).toHaveBeenCalledWith('list_agent_tools', {
      context: undefined,
    })
  })

  it('passes list_agent_tools with tool introspection context as provided', async () => {
    const context: ToolIntrospectionContext = {
      sessionId: 'session-3',
      goal: 'Ship release',
      history: [
        {
          role: 'user',
          content: 'Need to ship quickly',
          agentVisible: false,
        },
      ],
      images: [{ data: 'imgdata', mediaType: 'image/webp' }],
    }

    await rustBackend.listAgentTools(context)

    expect(invokeMock).toHaveBeenCalledWith('list_agent_tools', {
      context,
    })
  })

  it('threads run/session correlation through web control-plane helpers', async () => {
    await rustBackend.getAgentStatus({ runId: 'web-run-1' })
    await rustBackend.cancelAgent({ runId: 'web-run-1', sessionId: 'session-1' })
    await rustBackend.steerAgent('nudge', { runId: 'web-run-1' })
    await rustBackend.getMessageQueue({ sessionId: 'session-1' })
    await rustBackend.clearMessageQueue('all', { runId: 'web-run-1', sessionId: 'session-1' })
    await rustBackend.undoLastEdit({ runId: 'web-run-1', sessionId: 'session-1' })

    expect(invokeMock).toHaveBeenNthCalledWith(1, 'get_agent_status', {
      args: { runId: 'web-run-1' },
    })
    expect(invokeMock).toHaveBeenNthCalledWith(2, 'cancel_agent', {
      args: { runId: 'web-run-1', sessionId: 'session-1' },
    })
    expect(invokeMock).toHaveBeenNthCalledWith(3, 'steer_agent', {
      args: { message: 'nudge', runId: 'web-run-1' },
    })
    expect(invokeMock).toHaveBeenNthCalledWith(4, 'get_message_queue', {
      args: { sessionId: 'session-1' },
    })
    expect(invokeMock).toHaveBeenNthCalledWith(5, 'clear_message_queue', {
      args: { target: 'all', runId: 'web-run-1', sessionId: 'session-1' },
    })
    expect(invokeMock).toHaveBeenNthCalledWith(6, 'undo_last_edit', {
      args: { runId: 'web-run-1', sessionId: 'session-1' },
    })
  })
})
