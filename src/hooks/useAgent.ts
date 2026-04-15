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

import { createEffect, createSignal, on } from 'solid-js'

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
  const [pendingApproval, setPendingApproval] = createSignal<ApprovalRequest | null>(null)
  const [pendingQuestion, setPendingQuestion] = createSignal<QuestionRequest | null>(null)
  const [pendingPlan, setPendingPlan] = createSignal<PlanData | null>(null)
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
          log.info('tools', 'Approval requested', {
            tool: (event as ApprovalRequestEvent).tool_name,
            risk: (event as ApprovalRequestEvent).risk_level,
          })
          const approvalEvent = event as ApprovalRequestEvent
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

          setPendingApproval({
            id: approvalEvent.id,
            toolCallId: approvalEvent.tool_call_id ?? approvalEvent.toolCallId,
            type: toolType,
            toolName,
            args: approvalEvent.args as Record<string, unknown>,
            description: approvalEvent.reason,
            riskLevel,
            resolve: () => {}, // not used — resolution goes through IPC
          })
        }
        if (event.type === 'question_request') {
          log.info('agent', 'Question requested', {
            question: (event as QuestionRequestEvent).question?.slice(0, 80),
          })
          const questionEvent = event as QuestionRequestEvent
          setPendingQuestion({
            id: questionEvent.id,
            question: questionEvent.question,
            options: questionEvent.options,
          })
        }
        if (event.type === 'interactive_request_cleared') {
          const clearedEvent = event as InteractiveRequestClearedEvent
          const requestId = clearedEvent.requestId ?? clearedEvent.request_id ?? null
          const requestKind = clearedEvent.requestKind ?? clearedEvent.request_kind

          if (requestKind === 'approval' && pendingApproval()?.id === requestId) {
            setPendingApproval(null)
          }
          if (requestKind === 'question' && pendingQuestion()?.id === requestId) {
            setPendingQuestion(null)
          }
          if (requestKind === 'plan' && pendingPlan()?.requestId === requestId) {
            setPendingPlan(null)
          }
        }
        if (event.type === 'plan_created') {
          const planEvent = event as PlanCreatedEvent
          setPendingPlan({
            ...planEvent.plan,
            requestId: planEvent.id ?? planEvent.plan.requestId,
          })
        }
      }
      lastEventIdx = allEvents.length
    })
  )

  // ── Compose sub-modules ─────────────────────────────────────────────

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
    setPendingApproval,
    pendingQuestion,
    setPendingQuestion,
    pendingPlan,
    setPendingPlan,
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
    messageQueue,
    queuedCount: () => messageQueue().length,
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
