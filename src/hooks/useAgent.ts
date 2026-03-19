/**
 * useAgent Hook — Unified Agent + Chat (Orchestrator)
 *
 * Single hook that drives ALL agent interactions in the desktop app.
 * Delegates to the Rust backend via useRustAgent() for all execution.
 * The TypeScript layer only manages UI state (approval bridge, plan mode, queuing).
 */

import { batch, createEffect, createSignal, on } from 'solid-js'

import { DEFAULTS } from '../config/constants'
import { generateMessageId } from '../lib/ids'
import { log } from '../lib/logger'
import { deriveSessionTitle } from '../lib/title-utils'
import { checkAutoApproval as sharedCheckAutoApproval } from '../lib/tool-approval'
import { getCoreBudget } from '../services/core-bridge'
import { rustAgent as rustAgentBridge, rustBackend } from '../services/rust-bridge'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import { useTeam } from '../stores/team'
import type { Message } from '../types'
import type { StreamError } from '../types/llm'
import type {
  ApprovalRequestEvent,
  PlanCreatedEvent,
  PlanData,
  PlanResponse,
  QuestionRequestEvent,
} from '../types/rust-ipc'
import type { AgentState, ApprovalRequest, ToolActivity } from './agent'
import { createTeamBridge } from './agent/agent-team-bridge'
import type { QueuedMessage } from './chat/types'
import { useRustAgent } from './use-rust-agent'

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

  // Map Rust agent error signal to StreamError shape
  const error = (): StreamError | null => {
    const msg = rustAgent.error()
    return msg ? { type: 'unknown', message: msg } : null
  }

  // ── Forward agent events to team bridge ──────────────────────────────
  let lastTeamBridgeIdx = 0

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
      if (allEvents.length < lastTeamBridgeIdx) {
        lastTeamBridgeIdx = 0
      }
      for (let i = lastTeamBridgeIdx; i < allEvents.length; i++) {
        const event = allEvents[i]!
        // Try to map Praxis events to team bridge events
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
      }
      lastTeamBridgeIdx = allEvents.length
    })
  )

  // ── Watch for approval_request / question_request events from Rust ──
  let lastProcessedEventIdx = 0

  createEffect(
    on(rustAgent.events, (allEvents) => {
      // Reset index when events array is cleared (new run started)
      if (allEvents.length < lastProcessedEventIdx) {
        lastProcessedEventIdx = 0
      }
      for (let i = lastProcessedEventIdx; i < allEvents.length; i++) {
        const event = allEvents[i]!
        if (event.type === 'tool_call') {
          log.debug('agent', 'Tool called', { tool: (event as { name?: string }).name })
        }
        // Sync thinking content to frontend signal
        if (event.type === 'thinking') {
          setCurrentThought((prev) => prev + (event as { content: string }).content)
        }
        // Sync real token counts to the ContextBudget on each turn
        if (event.type === 'token_usage') {
          // Rust serializes as snake_case (input_tokens, output_tokens)
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
      lastProcessedEventIdx = allEvents.length
    })
  )

  // ====================================================================
  // Actions
  // ====================================================================

  async function run(
    goal: string,
    config?: { model?: string; provider?: string }
  ): Promise<unknown> {
    if (rustAgent.isRunning()) {
      setMessageQueue((prev) => [...prev, { content: goal }])
      return null
    }

    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
    })

    // Ensure a session exists before adding messages
    let currentSess = session.currentSession()
    if (!currentSess) {
      await session.createNewSession()
      currentSess = session.currentSession()
    }
    const sessionId = currentSess?.id ?? ''

    // Add user message to the session store so it's visible immediately
    const userMsg: Message = {
      id: generateMessageId('user'),
      sessionId,
      role: 'user',
      content: goal,
      createdAt: Date.now(),
    }
    session.addMessage(userMsg)

    // Auto-title the session from the first user message
    // (runs before the agent call so the sidebar updates immediately)
    if (settingsRef.settings().behavior.sessionAutoTitle && currentSess) {
      const isDefaultName = currentSess.name === DEFAULTS.SESSION_NAME
      const isFirstMessage = session.messages().length <= 1
      if (isDefaultName && isFirstMessage) {
        const title = deriveSessionTitle(goal)
        if (title) {
          void session.renameSession(sessionId, title).catch((err) => {
            log.warn('agent', 'Failed to auto-title session', { error: String(err) })
          })
        }
      }
    }

    // Feed the context budget so the status bar updates
    const budget = getCoreBudget()
    if (budget) {
      budget.addMessage(userMsg.id, userMsg.content)
      window.dispatchEvent(
        new CustomEvent('ava:core-settings-changed', { detail: { category: 'context' } })
      )
    }

    try {
      log.info('agent', 'Agent started', { goal: goal.slice(0, 120), sessionId })
      // Resolve the model/provider from the frontend's selection
      const selectedModelId = config?.model || session.selectedModel()
      const selectedProviderId = config?.provider || session.selectedProvider() || undefined
      const runStartedAt = Date.now()
      // Get the thinking/reasoning level from frontend settings
      const reasoningEffort = settingsRef.settings().generation.reasoningEffort
      const thinkingLevel = reasoningEffort === 'off' ? undefined : reasoningEffort

      // Team mode: route to Praxis Director instead of solo agent
      if (isTeamMode()) {
        log.info('agent', 'Team mode — routing to Praxis Director', {
          goal: goal.slice(0, 120),
        })
        try {
          const teamCfg = settingsRef.settings().team
          const teamConfigPayload: import('../types/rust-ipc').TeamConfigPayload = {
            defaultDirectorModel: teamCfg.defaultDirectorModel,
            defaultLeadModel: teamCfg.defaultLeadModel,
            defaultWorkerModel: teamCfg.defaultWorkerModel,
            defaultScoutModel: teamCfg.defaultScoutModel,
            workerNames: teamCfg.workerNames,
            leads: teamCfg.leads.map((l) => ({
              domain: l.domain,
              enabled: l.enabled,
              model: l.model,
              maxWorkers: l.maxWorkers,
            })),
          }
          await rustBackend.startPraxis(goal, undefined, teamConfigPayload)
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err)
          log.error('agent', 'Praxis failed', { error: msg })
          const errorMsg: Message = {
            id: generateMessageId('err'),
            sessionId,
            role: 'assistant',
            content: `**Team Error:** ${msg}`,
            createdAt: Date.now(),
            error: { type: 'unknown', message: msg, timestamp: Date.now() },
          }
          session.addMessage(errorMsg)
        }
        return null
      }

      const result = await rustAgent.run(goal, {
        model: selectedModelId,
        provider: selectedProviderId,
        thinkingLevel,
        sessionId,
      })
      const errorText = rustAgent.error()

      // Check if the agent errored (rustAgent.run catches internally, returns null)
      if (errorText) {
        log.error('agent', 'Agent failed', { error: errorText })
        const errorMsg: Message = {
          id: generateMessageId('err'),
          sessionId,
          role: 'assistant',
          content: `**Error:** ${errorText}`,
          createdAt: Date.now(),
          error: { type: 'unknown', message: errorText, timestamp: Date.now() },
        }
        session.addMessage(errorMsg)
        return null
      }

      // Add the assistant response from streamed tokens
      const content = rustAgent.streamingContent()
      if (content) {
        const elapsedMs = Date.now() - runStartedAt
        const thinking = rustAgent.thinkingContent()
        const assistantMsg: Message = {
          id: generateMessageId('asst'),
          sessionId,
          role: 'assistant',
          content,
          createdAt: Date.now(),
          tokensUsed: rustAgent.tokenUsage().output,
          // Hide cost for subscription providers (OAuth) — Rust returns 0.0 for these,
          // but use undefined so the UI hides the field entirely
          costUSD: rustAgent.tokenUsage().cost || undefined,
          model: selectedModelId,
          toolCalls: rustAgent.activeToolCalls(),
          metadata: {
            provider: selectedProviderId,
            model: selectedModelId,
            mode: isPlanMode() ? 'plan' : 'code',
            elapsedMs,
            ...(thinking ? { thinking } : {}),
          },
        }
        session.addMessage(assistantMsg)
      }

      log.info('agent', 'Agent completed', {
        tokens: rustAgent.tokenUsage().output,
        cost: rustAgent.tokenUsage().cost,
      })
      return result
    } catch (err) {
      // Unexpected error (not from rustAgent internals)
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Unexpected agent error', { error: msg })
      const errorMsg: Message = {
        id: generateMessageId('err'),
        sessionId,
        role: 'assistant',
        content: `**Error:** ${msg}`,
        createdAt: Date.now(),
        error: { type: 'unknown', message: msg, timestamp: Date.now() },
      }
      session.addMessage(errorMsg)
      return null
    } finally {
      batch(() => {
        setStreamingStartedAt(null)
      })
      // Process queue
      const queue = messageQueue()
      if (queue.length > 0) {
        const next = queue[0]!
        setMessageQueue((prev) => prev.slice(1))
        void run(next.content)
      }
    }
  }

  function cancel(): void {
    void rustAgent.cancel()
    batch(() => {
      setMessageQueue([])
      setStreamingStartedAt(null)
    })
  }

  function steer(content: string): void {
    void rustAgent.steer(content)
  }

  function followUp(content: string): void {
    // Only add to local queue if the backend accepts the message
    void rustAgent.followUp(content).then(
      () => setMessageQueue((prev) => [...prev, { content, tier: 'follow-up' }]),
      () => {
        // Backend rejected (agent not running or channel closed) — don't queue locally
        log.warn('agent', 'Follow-up rejected by backend', { content: content.slice(0, 80) })
      }
    )
  }

  function postComplete(content: string, group?: number): void {
    // Only add to local queue if the backend accepts the message
    void rustAgent.postComplete(content, group).then(
      () =>
        setMessageQueue((prev) => [...prev, { content, tier: 'post-complete', group: group ?? 1 }]),
      () => {
        // Backend rejected (agent not running or channel closed) — don't queue locally
        log.warn('agent', 'Post-complete rejected by backend', { content: content.slice(0, 80) })
      }
    )
  }

  function togglePlanMode(): void {
    setIsPlanMode((prev) => !prev)
  }

  function checkAutoApproval(
    toolName: string,
    args: Record<string, unknown>
  ): { approved: boolean; reason?: string } {
    return sharedCheckAutoApproval(toolName, args, settingsRef.isToolAutoApproved)
  }

  function resolveApproval(approved: boolean, alwaysAllow?: boolean): void {
    setPendingApproval(null)
    void rustAgentBridge.resolveApproval(approved, alwaysAllow ?? false).catch((err) => {
      console.error('Failed to resolve approval:', err)
    })
  }

  function resolveQuestion(answer: string): void {
    setPendingQuestion(null)
    void rustAgentBridge.resolveQuestion(answer).catch((err) => {
      console.error('Failed to resolve question:', err)
    })
  }

  function resolvePlan(
    response: PlanResponse,
    modifiedPlan?: PlanData,
    feedback?: string,
    stepComments?: Record<string, string>
  ): void {
    setPendingPlan(null)
    void rustAgentBridge
      .resolvePlan(response, modifiedPlan ?? null, feedback ?? null, stepComments ?? null)
      .catch((err) => {
        console.error('Failed to resolve plan:', err)
      })
  }

  function clearError(): void {
    batch(() => {
      rustAgent.clearError()
    })
  }

  function getState(): AgentState {
    return {
      isRunning: rustAgent.isRunning(),
      isPlanMode: isPlanMode(),
      currentTurn: currentTurn(),
      tokensUsed: tokensUsed(),
      currentThought: currentThought(),
      toolActivity: toolActivity(),
      pendingApproval: pendingApproval(),
      doomLoopDetected: doomLoopDetected(),
      lastError: rustAgent.error(),
    }
  }

  function removeFromQueue(index: number): void {
    setMessageQueue((prev) => prev.filter((_, i) => i !== index))
  }

  function clearQueue(): void {
    setMessageQueue([])
  }

  // Message actions — wired through Rust IPC
  async function retryMessage(_assistantMessageId: string): Promise<void> {
    if (rustAgent.isRunning()) return
    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
    })
    try {
      await rustBackend.retryLastMessage()
    } finally {
      setStreamingStartedAt(null)
    }
  }

  async function editAndResend(messageId: string, newContent: string): Promise<void> {
    if (rustAgent.isRunning()) return
    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
    })
    try {
      await rustBackend.editAndResend({ messageId, newContent })
    } finally {
      setStreamingStartedAt(null)
    }
  }

  async function regenerateResponse(_assistantMessageId: string): Promise<void> {
    if (rustAgent.isRunning()) return
    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
    })
    try {
      await rustBackend.regenerateResponse()
    } finally {
      setStreamingStartedAt(null)
    }
  }

  async function undoLastEdit(): Promise<{ success: boolean; message: string }> {
    const result = await rustBackend.undoLastEdit()
    return { success: result.success, message: result.message }
  }

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
    isStreaming: rustAgent.isRunning, // alias for backward compat
    activeToolCalls: rustAgent.activeToolCalls,
    streamingContent: rustAgent.streamingContent,
    streamingTokenEstimate,
    streamingStartedAt,
    error,
    messageQueue,
    queuedCount: () => messageQueue().length,

    // ── Actions ──────────────────────────────────────────────────────
    run,
    cancel,
    steer,
    followUp,
    postComplete,
    retryMessage,
    editAndResend,
    regenerateResponse,
    undoLastEdit,

    // ── Queue ────────────────────────────────────────────────────────
    removeFromQueue,
    clearQueue,

    // ── Agent-specific ──────────────────────────────────────────────
    togglePlanMode,
    checkAutoApproval,
    resolveApproval,
    resolveQuestion,
    resolvePlan,
    clearError,
    getState,
    stopAgent: (memberId: string) => teamBridge.stopAgent(memberId),
    sendTeamMessage: (memberId: string, message: string) =>
      teamBridge.sendMessage(memberId, message),
  }
}
