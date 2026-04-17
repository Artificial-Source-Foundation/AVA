/**
 * useAgent Hook — Unified Agent + Chat (Orchestrator)
 *
 * Single hook that drives ALL agent interactions in the desktop app.
 * Delegates to the Rust backend via useRustAgent() for all execution.
 * The TypeScript layer only manages UI state (approval bridge, plan mode, queuing).
 *
 * Sub-modules:
 * - useAgentStreaming.ts — offset tracking, derived streaming signals
 * - useAgentRun.ts — the main run() function
 * - useAgentActions.ts — steer, cancel, retry, regenerate, editAndResend, etc.
 */

import { isTauri } from '@tauri-apps/api/core'
import { type Accessor, batch, createEffect, createSignal, on, type Setter } from 'solid-js'

import { debugLog } from '../lib/debug-log'
import { log } from '../lib/logger'
import { applyCompactionResult } from '../services/context-compaction'
import { getCoreBudget } from '../services/core-bridge'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import type {
  ApprovalRequestEvent,
  InteractiveRequestClearedEvent,
  PlanCreatedEvent,
  PlanData,
  QuestionRequestEvent,
} from '../types/rust-ipc'
import type { AgentState, ApprovalRequest, ToolActivity } from './agent'
import type { QueuedMessage } from './chat/types'
import { useRustAgent } from './use-rust-agent'
import { createAgentActions } from './useAgentActions'
import { createAgentRun } from './useAgentRun'
import { createAgentStreaming } from './useAgentStreaming'

// Re-export types so existing consumers continue working
export type { AgentState, ApprovalRequest, ToolActivity }
export type { QueuedMessage }

/** A question the agent is asking the user */
export interface QuestionRequest {
  id: string
  question: string
  options: string[]
}

type PendingRequestWithId = {
  id?: string
  requestId?: string
}

// ============================================================================
// Singleton
// ============================================================================

type AgentStore = ReturnType<typeof createAgentStore>
let agentStoreSingleton: AgentStore | null = null

export function useAgent(): AgentStore {
  if (!agentStoreSingleton) {
    agentStoreSingleton = createAgentStore()
  }
  return agentStoreSingleton
}

/** Reset singleton for testing — not for production use */
export function _resetAgentSingleton(): void {
  agentStoreSingleton = null
}

// ============================================================================
// Store Factory
// ============================================================================

function createAgentStore() {
  const rustAgent = useRustAgent()
  const settingsRef = useSettings()
  const session = useSession()
  // Startup rehydration must stay on the singleton path so multiple
  // useRustAgent()-only consumers cannot duplicate listener side effects.
  if (isTauri()) {
    void rustAgent.rehydrateStatus()
  } else {
    createEffect(
      on(
        () => session.currentSession()?.id ?? null,
        (sessionId) => {
          if (!sessionId) {
            return
          }
          void rustAgent.rehydrateStatus(sessionId)
        }
      )
    )
  }
  let nextRunToken = 0
  let activeRunToken = 0

  const runOwnership = {
    beginRun(): number {
      activeRunToken = ++nextRunToken
      return activeRunToken
    },
    isCurrentRun(token: number): boolean {
      return activeRunToken === token
    },
  }

  // ── Frontend-only signals ───────────────────────────────────────────
  const [isPlanMode, setIsPlanMode] = createSignal(false)
  const [currentTurn, _setCurrentTurn] = createSignal(0)
  const [tokensUsed, _setTokensUsed] = createSignal(0)
  const [currentThought, setCurrentThought] = createSignal('')
  const [toolActivity, setToolActivity] = createSignal<ToolActivity[]>([])
  const [visiblePendingApproval, setVisiblePendingApproval] = createSignal<ApprovalRequest | null>(
    null
  )
  const [, setQueuedPendingApprovals] = createSignal<ApprovalRequest[]>([])
  const [visiblePendingQuestion, setVisiblePendingQuestion] = createSignal<QuestionRequest | null>(
    null
  )
  const [, setQueuedPendingQuestions] = createSignal<QuestionRequest[]>([])
  const [visiblePendingPlan, setVisiblePendingPlan] = createSignal<PlanData | null>(null)
  const [, setQueuedPendingPlans] = createSignal<PlanData[]>([])
  const [doomLoopDetected, setDoomLoopDetected] = createSignal(false)
  const [currentAgentId, _setCurrentAgentId] = createSignal<string | null>(null)
  const [streamingTokenEstimate, setStreamingTokenEstimate] = createSignal(0)
  const [streamingStartedAt, setStreamingStartedAt] = createSignal<number | null>(null)
  const [messageQueue, setMessageQueue] = createSignal<QueuedMessage[]>([])
  /**
   * ID of the placeholder assistant message that is added to the session store at
   * the START of each run, before any tokens arrive.  The same DOM node stays alive
   * throughout streaming (no flash on unmount) and is filled-in / replaced with the
   * final metadata when streaming ends.  Null when no run is in progress.
   */
  const [liveMessageId, setLiveMessageId] = createSignal<string | null>(null)

  // ── Streaming state (offsets + derived signals) ─────────────────────
  const streamingState = createAgentStreaming(rustAgent)

  // ── Forward agent events into UI signals ─────────────────────────────
  let lastEventIdx = 0
  const requestKey = <T extends PendingRequestWithId>(request: T | null): string | null =>
    request?.id ?? request?.requestId ?? null
  const upsertPendingRequest = <T extends PendingRequestWithId>(
    nextRequest: T,
    current: Accessor<T | null>,
    setCurrent: Setter<T | null>,
    setQueue: Setter<T[]>
  ): void => {
    const nextRequestId = requestKey(nextRequest)
    if (!nextRequestId) {
      return
    }

    if (requestKey(current()) === nextRequestId) {
      setCurrent(() => nextRequest)
      return
    }

    let updatedQueuedRequest = false
    setQueue((prev) => {
      const existingIndex = prev.findIndex((request) => requestKey(request) === nextRequestId)
      if (existingIndex === -1) {
        return prev
      }

      updatedQueuedRequest = true
      const next = [...prev]
      next[existingIndex] = nextRequest
      return next
    })

    if (updatedQueuedRequest) {
      return
    }

    if (!current()) {
      setCurrent(() => nextRequest)
      return
    }

    setQueue((prev) => [...prev, nextRequest])
  }
  const removePendingRequest = <T extends PendingRequestWithId>(
    requestId: string | null | undefined,
    current: Accessor<T | null>,
    setCurrent: Setter<T | null>,
    setQueue: Setter<T[]>
  ): void => {
    if (!requestId) {
      return
    }

    if (requestKey(current()) === requestId) {
      let nextVisible: T | null = null
      setQueue((prev) => {
        if (prev.length === 0) {
          return prev
        }

        const [nextRequest, ...remaining] = prev
        nextVisible = nextRequest ?? null
        return remaining
      })
      setCurrent(() => nextVisible)
      if (!nextVisible) {
        setCurrent(null)
      }
      return
    }

    setQueue((prev) => prev.filter((request) => requestKey(request) !== requestId))
  }
  const clearPendingRequests = <T extends PendingRequestWithId>(
    setCurrent: Setter<T | null>,
    setQueue: Setter<T[]>
  ): void => {
    setCurrent(null)
    setQueue([])
  }
  const pendingApproval = (): ApprovalRequest | null => visiblePendingApproval()
  const pendingQuestion = (): QuestionRequest | null => visiblePendingQuestion()
  const pendingPlan = (): PlanData | null => visiblePendingPlan()
  const removePendingApproval = (requestId: string | null | undefined): void => {
    removePendingRequest(
      requestId,
      visiblePendingApproval,
      setVisiblePendingApproval,
      setQueuedPendingApprovals
    )
  }
  const removePendingQuestion = (requestId: string | null | undefined): void => {
    removePendingRequest(
      requestId,
      visiblePendingQuestion,
      setVisiblePendingQuestion,
      setQueuedPendingQuestions
    )
  }
  const removePendingPlan = (requestId: string | null | undefined): void => {
    removePendingRequest(
      requestId,
      visiblePendingPlan,
      setVisiblePendingPlan,
      setQueuedPendingPlans
    )
  }
  const clearPendingInteractiveRequests = (): void => {
    batch(() => {
      clearPendingRequests(setVisiblePendingApproval, setQueuedPendingApprovals)
      clearPendingRequests(setVisiblePendingQuestion, setQueuedPendingQuestions)
      clearPendingRequests(setVisiblePendingPlan, setQueuedPendingPlans)
    })
  }
  const eventRunId = (event: { run_id?: string; runId?: string }): string | null =>
    event.runId ?? event.run_id ?? null
  const shouldHandleInteractiveEvent = (
    event:
      | ApprovalRequestEvent
      | QuestionRequestEvent
      | InteractiveRequestClearedEvent
      | PlanCreatedEvent
  ): boolean => {
    const correlatedRunId = eventRunId(event)
    const activeRunId = rustAgent.currentRunId()

    if (!correlatedRunId) {
      if (activeRunId && !isTauri()) {
        log.warn('agent', 'Ignoring uncorrelated interactive event during active web run', {
          eventType: event.type,
          activeRunId,
        })
        return false
      }

      return true
    }

    if (!activeRunId || correlatedRunId !== activeRunId) {
      log.warn('agent', 'Ignoring stale interactive event', {
        eventType: event.type,
        eventRunId: correlatedRunId,
        activeRunId,
      })
      return false
    }

    return true
  }

  createEffect(
    on(rustAgent.events, (allEvents) => {
      // Reset cursor when events array is cleared (new run started)
      if (allEvents.length < lastEventIdx) {
        lastEventIdx = 0
      }
      for (let i = lastEventIdx; i < allEvents.length; i++) {
        const event = allEvents[i]!

        // ── UI signals: approval / question / thinking / tokens ────
        if (event.type === 'tool_call') {
          log.debug('agent', 'Tool called', { tool: (event as { name?: string }).name })
        }
        // Sync thinking content to frontend signal
        if (event.type === 'thinking') {
          const chunk = (event as { content: string }).content
          setCurrentThought((prev) => {
            const updated = prev + chunk
            debugLog('thinking', 'currentThought updated:', updated.length, 'chars total')
            return updated
          })
        }
        // Sync real token counts to the ContextBudget on each turn
        if (event.type === 'token_usage') {
          const tu = event as unknown as Record<string, number>
          const input = tu.input_tokens ?? tu.inputTokens ?? 0
          const output = tu.output_tokens ?? tu.outputTokens ?? 0
          const bgt = getCoreBudget()
          if (bgt) {
            bgt.setUsed(input + output)
            window.dispatchEvent(
              new CustomEvent('ava:core-settings-changed', { detail: { category: 'context' } })
            )
          }
        }
        if (event.type === 'context_compacted') {
          const compact = event as import('../types/rust-ipc').ContextCompactedEvent &
            Record<string, unknown>
          applyCompactionResult(
            {
              messages:
                ((compact.activeMessages ??
                  compact.active_messages) as import('../types/rust-ipc').CompactMessageOut[]) ??
                [],
              tokensBefore: Number(compact.tokensBefore ?? compact.tokens_before ?? 0),
              tokensAfter: Number(compact.tokensAfter ?? compact.tokens_after ?? 0),
              tokensSaved: Number(compact.tokensSaved ?? compact.tokens_saved ?? 0),
              messagesBefore: Number(compact.messagesBefore ?? compact.messages_before ?? 0),
              messagesAfter: Number(compact.messagesAfter ?? compact.messages_after ?? 0),
              summary: String(compact.summary ?? ''),
              contextSummary: String(compact.contextSummary ?? compact.context_summary ?? ''),
              usageBeforePercent: Number(
                compact.usageBeforePercent ?? compact.usage_before_percent ?? 0
              ),
            } as import('../types/rust-ipc').CompactContextResult,
            'auto',
            { appendSummaryMessage: false }
          )
        }
        if (event.type === 'approval_request') {
          const approvalEvent = event as ApprovalRequestEvent
          if (!shouldHandleInteractiveEvent(approvalEvent)) {
            continue
          }
          log.info('tools', 'Approval requested', {
            tool: approvalEvent.tool_name,
            risk: approvalEvent.risk_level,
          })
          const riskLevel = (
            ['low', 'medium', 'high', 'critical'].includes(approvalEvent.risk_level)
              ? approvalEvent.risk_level
              : 'medium'
          ) as 'low' | 'medium' | 'high' | 'critical'

          const toolName = approvalEvent.tool_name
          const toolType =
            toolName === 'bash'
              ? ('command' as const)
              : toolName.startsWith('mcp_')
                ? ('mcp' as const)
                : ('file' as const)

          upsertPendingRequest(
            {
              id: approvalEvent.id,
              toolCallId: approvalEvent.tool_call_id,
              type: toolType,
              toolName,
              args: approvalEvent.args as Record<string, unknown>,
              description: approvalEvent.reason,
              riskLevel,
              resolve: () => {}, // not used — resolution goes through IPC
            },
            visiblePendingApproval,
            setVisiblePendingApproval,
            setQueuedPendingApprovals
          )
        }
        if (event.type === 'question_request') {
          const questionEvent = event as QuestionRequestEvent
          if (!shouldHandleInteractiveEvent(questionEvent)) {
            continue
          }
          log.info('agent', 'Question requested', {
            question: questionEvent.question?.slice(0, 80),
          })
          upsertPendingRequest(
            {
              id: questionEvent.id,
              question: questionEvent.question,
              options: questionEvent.options,
            },
            visiblePendingQuestion,
            setVisiblePendingQuestion,
            setQueuedPendingQuestions
          )
        }
        if (event.type === 'interactive_request_cleared') {
          const clearedEvent = event as InteractiveRequestClearedEvent
          if (!shouldHandleInteractiveEvent(clearedEvent)) {
            continue
          }
          const requestId = clearedEvent.request_id ?? null
          const requestKind = clearedEvent.request_kind

          if (requestKind === 'approval') {
            removePendingApproval(requestId)
          }
          if (requestKind === 'question') {
            removePendingQuestion(requestId)
          }
          if (requestKind === 'plan') {
            removePendingPlan(requestId)
          }
        }
        if (event.type === 'plan_created') {
          const planEvent = event as PlanCreatedEvent
          if (!shouldHandleInteractiveEvent(planEvent)) {
            continue
          }
          upsertPendingRequest(
            {
              ...planEvent.plan,
              requestId: planEvent.id,
            },
            visiblePendingPlan,
            setVisiblePendingPlan,
            setQueuedPendingPlans
          )
        }
      }
      lastEventIdx = allEvents.length
    })
  )

  // ── Compose sub-modules ─────────────────────────────────────────────

  const visibleMessageQueue = (): QueuedMessage[] => {
    const currentSessionId = session.currentSession()?.id
    return messageQueue().filter(
      (message) => !message.sessionId || message.sessionId === currentSessionId
    )
  }

  const actions = createAgentActions({
    rustAgent,
    session,
    settingsRef,
    isPlanMode,
    setIsPlanMode,
    currentTurn,
    tokensUsed,
    currentThought,
    setCurrentThought,
    toolActivity,
    setToolActivity,
    pendingApproval,
    pendingQuestion,
    pendingPlan,
    removePendingApproval,
    removePendingQuestion,
    removePendingPlan,
    clearPendingInteractiveRequests,
    doomLoopDetected,
    setDoomLoopDetected,
    streamingTokenEstimate,
    setStreamingTokenEstimate,
    streamingStartedAt,
    setStreamingStartedAt,
    messageQueue,
    setMessageQueue,
    liveMessageId,
    setLiveMessageId,
    streaming: streamingState,
    runOwnership,
  })

  const runModule = createAgentRun({
    rustAgent,
    session,
    settingsRef,
    isPlanMode,
    setCurrentThought,
    setDoomLoopDetected,
    setToolActivity,
    setStreamingTokenEstimate,
    streamingStartedAt,
    setStreamingStartedAt,
    messageQueue,
    setMessageQueue,
    liveMessageId,
    setLiveMessageId,
    streaming: streamingState,
    runOwnership,
  })

  // ====================================================================
  // Return full public API (identical shape to original)
  // ====================================================================

  return {
    // ── Agent signals (mapped from Rust agent) ───────────────────────
    isRunning: rustAgent.isRunning,
    currentRunId: rustAgent.currentRunId,
    progressMessage: rustAgent.progressMessage,
    isPlanMode,
    currentTurn,
    tokensUsed,
    currentThought,
    toolActivity,
    pendingApproval,
    pendingQuestion,
    pendingPlan,
    doomLoopDetected,
    lastError: rustAgent.error,
    currentAgentId,
    eventTimeline: rustAgent.events,

    // ── Chat signals (mapped from Rust agent) ────────────────────────
    // These use offset-aware derived signals so that after a steering message
    // splits the response, only the current placeholder's content is shown.
    isStreaming: rustAgent.isRunning, // alias for backward compat
    activeToolCalls: streamingState.liveActiveToolCalls,
    streamingContent: streamingState.liveStreamingContent,
    thinkingSegments: streamingState.liveThinkingSegments,
    streamingTokenEstimate,
    streamingStartedAt,
    error: streamingState.error,
    messageQueue: visibleMessageQueue,
    queuedCount: () => visibleMessageQueue().length,
    /**
     * ID of the placeholder message that was pre-added to the session store at the
     * start of the current run.  The MessageList uses this to identify which message
     * row is the live streaming bubble so it can pass live signals down to it.
     * Null when no run is in progress.
     */
    liveMessageId,

    // ── Actions ──────────────────────────────────────────────────────
    run: runModule.run,
    cancel: actions.cancel,
    steer: actions.steer,
    followUp: actions.followUp,
    postComplete: actions.postComplete,
    retryMessage: actions.retryMessage,
    editAndResend: actions.editAndResend,
    regenerateResponse: actions.regenerateResponse,
    undoLastEdit: actions.undoLastEdit,

    // ── Queue ────────────────────────────────────────────────────────
    removeFromQueue: actions.removeFromQueue,
    reorderInQueue: actions.reorderInQueue,
    editInQueue: actions.editInQueue,
    clearQueue: actions.clearQueue,

    // ── Agent-specific ──────────────────────────────────────────────
    togglePlanMode: actions.togglePlanMode,
    checkAutoApproval: actions.checkAutoApproval,
    resolveApproval: actions.resolveApproval,
    resolveQuestion: actions.resolveQuestion,
    resolvePlan: actions.resolvePlan,
    clearError: actions.clearError,
    getState: actions.getState,
  }
}
