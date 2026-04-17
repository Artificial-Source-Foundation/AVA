import { createRoot } from 'solid-js'
import { beforeEach, describe, expect, it, vi } from 'vitest'

import type { AgentEvent } from '../types/rust-ipc'

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => false,
}))

const h = vi.hoisted(() => {
  let appendEvent: ((event: AgentEvent) => void) | null = null
  let resetEvents: (() => void) | null = null
  let setCurrentRunId: ((runId: string | null) => void) | null = null
  const rehydrateStatus = vi.fn(async () => {})

  return {
    bind(
      nextAppendEvent: (event: AgentEvent) => void,
      nextResetEvents: () => void,
      nextSetCurrentRunId: (runId: string | null) => void
    ): void {
      appendEvent = nextAppendEvent
      resetEvents = nextResetEvents
      setCurrentRunId = nextSetCurrentRunId
    },
    emit(event: AgentEvent): void {
      appendEvent?.(event)
    },
    setRunId(runId: string | null): void {
      setCurrentRunId?.(runId)
    },
    reset(): void {
      resetEvents?.()
      setCurrentRunId?.(null)
      rehydrateStatus.mockClear()
    },
    rehydrateStatus,
  }
})

vi.mock('./use-rust-agent', async () => {
  const { createSignal } = await vi.importActual<typeof import('solid-js')>('solid-js')

  return {
    useRustAgent: () => {
      const [events, setEvents] = createSignal<AgentEvent[]>([])
      const [isRunning] = createSignal(false)
      const [streamingContent] = createSignal('')
      const [thinkingContent] = createSignal('')
      const [thinkingSegments] = createSignal([])
      const [activeToolCalls] = createSignal([])
      const [error, setError] = createSignal<string | null>(null)
      const [lastResult] = createSignal(null)
      const [currentRunId, setCurrentRunId] = createSignal<string | null>(null)
      const [tokenUsage] = createSignal({ input: 0, output: 0, cost: 0 })

      h.bind(
        (event) => setEvents((prev) => [...prev, event]),
        () => setEvents([]),
        setCurrentRunId
      )

      return {
        isRunning,
        streamingContent,
        thinkingContent,
        thinkingSegments,
        activeToolCalls,
        error,
        lastResult,
        currentRunId,
        tokenUsage,
        events,
        run: vi.fn(async () => null),
        editAndResendRun: vi.fn(async () => null),
        retryRun: vi.fn(async () => null),
        regenerateRun: vi.fn(async () => null),
        cancel: vi.fn(async () => {}),
        clearError: () => setError(null),
        endRun: vi.fn(),
        steer: vi.fn(async () => {}),
        followUp: vi.fn(async () => {}),
        postComplete: vi.fn(async () => {}),
        rehydrateStatus: h.rehydrateStatus,
        markToolApproval: vi.fn(),
        stop: vi.fn(async () => {}),
        isStreaming: isRunning,
        currentTokens: streamingContent,
        session: lastResult,
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

vi.mock('../stores/session', () => ({
  useSession: () => ({
    currentSession: () => ({ id: 'session-1', name: 'Session 1' }),
    messages: () => [],
    selectedModel: () => 'gpt-5.4',
    selectedProvider: () => 'openai',
    createNewSession: vi.fn(async () => ({ id: 'session-1', name: 'Session 1' })),
    addMessage: vi.fn(),
    addMessageToSession: vi.fn(),
    updateMessage: vi.fn(),
    updateMessageInSession: vi.fn(),
    deleteMessage: vi.fn(async () => {}),
    deleteMessageInSession: vi.fn(async () => {}),
    deleteMessagesAfter: vi.fn(),
    renameSession: vi.fn(async () => {}),
    stopEditing: vi.fn(),
    setMessageError: vi.fn(),
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
  rustBackend: { undoLastEdit: vi.fn(async () => ({ success: true, message: 'ok' })) },
}))

import { _resetAgentSingleton, useAgent } from './useAgent'

async function flushEffects(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

describe('useAgent interactive request clearing', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    h.reset()
    _resetAgentSingleton()
  })

  it('clears only the matching pending dock when Tauri emits a request-cleared event', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))

    h.emit({
      type: 'approval_request',
      id: 'approval-1',
      tool_call_id: 'call-1',
      tool_name: 'bash',
      args: { command: 'rm -rf /tmp/demo' },
      risk_level: 'high',
      reason: 'destructive command',
      warnings: [],
    })
    h.emit({
      type: 'question_request',
      id: 'question-1',
      question: 'Continue?',
      options: ['Yes', 'No'],
    })
    h.emit({
      type: 'plan_created',
      id: 'plan-1',
      plan: { summary: 'Ship polish', steps: [], estimatedTurns: 2 },
    })
    await flushEffects()

    expect(ctx.agent.pendingApproval()?.id).toBe('approval-1')
    expect(ctx.agent.pendingQuestion()?.id).toBe('question-1')
    expect(ctx.agent.pendingPlan()?.requestId).toBe('plan-1')

    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'plan-stale',
      request_kind: 'plan',
      timed_out: true,
    })
    await flushEffects()
    expect(ctx.agent.pendingPlan()?.requestId).toBe('plan-1')

    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'approval-1',
      request_kind: 'approval',
      timed_out: true,
    })
    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'question-1',
      request_kind: 'question',
      timed_out: false,
    })
    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'plan-1',
      request_kind: 'plan',
      timed_out: false,
    })
    await flushEffects()

    expect(ctx.agent.pendingApproval()).toBeNull()
    expect(ctx.agent.pendingQuestion()).toBeNull()
    expect(ctx.agent.pendingPlan()).toBeNull()

    ctx.dispose()
  })

  it('rehydrates backend status only once through the singleton agent store', () => {
    const first = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    const second = createRoot((dispose) => ({ agent: useAgent(), dispose }))

    expect(first.agent).toBe(second.agent)
    expect(h.rehydrateStatus).toHaveBeenCalledTimes(1)
    expect(h.rehydrateStatus).toHaveBeenCalledWith('session-1')

    first.dispose()
    second.dispose()
  })

  it('queues same-kind interactive requests by request id and promotes the next visible request on clear', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))

    h.emit({
      type: 'approval_request',
      id: 'approval-1',
      tool_call_id: 'call-1',
      tool_name: 'bash',
      args: { command: 'pwd' },
      risk_level: 'low',
      reason: 'read cwd',
      warnings: [],
    })
    h.emit({
      type: 'approval_request',
      id: 'approval-2',
      tool_call_id: 'call-2',
      tool_name: 'bash',
      args: { command: 'ls' },
      risk_level: 'low',
      reason: 'list cwd',
      warnings: [],
    })
    h.emit({
      type: 'question_request',
      id: 'question-1',
      question: 'First question?',
      options: ['Yes', 'No'],
    })
    h.emit({
      type: 'question_request',
      id: 'question-2',
      question: 'Second question?',
      options: ['Yes', 'No'],
    })
    h.emit({
      type: 'plan_created',
      id: 'plan-1',
      plan: { summary: 'First plan', steps: [], estimatedTurns: 1 },
    })
    h.emit({
      type: 'plan_created',
      id: 'plan-2',
      plan: { summary: 'Second plan', steps: [], estimatedTurns: 2 },
    })
    await flushEffects()

    expect(ctx.agent.pendingApproval()?.id).toBe('approval-1')
    expect(ctx.agent.pendingQuestion()?.id).toBe('question-1')
    expect(ctx.agent.pendingPlan()?.requestId).toBe('plan-1')

    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'approval-1',
      request_kind: 'approval',
      timed_out: false,
    })
    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'question-1',
      request_kind: 'question',
      timed_out: false,
    })
    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'plan-1',
      request_kind: 'plan',
      timed_out: false,
    })
    await flushEffects()

    expect(ctx.agent.pendingApproval()?.id).toBe('approval-2')
    expect(ctx.agent.pendingQuestion()?.id).toBe('question-2')
    expect(ctx.agent.pendingPlan()?.requestId).toBe('plan-2')

    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'approval-2',
      request_kind: 'approval',
      timed_out: false,
    })
    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'question-2',
      request_kind: 'question',
      timed_out: false,
    })
    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'plan-2',
      request_kind: 'plan',
      timed_out: false,
    })
    await flushEffects()

    expect(ctx.agent.pendingApproval()).toBeNull()
    expect(ctx.agent.pendingQuestion()).toBeNull()
    expect(ctx.agent.pendingPlan()).toBeNull()

    ctx.dispose()
  })

  it('ignores stale correlated interactive events from an older run', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    h.setRunId('desktop-run-current')

    h.emit({
      type: 'approval_request',
      id: 'approval-stale',
      tool_call_id: 'call-stale',
      tool_name: 'bash',
      args: { command: 'rm -rf /tmp/demo' },
      risk_level: 'high',
      reason: 'destructive command',
      warnings: [],
      run_id: 'desktop-run-old',
    })
    h.emit({
      type: 'question_request',
      id: 'question-current',
      question: 'Continue?',
      options: ['Yes', 'No'],
      run_id: 'desktop-run-current',
    })
    h.emit({
      type: 'plan_created',
      id: 'plan-stale',
      plan: { summary: 'Ship polish', steps: [], estimatedTurns: 2 },
      run_id: 'desktop-run-old',
    })
    await flushEffects()

    expect(ctx.agent.pendingApproval()).toBeNull()
    expect(ctx.agent.pendingQuestion()?.id).toBe('question-current')
    expect(ctx.agent.pendingPlan()).toBeNull()

    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'question-current',
      request_kind: 'question',
      timed_out: false,
      run_id: 'desktop-run-old',
    })
    await flushEffects()

    expect(ctx.agent.pendingQuestion()?.id).toBe('question-current')

    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'question-current',
      request_kind: 'question',
      timed_out: false,
      run_id: 'desktop-run-current',
    })
    await flushEffects()

    expect(ctx.agent.pendingQuestion()).toBeNull()

    ctx.dispose()
  })

  it('ignores uncorrelated interactive events while a web run is active', async () => {
    const ctx = createRoot((dispose) => ({ agent: useAgent(), dispose }))
    h.setRunId('web-run-current')

    h.emit({
      type: 'approval_request',
      id: 'approval-uncorrelated',
      tool_call_id: 'call-1',
      tool_name: 'bash',
      args: { command: 'pwd' },
      risk_level: 'low',
      reason: 'read cwd',
      warnings: [],
    })
    h.emit({
      type: 'question_request',
      id: 'question-uncorrelated',
      question: 'Continue?',
      options: ['Yes', 'No'],
    })
    h.emit({
      type: 'plan_created',
      id: 'plan-uncorrelated',
      plan: { summary: 'Ship polish', steps: [], estimatedTurns: 2 },
    })
    await flushEffects()

    expect(ctx.agent.pendingApproval()).toBeNull()
    expect(ctx.agent.pendingQuestion()).toBeNull()
    expect(ctx.agent.pendingPlan()).toBeNull()

    h.emit({
      type: 'question_request',
      id: 'question-current',
      question: 'Continue?',
      options: ['Yes', 'No'],
      run_id: 'web-run-current',
    })
    await flushEffects()

    expect(ctx.agent.pendingQuestion()?.id).toBe('question-current')

    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'question-current',
      request_kind: 'question',
      timed_out: false,
    })
    await flushEffects()

    expect(ctx.agent.pendingQuestion()?.id).toBe('question-current')

    h.emit({
      type: 'interactive_request_cleared',
      request_id: 'question-current',
      request_kind: 'question',
      timed_out: false,
      run_id: 'web-run-current',
    })
    await flushEffects()

    expect(ctx.agent.pendingQuestion()).toBeNull()

    ctx.dispose()
  })
})
