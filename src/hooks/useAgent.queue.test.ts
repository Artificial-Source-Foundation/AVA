import { createRoot } from 'solid-js'
import { beforeEach, describe, expect, it, vi } from 'vitest'

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => false,
}))

const h = vi.hoisted(() => ({
  isRunning: false,
}))

vi.mock('./use-rust-agent', async () => {
  const { createSignal } = await vi.importActual<typeof import('solid-js')>('solid-js')

  return {
    useRustAgent: () => {
      const [streamingContent] = createSignal('')
      const [thinkingContent] = createSignal('')
      const [thinkingSegments] = createSignal([])
      const [activeToolCalls] = createSignal([])
      const [error] = createSignal<string | null>(null)
      const [currentRunId] = createSignal<string | null>(null)
      const [tokenUsage] = createSignal({ input: 0, output: 0, cost: 0 })
      const [events] = createSignal<unknown[]>([])

      return {
        isRunning: () => h.isRunning,
        streamingContent,
        thinkingContent,
        thinkingSegments,
        activeToolCalls,
        error,
        lastResult: null,
        currentRunId,
        tokenUsage,
        events,
        run: vi.fn(async () => null),
        editAndResendRun: vi.fn(async () => null),
        retryRun: vi.fn(async () => null),
        regenerateRun: vi.fn(async () => null),
        cancel: vi.fn(async () => {}),
        clearError: vi.fn(),
        endRun: vi.fn(),
        steer: vi.fn(async () => {}),
        followUp: vi.fn(async () => {}),
        postComplete: vi.fn(async () => {}),
        markToolApproval: vi.fn(),
        stop: vi.fn(async () => {}),
        isStreaming: () => h.isRunning,
        currentTokens: streamingContent,
        session: null,
      }
    },
  }
})

vi.mock('../stores/settings', () => ({
  useSettings: () => ({
    settings: () => ({
      generation: {
        customInstructions: '',
        reasoningEffort: 'off',
        compactionModel: '',
        autoCompact: false,
        compactionThreshold: 80,
      },
      behavior: { sessionAutoTitle: false },
      agentLimits: { agentMaxTurns: 20, agentMaxTimeMinutes: 10, autoFixLint: false },
      notifications: {},
      permissionMode: 'normal',
    }),
    isToolAutoApproved: () => false,
  }),
}))

let mockCurrentSessionId = 'session-1'
const mockAddMessage = vi.fn()

vi.mock('../stores/session', () => ({
  useSession: () => ({
    currentSession: () => ({ id: mockCurrentSessionId, name: `Session ${mockCurrentSessionId}` }),
    messages: () => [],
    selectedModel: () => 'gpt-4',
    selectedProvider: () => 'openai',
    createNewSession: vi.fn(async () => ({ id: 'session-new', name: 'New Session' })),
    addMessage: mockAddMessage,
    addMessageToSession: vi.fn(),
    updateMessage: vi.fn(),
    updateMessageInSession: vi.fn(),
    deleteMessage: vi.fn(async () => {}),
    deleteMessageInSession: vi.fn(async () => {}),
    deleteMessagesAfter: vi.fn(),
    renameSession: vi.fn(async () => {}),
    stopEditing: vi.fn(),
    setMessageError: vi.fn(),
    setMessages: vi.fn(),
  }),
}))

vi.mock('../services/core-bridge', () => ({
  ensureActiveSessionSynced: vi.fn(async () => ({
    sessionId: 'session-1',
    exists: true,
    messageCount: 0,
  })),
  getCoreBudget: () => null,
  markActiveSessionSynced: vi.fn(),
}))

vi.mock('../services/context-compaction', () => ({
  applyCompactionResult: vi.fn(),
  decodeCompactionModel: () => null,
}))

vi.mock('../services/db-web-fallback', () => ({
  registerBackendSessionId: vi.fn(),
}))

vi.mock('../services/rust-bridge', () => ({
  rustAgent: {
    cancel: vi.fn(async () => {}),
    resolveApproval: vi.fn(async () => {}),
    resolveQuestion: vi.fn(async () => {}),
    resolvePlan: vi.fn(async () => {}),
  },
  rustBackend: {
    undoLastEdit: vi.fn(async () => ({ success: true, message: 'ok' })),
  },
}))

import { _resetAgentSingleton, useAgent } from './useAgent'

async function flushEffects(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

describe('useAgent queue session handling', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    h.isRunning = false
    mockCurrentSessionId = 'session-1'
    _resetAgentSingleton()
  })

  it('force-clears only the session being left during a session switch', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))

    h.isRunning = true

    await ctx.agent.run('session-1 local queued')
    ctx.agent.followUp('session-1 backend follow-up')
    await flushEffects()

    mockCurrentSessionId = 'session-2'

    await ctx.agent.run('session-2 local queued')
    ctx.agent.postComplete('session-2 backend post-complete')
    await flushEffects()

    expect(ctx.agent.messageQueue().map((item) => item.content)).toEqual([
      'session-2 local queued',
      'session-2 backend post-complete',
    ])

    ctx.agent.clearQueue(true, 'session-1')

    expect(ctx.agent.messageQueue().map((item) => item.content)).toEqual([
      'session-2 local queued',
      'session-2 backend post-complete',
    ])

    mockCurrentSessionId = 'session-1'
    expect(ctx.agent.messageQueue()).toHaveLength(0)

    mockCurrentSessionId = 'session-2'
    expect(ctx.agent.messageQueue().map((item) => item.content)).toEqual([
      'session-2 local queued',
      'session-2 backend post-complete',
    ])

    ctx.dispose()
  })

  it('preserves backend-managed rows for the current session on user clear', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))

    h.isRunning = true

    await ctx.agent.run('session-1 local queued')
    ctx.agent.followUp('session-1 backend follow-up')
    await flushEffects()

    ctx.agent.clearQueue(false, 'session-1')

    expect(ctx.agent.messageQueue().map((item) => item.content)).toEqual([
      'session-1 backend follow-up',
    ])

    ctx.dispose()
  })

  it('maps visible queue edits and removals back to the current-session rows', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))

    h.isRunning = true

    await ctx.agent.run('session-1 first visible row')

    mockCurrentSessionId = 'session-2'
    await ctx.agent.run('session-2 hidden row')

    mockCurrentSessionId = 'session-1'
    await ctx.agent.run('session-1 second visible row')

    expect(ctx.agent.messageQueue().map((item) => item.content)).toEqual([
      'session-1 first visible row',
      'session-1 second visible row',
    ])

    ctx.agent.editInQueue(1, 'session-1 second visible row (edited)')

    expect(ctx.agent.messageQueue().map((item) => item.content)).toEqual([
      'session-1 first visible row',
      'session-1 second visible row (edited)',
    ])

    mockCurrentSessionId = 'session-2'
    expect(ctx.agent.messageQueue().map((item) => item.content)).toEqual(['session-2 hidden row'])

    mockCurrentSessionId = 'session-1'
    ctx.agent.removeFromQueue(1)

    expect(ctx.agent.messageQueue().map((item) => item.content)).toEqual([
      'session-1 first visible row',
    ])

    mockCurrentSessionId = 'session-2'
    expect(ctx.agent.messageQueue().map((item) => item.content)).toEqual(['session-2 hidden row'])

    ctx.dispose()
  })

  it('reorders only visible current-session rows when hidden rows exist between them', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))

    h.isRunning = true

    await ctx.agent.run('session-1 first visible row')

    mockCurrentSessionId = 'session-2'
    await ctx.agent.run('session-2 hidden row')

    mockCurrentSessionId = 'session-1'
    await ctx.agent.run('session-1 second visible row')

    ctx.agent.reorderInQueue(1, 0)

    expect(ctx.agent.messageQueue().map((item) => item.content)).toEqual([
      'session-1 second visible row',
      'session-1 first visible row',
    ])

    mockCurrentSessionId = 'session-2'
    expect(ctx.agent.messageQueue().map((item) => item.content)).toEqual(['session-2 hidden row'])

    ctx.dispose()
  })
})
