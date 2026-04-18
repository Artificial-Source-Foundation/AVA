import { isTauri } from '@tauri-apps/api/core'
import { batch, createSignal, onCleanup } from 'solid-js'
import type { ToolCall } from '../types'
import type { AgentEvent, PlanData, SubmitGoalResult, TodoItem } from '../types/rust-ipc'
import { createBoundedEventHistory } from './event-history'
import {
  type CompletionResolver,
  createAgentEventHandler,
  type StreamingMetrics,
} from './rust-agent-events'
import { createAgentIpc, type RehydrateResult } from './rust-agent-ipc'

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

export interface AgentRuntimeSnapshot {
  isRunning: boolean
  streamingContent: string
  thinkingContent: string
  activeToolCalls: ToolCall[]
  error: string | null
  lastResult: SubmitGoalResult | null
  currentRunId: string | null
  trackedSessionId: string | null
  detachedSessionId: string | null
  tokenUsage: { input: number; output: number; cost: number }
  events: AgentEvent[]
  progressMessage: string | null
  budgetWarning: {
    thresholdPercent: number
    currentCostUsd: number
    maxBudgetUsd: number
  } | null
  pendingPlan: PlanData | null
  thinkingSegments: ThinkingSegment[]
  todos: TodoItem[]
  binding: {
    activeRunId: string | null
    attachedSessionId: string | null
  }
}

export type AgentRehydrateResult = RehydrateResult

interface CaptureRuntimeSnapshotOptions {
  maxEvents?: number
}

// Module-level shared signal for todos — all useRustAgent instances share this
// so TodoPanel can read todos set by the agent event handler.
const [todos, setTodos] = createSignal<TodoItem[]>([])

/** Clear todos when switching sessions so stale items don't bleed across chats. */
export function clearTodos(): void {
  setTodos([])
}
const MAX_EVENT_HISTORY = 4000

export function useRustAgent() {
  const [isRunning, setIsRunning] = createSignal(false)
  const [streamingContent, setStreamingContent] = createSignal('')
  const [thinkingContent, setThinkingContent] = createSignal('')
  const [activeToolCalls, setActiveToolCalls] = createSignal<ToolCall[]>([])
  const [error, setError] = createSignal<string | null>(null)
  const [lastResult, setLastResult] = createSignal<SubmitGoalResult | null>(null)
  const [currentRunId, setCurrentRunId] = createSignal<string | null>(null)
  const [trackedSessionId, setTrackedSessionId] = createSignal<string | null>(null)
  const [detachedSessionId, setDetachedSessionId] = createSignal<string | null>(null)
  const [tokenUsage, setTokenUsage] = createSignal({ input: 0, output: 0, cost: 0 })
  const eventHistory = createBoundedEventHistory<AgentEvent>(MAX_EVENT_HISTORY)
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
    appendEvent: eventHistory.append,
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
      eventHistory.clear()
      setStreamingContent('')
      setThinkingContent('')
      setActiveToolCalls([])
      setError(null)
      setLastResult(null)
      setCurrentRunId(null)
      setTrackedSessionId(null)
      setDetachedSessionId(null)
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
    setCurrentRunId,
    setTrackedSessionId,
    setDetachedSessionId,
    setActiveToolCalls,
    handleAgentEvent,
    resetState,
  })

  const clearError = (): void => {
    setError(null)
  }

  const clearDetachedSessionId = (): void => {
    setDetachedSessionId(null)
  }

  const captureRuntimeSnapshot = (
    options: CaptureRuntimeSnapshotOptions = {}
  ): AgentRuntimeSnapshot => {
    const events = eventHistory.snapshot()
    const maxEvents =
      typeof options.maxEvents === 'number' && options.maxEvents >= 0
        ? Math.floor(options.maxEvents)
        : null

    return {
      isRunning: isRunning(),
      streamingContent: streamingContent(),
      thinkingContent: thinkingContent(),
      activeToolCalls: [...activeToolCalls()],
      error: error(),
      lastResult: lastResult(),
      currentRunId: currentRunId(),
      trackedSessionId: trackedSessionId(),
      detachedSessionId: detachedSessionId(),
      tokenUsage: { ...tokenUsage() },
      events: maxEvents !== null && events.length > maxEvents ? events.slice(-maxEvents) : events,
      progressMessage: progressMessage(),
      budgetWarning: budgetWarning() ? { ...budgetWarning()! } : null,
      pendingPlan: pendingPlan() ? { ...pendingPlan()! } : null,
      thinkingSegments: [...thinkingSegments()],
      todos: [...todos()],
      binding: ipc.captureSessionBinding(),
    }
  }

  const restoreRuntimeSnapshot = (snapshot: AgentRuntimeSnapshot | null): void => {
    if (!snapshot) {
      ipc.restoreSessionBinding({ activeRunId: null, attachedSessionId: null })
      resetState()
      return
    }

    ipc.restoreSessionBinding(snapshot.binding)
    batch(() => {
      eventHistory.replace(snapshot.events)
      setIsRunning(snapshot.isRunning)
      setStreamingContent(snapshot.streamingContent)
      setThinkingContent(snapshot.thinkingContent)
      setActiveToolCalls([...snapshot.activeToolCalls])
      setError(snapshot.error)
      setLastResult(snapshot.lastResult)
      setCurrentRunId(snapshot.currentRunId)
      setTrackedSessionId(snapshot.trackedSessionId)
      setDetachedSessionId(snapshot.detachedSessionId)
      setTokenUsage({ ...snapshot.tokenUsage })
      setProgressMessage(snapshot.progressMessage)
      setBudgetWarning(snapshot.budgetWarning ? { ...snapshot.budgetWarning } : null)
      setPendingPlan(snapshot.pendingPlan ? { ...snapshot.pendingPlan } : null)
      setThinkingSegments([...snapshot.thinkingSegments])
      setTodos([...snapshot.todos])
    })
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
    currentRunId,
    trackedSessionId,
    detachedSessionId,
    tokenUsage,
    events: eventHistory.events,
    eventVersion: eventHistory.version,
    eventCursor: eventHistory.cursor,
    readEventsSince: eventHistory.readSince,
    progressMessage,
    budgetWarning,
    pendingPlan,
    todos,
    run: ipc.run,
    editAndResendRun: ipc.editAndResendRun,
    retryRun: ipc.retryRun,
    regenerateRun: ipc.regenerateRun,
    cancel: ipc.cancel,
    clearError,
    endRun: ipc.endRun,
    // Mid-stream messaging
    steer: ipc.steer,
    followUp: ipc.followUp,
    postComplete: ipc.postComplete,
    rehydrateStatus: ipc.rehydrateStatus,
    resetState,
    captureRuntimeSnapshot,
    restoreRuntimeSnapshot,
    clearDetachedSessionId,
    markToolApproval,
    // Aliases for compatibility
    stop: ipc.cancel,
    isStreaming: isRunning,
    currentTokens: streamingContent,
    session: lastResult,
  }
}
