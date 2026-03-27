import { isTauri } from '@tauri-apps/api/core'
import { batch, createSignal, onCleanup } from 'solid-js'
import type { ToolCall } from '../types'
import type { AgentEvent, PlanData, SubmitGoalResult, TodoItem } from '../types/rust-ipc'
import {
  type CompletionResolver,
  createAgentEventHandler,
  type StreamingMetrics,
} from './rust-agent-events'
import { createAgentIpc } from './rust-agent-ipc'

/**
 * A thinking segment: thinking content that occurred before a group of tool calls.
 * Used to reconstruct the interleaved thinking->tools->thinking->response sequence.
 */
export interface ThinkingSegment {
  /** Accumulated thinking text for this segment */
  thinking: string
  /** IDs of tool calls that followed this thinking block (may be empty for final thinking) */
  toolCallIds: string[]
}

// Module-level shared signal for todos — all useRustAgent instances share this
// so TodoPanel can read todos set by the agent event handler.
const [todos, setTodos] = createSignal<TodoItem[]>([])

export function useRustAgent() {
  const [isRunning, setIsRunning] = createSignal(false)
  const [streamingContent, setStreamingContent] = createSignal('')
  const [thinkingContent, setThinkingContent] = createSignal('')
  const [activeToolCalls, setActiveToolCalls] = createSignal<ToolCall[]>([])
  const [error, setError] = createSignal<string | null>(null)
  const [lastResult, setLastResult] = createSignal<SubmitGoalResult | null>(null)
  const [tokenUsage, setTokenUsage] = createSignal({ input: 0, output: 0, cost: 0 })
  const [events, setEvents] = createSignal<AgentEvent[]>([])
  const [progressMessage, setProgressMessage] = createSignal<string | null>(null)
  const [budgetWarning, setBudgetWarning] = createSignal<{
    thresholdPercent: number
    currentCostUsd: number
    maxBudgetUsd: number
  } | null>(null)
  const [pendingPlan, setPendingPlan] = createSignal<PlanData | null>(null)
  // Interleaved thinking segments: each entry is a block of thinking + the tool calls that followed
  const [thinkingSegments, setThinkingSegments] = createSignal<ThinkingSegment[]>([])

  // Shared mutable state for streaming metrics and completion resolution
  const metrics: StreamingMetrics = {
    chunkCount: 0,
    totalTextLen: 0,
    runStartTime: 0,
    firstTokenLogged: false,
    pendingToolNames: [],
  }
  const completion: CompletionResolver = { resolve: null }

  // Create the event handler
  const handleAgentEvent = createAgentEventHandler({
    metrics,
    completion,
    setEvents,
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
    isTauriRuntime: isTauri(),
  })

  const resetState = (): void => {
    batch(() => {
      setEvents([])
      setStreamingContent('')
      setThinkingContent('')
      setActiveToolCalls([])
      setError(null)
      setLastResult(null)
      setTokenUsage({ input: 0, output: 0, cost: 0 })
      setProgressMessage(null)
      setBudgetWarning(null)
      setPendingPlan(null)
      setThinkingSegments([])
      setTodos([])
    })
  }

  // Create IPC functions
  const ipc = createAgentIpc({
    metrics,
    completion,
    isRunning,
    setIsRunning,
    setError,
    setLastResult,
    setActiveToolCalls,
    handleAgentEvent,
    resetState,
  })

  const clearError = (): void => {
    setError(null)
  }

  /**
   * Tag the most-recently-started tool call for `toolName` with an approval decision.
   * Called by `useAgent.resolveApproval` right after the user acts on the ApprovalDock.
   */
  const markToolApproval = (
    toolName: string,
    decision: 'once' | 'always' | 'denied',
    toolCallId?: string
  ): void => {
    setActiveToolCalls((prev) => {
      let realIdx = -1
      if (toolCallId) {
        realIdx = prev.findIndex((tc) => tc.id === toolCallId)
      } else {
        for (let i = prev.length - 1; i >= 0; i -= 1) {
          if (prev[i]?.name === toolName) {
            realIdx = i
            break
          }
        }
      }
      if (realIdx === -1) return prev
      const updated = [...prev]
      updated[realIdx] = { ...prev[realIdx]!, approvalDecision: decision }
      return updated
    })
  }

  onCleanup(() => {
    ipc.destroyListener()
  })

  return {
    isRunning,
    streamingContent,
    thinkingContent,
    activeToolCalls,
    thinkingSegments,
    error,
    lastResult,
    tokenUsage,
    events,
    progressMessage,
    budgetWarning,
    pendingPlan,
    todos,
    run: ipc.run,
    editAndResendRun: ipc.editAndResendRun,
    cancel: ipc.cancel,
    clearError,
    endRun: ipc.endRun,
    // Mid-stream messaging
    steer: ipc.steer,
    followUp: ipc.followUp,
    postComplete: ipc.postComplete,
    markToolApproval,
    // Aliases for compatibility
    stop: ipc.cancel,
    isStreaming: isRunning,
    currentTokens: streamingContent,
    session: lastResult,
  }
}
