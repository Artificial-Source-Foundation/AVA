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
import { getCoreBudget } from '../services/core-bridge'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import { useTeam } from '../stores/team'
import type {
  ApprovalRequestEvent,
  PlanCreatedEvent,
  PlanData,
  QuestionRequestEvent,
} from '../types/rust-ipc'
import type { AgentState, ApprovalRequest, ToolActivity } from './agent'
import { createTeamBridge } from './agent/agent-team-bridge'
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
  const teamStore = useTeam()

  // ── Team bridge ─────────────────────────────────────────────────────
  const isTeamMode = () => settingsRef.settings().generation.delegationEnabled
  const teamBridge = createTeamBridge(teamStore, isTeamMode)

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

  // ── Forward agent events to team bridge + handle UI events ──────────
  let lastEventIdx = 0

  /**
   * Map Praxis IPC events (from Rust) to the team bridge's AgentEvent format.
   * Returns null for events that have no team bridge mapping.
   */
  function mapPraxisToTeamEvent(
    event: import('../types/rust-ipc').AgentEvent
  ): import('./agent/agent-events').AgentEvent | null {
    switch (event.type) {
      case 'praxis_worker_started':
        return {
          type: 'delegation:start',
          agentId: 'praxis-director',
          childAgentId: event.worker_id,
          workerName: event.lead,
          task: event.task,
          tier: 'worker',
        }
      case 'praxis_worker_completed':
        return {
          type: 'delegation:complete',
          agentId: 'praxis-director',
          childAgentId: event.worker_id,
          workerName: '',
          success: event.success,
          output: `Completed in ${event.turns} turns`,
          durationMs: 0,
        }
      case 'praxis_worker_failed':
        return {
          type: 'delegation:complete',
          agentId: 'praxis-director',
          childAgentId: event.worker_id,
          workerName: '',
          success: false,
          output: event.error,
          durationMs: 0,
        }
      case 'praxis_worker_token':
        return {
          type: 'thought',
          agentId: event.worker_id,
          content: event.token,
        }
      case 'praxis_worker_progress':
        return {
          type: 'turn:start',
          agentId: event.worker_id,
          turn: event.turn,
        }
      case 'praxis_all_complete':
        return {
          type: 'agent:finish',
          agentId: 'praxis-director',
          result: {
            success: event.failed === 0,
            turns: 0,
            tokensUsed: { input: 0, output: 0 },
            output: `${event.succeeded}/${event.total_workers} workers succeeded`,
          },
        }
      default:
        return null
    }
  }

  createEffect(
    on(rustAgent.events, (allEvents) => {
      // Reset cursor when events array is cleared (new run started)
      if (allEvents.length < lastEventIdx) {
        lastEventIdx = 0
      }
      for (let i = lastEventIdx; i < allEvents.length; i++) {
        const event = allEvents[i]!

        // ── Team bridge: forward Praxis events ─────────────────────
        const praxisMapped = mapPraxisToTeamEvent(event)
        if (praxisMapped) {
          // Ensure the director agent exists before forwarding child events
          if (
            praxisMapped.type === 'delegation:start' &&
            !teamStore.teamMembers().has('praxis-director')
          ) {
            teamBridge.bridgeToTeam({
              type: 'agent:start',
              agentId: 'praxis-director',
              goal: 'Multi-agent coordination',
            })
          }
          teamBridge.bridgeToTeam(praxisMapped)
        } else {
          teamBridge.bridgeToTeam(event as import('./agent/agent-events').AgentEvent)
        }

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
        if (event.type === 'plan_created') {
          const planEvent = event as PlanCreatedEvent
          setPendingPlan(planEvent.plan)
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
  })

  const runModule = createAgentRun({
    rustAgent,
    session,
    settingsRef,
    isTeamMode,
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
    stopAgent: (memberId: string) => teamBridge.stopAgent(memberId),
    sendTeamMessage: (memberId: string, message: string) =>
      teamBridge.sendMessage(memberId, message),
  }
}
