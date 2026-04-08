import { createRoot, createSignal } from 'solid-js'
import { describe, expect, it, vi } from 'vitest'
import type { ToolCall } from '../types'
import type { AgentEvent, PlanData, TodoItem } from '../types/rust-ipc'
import { createBoundedEventHistory } from './event-history'
import {
  type CompletionResolver,
  createAgentEventHandler,
  type StreamingMetrics,
} from './rust-agent-events'
import type { ThinkingSegment } from './use-rust-agent'

vi.mock('../stores/layout', () => ({
  useLayout: () => ({ switchRightPanelTab: vi.fn() }),
}))

vi.mock('../stores/planOverlayStore', () => ({
  usePlanOverlay: () => ({ markStepComplete: vi.fn() }),
}))

function createHandlerHarness(opts?: { isTauriRuntime?: boolean }) {
  return createRoot((dispose) => {
    const history = createBoundedEventHistory<AgentEvent>(64)
    const [streamingContent, setStreamingContent] = createSignal('')
    const [thinkingContent, setThinkingContent] = createSignal('')
    const [activeToolCalls, setActiveToolCalls] = createSignal<ToolCall[]>([])
    const [error, setError] = createSignal<string | null>(null)
    const [isRunning, setIsRunning] = createSignal(false)
    const [tokenUsage, setTokenUsage] = createSignal({ input: 0, output: 0, cost: 0 })
    const [progressMessage, setProgressMessage] = createSignal<string | null>(null)
    const [budgetWarning, setBudgetWarning] = createSignal<{
      thresholdPercent: number
      currentCostUsd: number
      maxBudgetUsd: number
    } | null>(null)
    const [pendingPlan, setPendingPlan] = createSignal<PlanData | null>(null)
    const [thinkingSegments, setThinkingSegments] = createSignal<ThinkingSegment[]>([])
    const [todos, setTodos] = createSignal<TodoItem[]>([])

    const metrics: StreamingMetrics = {
      chunkCount: 0,
      totalTextLen: 0,
      runStartTime: 0,
      firstTokenLogged: false,
      pendingToolNames: [],
    }
    const completion: CompletionResolver = { resolve: null }

    const handler = createAgentEventHandler({
      metrics,
      completion,
      appendEvent: history.append,
      setStreamingContent,
      setThinkingContent,
      setActiveToolCalls,
      setError,
      setIsRunning,
      setTokenUsage,
      setProgressMessage,
      setBudgetWarning,
      setPendingPlan,
      setThinkingSegments,
      setTodos,
      isTauriRuntime: opts?.isTauriRuntime ?? true,
    })

    return {
      dispose,
      handler,
      events: history.events,
      streamingContent,
      thinkingContent,
      activeToolCalls,
      error,
      isRunning,
      tokenUsage,
      progressMessage,
      budgetWarning,
      pendingPlan,
      thinkingSegments,
      todos,
      completion,
    }
  })
}

describe('createAgentEventHandler', () => {
  it('uses backend tool ids and avoids duplicate tool-call entries', () => {
    const harness = createHandlerHarness()

    harness.handler({
      type: 'tool_call',
      id: 'call-123',
      name: 'read',
      args: { path: 'src/main.rs' },
    })
    harness.handler({
      type: 'tool_call',
      id: 'call-123',
      name: 'read',
      args: { path: 'src/main.rs' },
    })

    expect(harness.activeToolCalls()).toHaveLength(1)
    expect(harness.activeToolCalls()[0]?.id).toBe('call-123')
    expect((harness.events()[0] as { timestamp?: number }).timestamp).toBeTypeOf('number')

    harness.dispose()
  })

  it('matches tool results by call_id even when results arrive out of order', () => {
    const harness = createHandlerHarness()

    harness.handler({
      type: 'tool_call',
      id: 'call-a',
      name: 'read',
      args: { path: 'a.rs' },
    })
    harness.handler({
      type: 'tool_call',
      id: 'call-b',
      name: 'read',
      args: { path: 'b.rs' },
    })

    harness.handler({
      type: 'tool_result',
      call_id: 'call-b',
      content: 'b done',
      is_error: false,
    })

    expect(harness.activeToolCalls()[0]?.status).toBe('running')
    expect(harness.activeToolCalls()[1]?.status).toBe('success')
    expect(harness.activeToolCalls()[1]?.output).toBe('b done')

    harness.handler({
      type: 'tool_result',
      call_id: 'call-a',
      content: 'a failed',
      is_error: true,
    })

    expect(harness.activeToolCalls()[0]?.status).toBe('error')
    expect(harness.activeToolCalls()[0]?.output).toBe('a failed')

    harness.dispose()
  })
})
