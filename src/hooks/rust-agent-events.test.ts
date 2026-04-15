import { createRoot, createSignal } from 'solid-js'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import type { ToolCall } from '../types'
import type { AgentEvent, PlanData, TodoItem } from '../types/rust-ipc'
import { createBoundedEventHistory } from './event-history'
import {
  type CompletionResolver,
  createAgentEventHandler,
  type StreamingMetrics,
} from './rust-agent-events'
import type { ThinkingSegment } from './use-rust-agent'

const switchRightPanelTabMock = vi.fn()
const markStepCompleteMock = vi.fn()

vi.mock('../stores/layout', () => ({
  useLayout: () => ({ switchRightPanelTab: switchRightPanelTabMock }),
}))

vi.mock('../stores/planOverlayStore', () => ({
  usePlanOverlay: () => ({ markStepComplete: markStepCompleteMock }),
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
  beforeEach(() => {
    switchRightPanelTabMock.mockReset()
    markStepCompleteMock.mockReset()
  })

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

  it('promotes streaming edit progress into a live tool call and merges the later tool_call payload', () => {
    const harness = createHandlerHarness()

    harness.handler({
      type: 'streaming_edit_progress',
      call_id: 'edit-1',
      tool_name: 'apply_patch',
      file_path: 'src/main.rs',
      bytes_received: 256,
    })

    expect(harness.activeToolCalls()).toHaveLength(1)
    expect(harness.activeToolCalls()[0]).toMatchObject({
      id: 'edit-1',
      name: 'apply_patch',
      filePath: 'src/main.rs',
      status: 'running',
    })
    expect(harness.activeToolCalls()[0]?.streamingOutput).toContain('256 bytes')

    harness.handler({
      type: 'tool_call',
      id: 'edit-1',
      name: 'apply_patch',
      args: { patch: '*** Begin Patch' },
    })

    expect(harness.activeToolCalls()).toHaveLength(1)
    expect(harness.activeToolCalls()[0]?.args).toMatchObject({ patch: '*** Begin Patch' })

    harness.dispose()
  })

  it('tracks plan request ids and projected subagent completions', () => {
    const harness = createHandlerHarness()

    harness.handler({
      type: 'plan_created',
      id: 'plan-1',
      plan: {
        summary: 'Ship polish',
        steps: [],
        estimatedTurns: 2,
      },
    })

    expect(harness.pendingPlan()).toMatchObject({ requestId: 'plan-1', summary: 'Ship polish' })

    harness.handler({
      type: 'tool_call',
      id: 'delegate-1',
      name: 'task',
      args: { description: 'Investigate parser bug' },
    })
    harness.handler({
      type: 'subagent_complete',
      call_id: 'delegate-1',
      session_id: 'child-session',
      description: 'Investigate parser bug',
      input_tokens: 120,
      output_tokens: 80,
      cost_usd: 0.42,
      agent_type: 'reviewer',
      provider: 'openai',
      resumed: true,
    })

    expect(harness.activeToolCalls()[0]).toMatchObject({
      id: 'delegate-1',
      status: 'success',
    })
    expect(harness.activeToolCalls()[0]?.output).toContain('Session: child-session')

    harness.dispose()
  })

  it('projects plan step completion into the plan overlay store', () => {
    const harness = createHandlerHarness()

    harness.handler({
      type: 'plan_step_complete',
      step_id: 'step-2',
    })

    expect(markStepCompleteMock).toHaveBeenCalledWith('step-2')

    harness.dispose()
  })

  it('resolves terminal completion in Tauri mode on complete events', async () => {
    const harness = createHandlerHarness({ isTauriRuntime: true })
    const settled = vi.fn()
    harness.completion.resolve = settled

    harness.handler({
      type: 'complete',
      session: {
        id: 'session-123',
        messages: [],
        completed: true,
      },
    })

    expect(settled).toHaveBeenCalledWith({
      success: true,
      turns: 0,
      sessionId: 'session-123',
    })
    expect(harness.isRunning()).toBe(false)

    harness.dispose()
  })

  it('resolves terminal completion in Tauri mode on error events', () => {
    const harness = createHandlerHarness({ isTauriRuntime: true })
    const settled = vi.fn()
    harness.completion.resolve = settled

    harness.handler({
      type: 'error',
      message: 'backend session missing',
    })

    expect(settled).toHaveBeenCalledWith(null)
    expect(harness.error()).toBe('backend session missing')
    expect(harness.isRunning()).toBe(false)

    harness.dispose()
  })
})
