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
import {
  type Accessor,
  batch,
  createEffect,
  createMemo,
  createSignal,
  on,
  type Setter,
} from 'solid-js'
import { createBoundedSessionCache } from '../lib/bounded-session-cache'
import { debugLog } from '../lib/debug-log'
import { log } from '../lib/logger'
import { applyCompactionResult } from '../services/context-compaction'
import { getCoreBudget } from '../services/core-bridge'
import { getMessages } from '../services/database'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import type { ToolCall } from '../types'
import type {
  AgentEvent,
  ApprovalRequestEvent,
  InteractiveRequestClearedEvent,
  PlanCreatedEvent,
  PlanData,
  QuestionRequestEvent,
} from '../types/rust-ipc'
import type { AgentState, ApprovalRequest, ToolActivity } from './agent'
import type { QueuedMessage } from './chat/types'
import {
  type AgentRehydrateResult,
  type AgentRuntimeSnapshot,
  type ThinkingSegment,
  useRustAgent,
} from './use-rust-agent'
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

interface AgentUiRuntimeSnapshot {
  isPlanMode: boolean
  currentTurn: number
  tokensUsed: number
  currentThought: string
  toolActivity: ToolActivity[]
  visiblePendingApproval: ApprovalRequest | null
  queuedPendingApprovals: ApprovalRequest[]
  visiblePendingQuestion: QuestionRequest | null
  queuedPendingQuestions: QuestionRequest[]
  visiblePendingPlan: PlanData | null
  queuedPendingPlans: PlanData[]
  doomLoopDetected: boolean
  currentAgentId: string | null
  streamingTokenEstimate: number
  streamingStartedAt: number | null
  liveMessageId: string | null
  streamingContentOffset: number
  toolCallsOffset: number
  thinkingSegmentsOffset: number
}

interface CachedSessionRuntime {
  rust: AgentRuntimeSnapshot
  ui: AgentUiRuntimeSnapshot
}

const RECENT_SESSION_RUNTIME_CACHE_LIMIT = 3
const MAX_CACHED_RUNTIME_EVENTS = 1000

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
  const refreshSessionMessages = async (sessionId: string): Promise<void> => {
    if (isTauri() || !session.replaceMessagesFromBackend) {
      return
    }

    const backendMessages = await getMessages(sessionId)
    if (session.currentSession()?.id !== sessionId) {
      return
    }

    session.replaceMessagesFromBackend(backendMessages)
  }
  let lastVisibleSessionId: string | null = null
  // Startup rehydration must stay on the singleton path so multiple
  // useRustAgent()-only consumers cannot duplicate listener side effects.
  createEffect(
    on(
      () => session.currentSession()?.id ?? null,
      (sessionId) => {
        if (lastVisibleSessionId && lastVisibleSessionId !== sessionId) {
          cacheSessionRuntime(lastVisibleSessionId)
        }
        lastVisibleSessionId = sessionId

        const restoredCachedRuntime = sessionId
          ? (sessionRuntimeCache.get(sessionId) ?? null)
          : null
        restoreSessionRuntime(sessionId, restoredCachedRuntime)

        void (async () => {
          const rehydrateResult = await rustAgent.rehydrateStatus(sessionId)
          if (session.currentSession()?.id !== sessionId) {
            return
          }
          reconcileRuntimeAfterRehydrate(sessionId, rehydrateResult, restoredCachedRuntime)
          if (sessionId && session.currentSession()?.id === sessionId && !rehydrateResult.running) {
            await refreshSessionMessages(sessionId).catch(() => {})
          }
        })()
      }
    )
  )
  createEffect(
    on(
      () => ({
        sessionId: session.currentSession()?.id ?? null,
        isRunning: rustAgent.isRunning(),
      }),
      ({ sessionId, isRunning }) => {
        if (!isTauri() || !sessionId || isRunning) {
          return
        }

        void session.recoverDetachedDesktopSessionIfNeeded?.(sessionId)
      }
    )
  )
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
  const [queuedPendingApprovals, setQueuedPendingApprovals] = createSignal<ApprovalRequest[]>([])
  const [visiblePendingQuestion, setVisiblePendingQuestion] = createSignal<QuestionRequest | null>(
    null
  )
  const [queuedPendingQuestions, setQueuedPendingQuestions] = createSignal<QuestionRequest[]>([])
  const [visiblePendingPlan, setVisiblePendingPlan] = createSignal<PlanData | null>(null)
  const [queuedPendingPlans, setQueuedPendingPlans] = createSignal<PlanData[]>([])
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
  const sessionRuntimeCache = createBoundedSessionCache<CachedSessionRuntime>(
    RECENT_SESSION_RUNTIME_CACHE_LIMIT
  )

  // ── Streaming state (offsets + derived signals) ─────────────────────
  const streamingState = createAgentStreaming(rustAgent)

  // ── Forward agent events into UI signals ─────────────────────────────
  let lastEventCursor = 0
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
  const captureUiRuntimeSnapshot = (): AgentUiRuntimeSnapshot => ({
    isPlanMode: isPlanMode(),
    currentTurn: currentTurn(),
    tokensUsed: tokensUsed(),
    currentThought: currentThought(),
    toolActivity: [...toolActivity()],
    visiblePendingApproval: visiblePendingApproval(),
    queuedPendingApprovals: [...queuedPendingApprovals()],
    visiblePendingQuestion: visiblePendingQuestion(),
    queuedPendingQuestions: [...queuedPendingQuestions()],
    visiblePendingPlan: visiblePendingPlan(),
    queuedPendingPlans: [...queuedPendingPlans()],
    doomLoopDetected: doomLoopDetected(),
    currentAgentId: currentAgentId(),
    streamingTokenEstimate: streamingTokenEstimate(),
    streamingStartedAt: streamingStartedAt(),
    liveMessageId: liveMessageId(),
    streamingContentOffset: streamingState.streamingContentOffset(),
    toolCallsOffset: streamingState.toolCallsOffset(),
    thinkingSegmentsOffset: streamingState.thinkingSegmentsOffset(),
  })
  const hasRestorableUiRuntimeState = (): boolean =>
    currentThought().length > 0 ||
    toolActivity().length > 0 ||
    visiblePendingApproval() !== null ||
    queuedPendingApprovals().length > 0 ||
    visiblePendingQuestion() !== null ||
    queuedPendingQuestions().length > 0 ||
    visiblePendingPlan() !== null ||
    queuedPendingPlans().length > 0 ||
    doomLoopDetected() ||
    currentAgentId() !== null ||
    streamingTokenEstimate() !== 0 ||
    streamingStartedAt() !== null ||
    liveMessageId() !== null ||
    streamingState.streamingContentOffset() !== 0 ||
    streamingState.toolCallsOffset() !== 0 ||
    streamingState.thinkingSegmentsOffset() !== 0
  const hasRestorableRustRuntimeState = (): boolean =>
    rustAgent.isRunning() ||
    rustAgent.currentRunId() !== null ||
    rustAgent.trackedSessionId() !== null ||
    rustAgent.detachedSessionId() !== null ||
    rustAgent.streamingContent().length > 0 ||
    rustAgent.thinkingContent().length > 0 ||
    rustAgent.activeToolCalls().length > 0 ||
    rustAgent.error() !== null ||
    rustAgent.progressMessage() !== null ||
    rustAgent.budgetWarning() !== null ||
    rustAgent.pendingPlan() !== null ||
    rustAgent.thinkingSegments().length > 0 ||
    rustAgent.todos().length > 0
  const restoreUiRuntimeSnapshot = (snapshot: AgentUiRuntimeSnapshot | null): void => {
    if (!snapshot) {
      batch(() => {
        setIsPlanMode(false)
        _setCurrentTurn(0)
        _setTokensUsed(0)
        setCurrentThought('')
        setToolActivity([])
        setVisiblePendingApproval(null)
        setQueuedPendingApprovals([])
        setVisiblePendingQuestion(null)
        setQueuedPendingQuestions([])
        setVisiblePendingPlan(null)
        setQueuedPendingPlans([])
        setDoomLoopDetected(false)
        _setCurrentAgentId(null)
        setStreamingTokenEstimate(0)
        setStreamingStartedAt(null)
        setLiveMessageId(null)
        streamingState.setStreamingContentOffset(0)
        streamingState.setToolCallsOffset(0)
        streamingState.setThinkingSegmentsOffset(0)
      })
      return
    }

    batch(() => {
      setIsPlanMode(snapshot.isPlanMode)
      _setCurrentTurn(snapshot.currentTurn)
      _setTokensUsed(snapshot.tokensUsed)
      setCurrentThought(snapshot.currentThought)
      setToolActivity([...snapshot.toolActivity])
      setVisiblePendingApproval(snapshot.visiblePendingApproval)
      setQueuedPendingApprovals([...snapshot.queuedPendingApprovals])
      setVisiblePendingQuestion(snapshot.visiblePendingQuestion)
      setQueuedPendingQuestions([...snapshot.queuedPendingQuestions])
      setVisiblePendingPlan(snapshot.visiblePendingPlan)
      setQueuedPendingPlans([...snapshot.queuedPendingPlans])
      setDoomLoopDetected(snapshot.doomLoopDetected)
      _setCurrentAgentId(snapshot.currentAgentId)
      setStreamingTokenEstimate(snapshot.streamingTokenEstimate)
      setStreamingStartedAt(snapshot.streamingStartedAt)
      setLiveMessageId(snapshot.liveMessageId)
      streamingState.setStreamingContentOffset(snapshot.streamingContentOffset)
      streamingState.setToolCallsOffset(snapshot.toolCallsOffset)
      streamingState.setThinkingSegmentsOffset(snapshot.thinkingSegmentsOffset)
    })
  }
  const cacheSessionRuntime = (sessionId: string | null | undefined): void => {
    if (!sessionId) {
      return
    }
    if (!hasRestorableUiRuntimeState() && !hasRestorableRustRuntimeState()) {
      sessionRuntimeCache.delete(sessionId)
      return
    }
    const rustSnapshot = rustAgent.captureRuntimeSnapshot({
      maxEvents: MAX_CACHED_RUNTIME_EVENTS,
    })
    sessionRuntimeCache.set(sessionId, {
      rust: rustSnapshot,
      ui: captureUiRuntimeSnapshot(),
    })
  }
  const discardSessionRuntime = (sessionId: string | null | undefined): void => {
    if (!sessionId) {
      return
    }
    sessionRuntimeCache.delete(sessionId)
  }
  const restoreSessionRuntime = (
    sessionId: string | null | undefined,
    cachedRuntime: CachedSessionRuntime | null = sessionId
      ? (sessionRuntimeCache.get(sessionId) ?? null)
      : null
  ): boolean => {
    if (!sessionId) {
      rustAgent.restoreRuntimeSnapshot(null)
      restoreUiRuntimeSnapshot(null)
      lastEventCursor = 0
      return false
    }

    const cached = cachedRuntime
    if (!cached) {
      rustAgent.restoreRuntimeSnapshot(null)
      restoreUiRuntimeSnapshot(null)
      lastEventCursor = 0
      return false
    }

    rustAgent.restoreRuntimeSnapshot(cached.rust)
    restoreUiRuntimeSnapshot(cached.ui)
    lastEventCursor = cached.rust.events.length
    return true
  }
  const approvalRequestFromEvent = (
    approvalEvent: ApprovalRequestEvent | null | undefined
  ): ApprovalRequest | null => {
    if (!approvalEvent) {
      return null
    }

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

    return {
      id: approvalEvent.id,
      toolCallId: approvalEvent.tool_call_id,
      type: toolType,
      toolName,
      args: approvalEvent.args as Record<string, unknown>,
      description: approvalEvent.reason,
      riskLevel,
      resolve: () => {},
    }
  }
  const questionRequestFromEvent = (
    questionEvent: QuestionRequestEvent | null | undefined
  ): QuestionRequest | null => {
    if (!questionEvent) {
      return null
    }
    return {
      id: questionEvent.id,
      question: questionEvent.question,
      options: questionEvent.options,
    }
  }
  const planRequestFromEvent = (
    planEvent: PlanCreatedEvent | null | undefined
  ): PlanData | null => {
    if (!planEvent) {
      return null
    }
    return {
      ...planEvent.plan,
      requestId: planEvent.id,
    }
  }
  const reconcileRuntimeAfterRehydrate = (
    sessionId: string | null,
    rehydrateResult: AgentRehydrateResult,
    restoredCachedRuntime: CachedSessionRuntime | null
  ): void => {
    if (!sessionId) {
      restoreUiRuntimeSnapshot(null)
      lastEventCursor = rustAgent.eventCursor()
      return
    }

    const restoredRunId = restoredCachedRuntime?.rust.currentRunId ?? null
    const authoritativeRunId = rehydrateResult.runId ?? null
    const runStillActive = rehydrateResult.running && authoritativeRunId !== null
    const runChanged = restoredRunId !== null && authoritativeRunId !== restoredRunId

    if (!runStillActive) {
      discardSessionRuntime(sessionId)
      restoreUiRuntimeSnapshot(null)
      lastEventCursor = rustAgent.eventCursor()
      return
    }

    if (runChanged) {
      restoreUiRuntimeSnapshot(null)
    }

    batch(() => {
      setVisiblePendingApproval(approvalRequestFromEvent(rehydrateResult.pendingApproval))
      setQueuedPendingApprovals([])
      setVisiblePendingQuestion(questionRequestFromEvent(rehydrateResult.pendingQuestion))
      setQueuedPendingQuestions([])
      setVisiblePendingPlan(planRequestFromEvent(rehydrateResult.pendingPlan))
      setQueuedPendingPlans([])
    })
    lastEventCursor = rustAgent.eventCursor()
    cacheSessionRuntime(sessionId)
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
      if (activeRunId) {
        log.warn('agent', 'Ignoring uncorrelated interactive event during active run', {
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
    on(rustAgent.eventVersion, () => {
      const { events: newEvents, cursor } = rustAgent.readEventsSince(lastEventCursor)

      for (const event of newEvents) {
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
      lastEventCursor = cursor
    })
  )

  // ── Compose sub-modules ─────────────────────────────────────────────

  const visibleMessageQueue = (): QueuedMessage[] => {
    const currentSessionId = session.currentSession()?.id
    return messageQueue().filter(
      (message) => !message.sessionId || message.sessionId === currentSessionId
    )
  }
  const sessionHasActiveRun = (sessionId?: string | null): boolean =>
    rustAgent.isRunning() && rustAgent.trackedSessionId() === (sessionId ?? null)
  const visibleSessionHasActiveRun = createMemo(() =>
    sessionHasActiveRun(session.currentSession()?.id ?? null)
  )

  const actions = createAgentActions({
    rustAgent,
    session,
    settingsRef,
    sessionHasActiveRun,
    onSessionRuntimeSettled: discardSessionRuntime,
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
    sessionHasActiveRun,
    onSessionRuntimeSettled: discardSessionRuntime,
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

  const visibleIsRunning = (): boolean =>
    visibleSessionHasActiveRun() ? rustAgent.isRunning() : false
  const visibleCurrentRunId = (): string | null =>
    visibleSessionHasActiveRun() ? rustAgent.currentRunId() : null
  const visibleProgressMessage = (): string | null =>
    visibleSessionHasActiveRun() ? rustAgent.progressMessage() : null
  const visibleIsPlanMode = (): boolean => (visibleSessionHasActiveRun() ? isPlanMode() : false)
  const visibleCurrentTurn = (): number => (visibleSessionHasActiveRun() ? currentTurn() : 0)
  const visibleTokensUsed = (): number => (visibleSessionHasActiveRun() ? tokensUsed() : 0)
  const visibleCurrentThought = (): string => (visibleSessionHasActiveRun() ? currentThought() : '')
  const visibleToolActivity = (): ToolActivity[] =>
    visibleSessionHasActiveRun() ? toolActivity() : []
  const visibleScopedPendingApproval = (): ApprovalRequest | null =>
    visibleSessionHasActiveRun() ? pendingApproval() : null
  const visibleScopedPendingQuestion = (): QuestionRequest | null =>
    visibleSessionHasActiveRun() ? pendingQuestion() : null
  const visibleScopedPendingPlan = (): PlanData | null =>
    visibleSessionHasActiveRun() ? pendingPlan() : null
  const visibleDoomLoopDetected = (): boolean =>
    visibleSessionHasActiveRun() ? doomLoopDetected() : false
  const visibleLastError = (): string | null =>
    visibleSessionHasActiveRun() ? rustAgent.error() : null
  const visibleCurrentAgentId = (): string | null =>
    visibleSessionHasActiveRun() ? currentAgentId() : null
  const visibleEventTimeline = (): AgentEvent[] =>
    visibleSessionHasActiveRun() ? rustAgent.events() : []
  const visibleActiveToolCalls = (): ToolCall[] =>
    visibleSessionHasActiveRun() ? streamingState.liveActiveToolCalls() : []
  const visibleStreamingContent = (): string =>
    visibleSessionHasActiveRun() ? streamingState.liveStreamingContent() : ''
  const visibleThinkingSegments = (): ThinkingSegment[] =>
    visibleSessionHasActiveRun() ? streamingState.liveThinkingSegments() : []
  const visibleStreamingTokenEstimate = (): number =>
    visibleSessionHasActiveRun() ? streamingTokenEstimate() : 0
  const visibleStreamingStartedAt = (): number | null =>
    visibleSessionHasActiveRun() ? streamingStartedAt() : null
  const visibleStreamingError = () => (visibleSessionHasActiveRun() ? streamingState.error() : null)
  const visibleLiveMessageId = (): string | null =>
    visibleSessionHasActiveRun() ? liveMessageId() : null

  // ====================================================================
  // Return full public API (identical shape to original)
  // ====================================================================

  return {
    // ── Agent signals (mapped from Rust agent) ───────────────────────
    isRunning: visibleIsRunning,
    currentRunId: visibleCurrentRunId,
    progressMessage: visibleProgressMessage,
    isPlanMode: visibleIsPlanMode,
    currentTurn: visibleCurrentTurn,
    tokensUsed: visibleTokensUsed,
    currentThought: visibleCurrentThought,
    toolActivity: visibleToolActivity,
    pendingApproval: visibleScopedPendingApproval,
    pendingQuestion: visibleScopedPendingQuestion,
    pendingPlan: visibleScopedPendingPlan,
    doomLoopDetected: visibleDoomLoopDetected,
    lastError: visibleLastError,
    currentAgentId: visibleCurrentAgentId,
    eventTimeline: visibleEventTimeline,

    // ── Chat signals (mapped from Rust agent) ────────────────────────
    // These use offset-aware derived signals so that after a steering message
    // splits the response, only the current placeholder's content is shown.
    isStreaming: visibleIsRunning, // alias for backward compat
    activeToolCalls: visibleActiveToolCalls,
    streamingContent: visibleStreamingContent,
    thinkingSegments: visibleThinkingSegments,
    streamingTokenEstimate: visibleStreamingTokenEstimate,
    streamingStartedAt: visibleStreamingStartedAt,
    error: visibleStreamingError,
    messageQueue: visibleMessageQueue,
    queuedCount: () => visibleMessageQueue().length,
    /**
     * ID of the placeholder message that was pre-added to the session store at the
     * start of the current run.  The MessageList uses this to identify which message
     * row is the live streaming bubble so it can pass live signals down to it.
     * Null when no run is in progress.
     */
    liveMessageId: visibleLiveMessageId,

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
