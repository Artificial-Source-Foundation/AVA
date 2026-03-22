/**
 * useAgent Hook — Unified Agent + Chat (Orchestrator)
 *
 * Single hook that drives ALL agent interactions in the desktop app.
 * Delegates to the Rust backend via useRustAgent() for all execution.
 * The TypeScript layer only manages UI state (approval bridge, plan mode, queuing).
 */

import { isTauri } from '@tauri-apps/api/core'
import { batch, createEffect, createMemo, createSignal, on } from 'solid-js'

import { DEFAULTS } from '../config/constants'
import { debugLog } from '../lib/debug-log'
import { generateMessageId } from '../lib/ids'
import { log } from '../lib/logger'
import { deriveSessionTitle } from '../lib/title-utils'
import { checkAutoApproval as sharedCheckAutoApproval } from '../lib/tool-approval'
import { getCoreBudget } from '../services/core-bridge'
import { registerBackendSessionId } from '../services/db-web-fallback'
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
  /**
   * ID of the placeholder assistant message that is added to the session store at
   * the START of each run, before any tokens arrive.  The same DOM node stays alive
   * throughout streaming (no flash on unmount) and is filled-in / replaced with the
   * final metadata when streaming ends.  Null when no run is in progress.
   */
  const [liveMessageId, setLiveMessageId] = createSignal<string | null>(null)

  /**
   * Offset into streamingContent() at which the current live placeholder begins.
   * When steering splits the response, the first segment's content is frozen into the
   * original placeholder and this offset tracks where the new placeholder's content starts.
   */
  const [streamingContentOffset, setStreamingContentOffset] = createSignal(0)
  const [toolCallsOffset, setToolCallsOffset] = createSignal(0)
  const [thinkingSegmentsOffset, setThinkingSegmentsOffset] = createSignal(0)

  // Derived signals that apply steering offsets — these show only the content
  // relevant to the CURRENT live placeholder, not the entire run's accumulation.
  const liveStreamingContent = createMemo(() =>
    rustAgent.streamingContent().slice(streamingContentOffset())
  )
  const liveActiveToolCalls = createMemo(() => rustAgent.activeToolCalls().slice(toolCallsOffset()))
  const liveThinkingSegments = createMemo(() =>
    rustAgent.thinkingSegments().slice(thinkingSegmentsOffset())
  )

  // Map Rust agent error signal to StreamError shape
  const error = (): StreamError | null => {
    const msg = rustAgent.error()
    return msg ? { type: 'unknown', message: msg } : null
  }

  // ── Forward agent events to team bridge + handle UI events ──────────
  // Single cursor shared across both concerns — avoids double-iteration of the
  // events signal and prevents the subtle bug where two independent cursors could
  // diverge when the events array is cleared between runs.
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
      // Reset steering offsets for new run
      setStreamingContentOffset(0)
      setToolCallsOffset(0)
      setThinkingSegmentsOffset(0)
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

    // Resolve model/provider now so we can embed it in the placeholder message.
    const selectedModelId = config?.model || session.selectedModel()
    const selectedProviderId = config?.provider || session.selectedProvider() || undefined

    // ── Pre-add assistant placeholder ──────────────────────────────────
    // Add an empty assistant message to the store BEFORE streaming begins so the
    // <For> list has a stable DOM node that persists through the entire stream.
    // When streaming ends we UPDATE this same message (same ID → same DOM node),
    // which means no unmount/remount and therefore no visible flash.
    const assistantMsgId = generateMessageId('asst')
    const placeholderMsg: Message = {
      id: assistantMsgId,
      sessionId,
      role: 'assistant',
      content: '',
      createdAt: Date.now(),
      model: selectedModelId,
    }
    session.addMessage(placeholderMsg)
    setLiveMessageId(assistantMsgId)

    try {
      log.info('agent', 'Run started', { goal: goal.slice(0, 120), sessionId })
      const runStartedAt = Date.now()
      // Get the thinking/reasoning level from frontend settings
      const reasoningEffort = settingsRef.settings().generation.reasoningEffort
      const thinkingLevel = reasoningEffort === 'off' ? undefined : reasoningEffort
      debugLog('agent', 'run config:', {
        model: selectedModelId,
        provider: selectedProviderId,
        thinkingLevel,
        reasoningEffort,
      })

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
          // Reuse the placeholder slot for the error
          session.updateMessage(assistantMsgId, {
            content: '',
            error: { type: 'unknown', message: msg, timestamp: Date.now() },
          })
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
        const isCancelled =
          errorText === 'Agent run cancelled by user' || errorText.includes('cancelled by user')

        if (isCancelled) {
          log.info('agent', 'Run cancelled by user — preserving partial response')
          // Preserve any streaming content received before cancellation.
          // Use the current live message ID (may differ from assistantMsgId after steering).
          const cancelMsgId = liveMessageId() || assistantMsgId
          const fullPartial = rustAgent.streamingContent()
          const cOffset = streamingContentOffset()
          const partialContent = cOffset > 0 ? fullPartial.slice(cOffset) : fullPartial
          const partialThinking = rustAgent.thinkingContent()
          const elapsedMs = Date.now() - runStartedAt
          if (partialContent || partialThinking) {
            const allSegments = rustAgent.thinkingSegments()
            const sOffset = thinkingSegmentsOffset()
            const partialSegments = sOffset > 0 ? allSegments.slice(sOffset) : allSegments
            const allTc = rustAgent.activeToolCalls()
            const tOffset = toolCallsOffset()
            const partialToolCalls = tOffset > 0 ? allTc.slice(tOffset) : allTc
            // Fill in the placeholder with the partial content
            session.updateMessage(cancelMsgId, {
              content: partialContent,
              tokensUsed: rustAgent.tokenUsage().output,
              costUSD: rustAgent.tokenUsage().cost || undefined,
              toolCalls: partialToolCalls,
              metadata: {
                provider: selectedProviderId,
                model: selectedModelId,
                mode: isPlanMode() ? 'plan' : 'code',
                elapsedMs,
                cancelled: true,
                ...(partialThinking ? { thinking: partialThinking } : {}),
                ...(partialSegments.length > 1 ? { thinkingSegments: partialSegments } : {}),
              },
            })
          } else {
            // Nothing to show — remove the empty placeholder
            session.deleteMessage(cancelMsgId)
          }
          // Add a subtle system-level cancellation note
          const cancelNote: Message = {
            id: generateMessageId('sys'),
            sessionId,
            role: 'assistant',
            content: '',
            createdAt: Date.now(),
            metadata: { cancelled: true, system: true },
            error: { type: 'cancelled', message: 'Session interrupted', timestamp: Date.now() },
          }
          session.addMessage(cancelNote)
          return null
        }

        log.error('agent', 'Run failed', { error: errorText })
        // Fill the placeholder with just an error (no content body)
        const errorMsgId = liveMessageId() || assistantMsgId
        session.updateMessage(errorMsgId, {
          content: '',
          error: { type: 'unknown', message: errorText, timestamp: Date.now() },
        })
        return null
      }

      // Settle the assistant response into the placeholder.
      // If steering happened, liveMessageId will differ from assistantMsgId —
      // the original was already finalized and a new placeholder was created.
      const finalMsgId = liveMessageId() || assistantMsgId
      const fullContent = rustAgent.streamingContent()
      const contentOffset = streamingContentOffset()
      const content = contentOffset > 0 ? fullContent.slice(contentOffset) : fullContent
      const elapsedMs = Date.now() - runStartedAt
      const thinking = rustAgent.thinkingContent()
      const allSegments = rustAgent.thinkingSegments()
      const tsOffset = thinkingSegmentsOffset()
      const segments = tsOffset > 0 ? allSegments.slice(tsOffset) : allSegments
      const allToolCalls = rustAgent.activeToolCalls()
      const tcOffset = toolCallsOffset()
      const toolCalls = tcOffset > 0 ? allToolCalls.slice(tcOffset) : allToolCalls
      debugLog(
        'thinking',
        'message metadata:',
        thinking ? `yes (${thinking.length} chars)` : 'no',
        segments.length > 0 ? `${segments.length} segments` : ''
      )

      if (content) {
        // Update the stable placeholder in-place — same ID → same DOM node → no flash
        session.updateMessage(finalMsgId, {
          content,
          tokensUsed: rustAgent.tokenUsage().output,
          // Hide cost for subscription providers (OAuth) — Rust returns 0.0 for these,
          // but use undefined so the UI hides the field entirely
          costUSD: rustAgent.tokenUsage().cost || undefined,
          toolCalls,
          metadata: {
            provider: selectedProviderId,
            model: selectedModelId,
            mode: isPlanMode() ? 'plan' : 'code',
            elapsedMs,
            ...(thinking ? { thinking } : {}),
            // Store interleaved segments for post-completion rendering
            ...(segments.length > 1 ? { thinkingSegments: segments } : {}),
          },
        })
      } else {
        // Agent produced no text output — remove the empty placeholder so it doesn't
        // linger as a blank bubble (tool-only responses, errors, etc.)
        session.deleteMessage(finalMsgId)
      }

      log.info('agent', 'Run completed', {
        success: true,
        tokens: rustAgent.tokenUsage().output,
        cost: rustAgent.tokenUsage().cost,
        toolCalls: rustAgent.activeToolCalls().length,
        contentLength: content?.length ?? 0,
      })

      // In web mode the Rust agent is the single writer for session persistence.
      // After the run completes, fetch the authoritative message list from the
      // backend and replace the in-memory store so the UI reflects what was
      // actually saved (correct tool_calls, metadata, tokens, etc.).
      // Use the backend's session ID (from the run result) — it may differ from the frontend's
      const backendSessionId = result?.sessionId || sessionId
      if (!isTauri() && backendSessionId) {
        try {
          const apiBase = (import.meta.env.VITE_API_URL as string) || ''
          const res = await fetch(`${apiBase}/api/sessions/${backendSessionId}/messages`)
          if (res.ok) {
            const rawMsgs = (await res.json()) as Array<Record<string, unknown>>
            const backendMsgs: Message[] = rawMsgs.map((m) => {
              const metaRaw = m.metadata
              const metadata =
                typeof metaRaw === 'string'
                  ? (JSON.parse(metaRaw) as Record<string, unknown>)
                  : (metaRaw as Record<string, unknown> | undefined)
              return {
                id: m.id as string,
                sessionId: backendSessionId,
                role: m.role as Message['role'],
                content: (m.content as string) ?? '',
                createdAt:
                  typeof m.created_at === 'number'
                    ? m.created_at
                    : typeof m.timestamp === 'string'
                      ? new Date(m.timestamp).getTime()
                      : Date.now(),
                tokensUsed: (m.tokens_used as number) || undefined,
                costUSD: (m.cost_usd as number | null) ?? undefined,
                model: (m.model as string | null) ?? undefined,
                metadata,
                toolCalls: (metadata?.toolCalls as Message['toolCalls']) ?? undefined,
              }
            })
            session.replaceMessagesFromBackend(backendMsgs)
            // Register the ID mapping so session switching loads from the right backend session
            registerBackendSessionId(sessionId, backendSessionId)
            log.info('agent', 'Store synced from backend', {
              count: backendMsgs.length,
              backendSessionId,
            })
          }
        } catch (syncErr) {
          log.warn('agent', 'Failed to sync messages from backend', { error: String(syncErr) })
        }
      }

      return result
    } catch (err) {
      // Unexpected error (not from rustAgent internals)
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Unexpected agent error', { error: msg })
      session.updateMessage(assistantMsgId, {
        content: `**Error:** ${msg}`,
        error: { type: 'unknown', message: msg, timestamp: Date.now() },
      })
      return null
    } finally {
      batch(() => {
        setStreamingStartedAt(null)
        setLiveMessageId(null)
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
    log.info('agent', 'Cancel requested by user')
    void rustAgent.cancel()
    batch(() => {
      setMessageQueue([])
      setStreamingStartedAt(null)
    })
  }

  function steer(content: string): void {
    // Snapshot the current streaming state into the existing assistant placeholder
    // so it becomes a finalized message. Then create a new placeholder for post-
    // steering content. This makes steering messages appear inline:
    //   [Assistant response up to steer] → [User: steering msg] → [New assistant response]
    const currentLiveId = liveMessageId()
    if (currentLiveId) {
      const currentContent = rustAgent.streamingContent()
      const currentToolCalls = rustAgent.activeToolCalls()
      const currentThinking = rustAgent.thinkingContent()
      const currentSegments = rustAgent.thinkingSegments()
      const contentOffset = streamingContentOffset()
      const tcOffset = toolCallsOffset()
      const tsOffset = thinkingSegmentsOffset()

      // Finalize the current placeholder with content accumulated since last offset
      const slicedContent = currentContent.slice(contentOffset)
      const slicedToolCalls = currentToolCalls.slice(tcOffset)
      const slicedThinking = currentThinking // thinking is harder to slice, keep full
      const slicedSegments = currentSegments.slice(tsOffset)

      session.updateMessage(currentLiveId, {
        content: slicedContent,
        toolCalls: slicedToolCalls.length > 0 ? slicedToolCalls : undefined,
        metadata: {
          ...(slicedThinking ? { thinking: slicedThinking } : {}),
          ...(slicedSegments.length > 1 ? { thinkingSegments: slicedSegments } : {}),
        },
      })

      // Update offsets for the new placeholder
      const newContentOffset = currentContent.length
      const newTcOffset = currentToolCalls.length
      const newTsOffset = currentSegments.length

      // Create new assistant placeholder AFTER the steering message
      // (the steering user message was already added by the caller in use-input-state.ts)
      const sessionId = session.currentSession()?.id ?? ''
      const newAssistantId = generateMessageId('asst')
      const newPlaceholder: Message = {
        id: newAssistantId,
        sessionId,
        role: 'assistant',
        content: '',
        createdAt: Date.now(),
        model: session.selectedModel(),
      }
      session.addMessage(newPlaceholder)

      batch(() => {
        setLiveMessageId(newAssistantId)
        setStreamingContentOffset(newContentOffset)
        setToolCallsOffset(newTcOffset)
        setThinkingSegmentsOffset(newTsOffset)
      })
    }

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
    const next = !isPlanMode()
    log.info('agent', 'Plan mode toggled', { planMode: next })
    setIsPlanMode(next)
  }

  function checkAutoApproval(
    toolName: string,
    args: Record<string, unknown>
  ): { approved: boolean; reason?: string } {
    return sharedCheckAutoApproval(toolName, args, settingsRef.isToolAutoApproved)
  }

  function resolveApproval(approved: boolean, alwaysAllow?: boolean): void {
    log.info('tools', 'Approval resolved', { approved, alwaysAllow: alwaysAllow ?? false })
    // Tag the tool call with the audit decision before clearing pending approval
    const current = pendingApproval()
    if (current) {
      const decision: 'once' | 'always' | 'denied' = !approved
        ? 'denied'
        : alwaysAllow
          ? 'always'
          : 'once'
      rustAgent.markToolApproval(current.toolName, decision)
    }
    setPendingApproval(null)
    void rustAgentBridge.resolveApproval(approved, alwaysAllow ?? false).catch((err) => {
      log.error('error', 'Failed to resolve approval', { error: String(err) })
    })
  }

  function resolveQuestion(answer: string): void {
    log.info('agent', 'Question resolved', { answerLength: answer.length })
    setPendingQuestion(null)
    void rustAgentBridge.resolveQuestion(answer).catch((err) => {
      log.error('error', 'Failed to resolve question', { error: String(err) })
    })
  }

  function resolvePlan(
    response: PlanResponse,
    modifiedPlan?: PlanData,
    feedback?: string,
    stepComments?: Record<string, string>
  ): void {
    log.info('agent', 'Plan resolved', { response, hasFeedback: !!feedback })
    setPendingPlan(null)
    void rustAgentBridge
      .resolvePlan(response, modifiedPlan ?? null, feedback ?? null, stepComments ?? null)
      .catch((err) => {
        log.error('error', 'Failed to resolve plan', { error: String(err) })
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
    log.info('agent', 'Retrying last message')
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
    log.info('agent', 'Edit and resend', { messageId, contentLength: newContent.length })

    // ── 1. Clean up in-memory messages (immediate UI update) ──────────
    batch(() => {
      // Remove all messages after the edited one, then remove the edited
      // message itself so the UI immediately reflects the truncation.
      session.deleteMessagesAfter(messageId)
      session.deleteMessage(messageId)
      // Exit editing mode
      session.stopEditing()
    })

    // ── 2. Reset agent UI state ──────────────────────────────────────
    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
      // Reset steering offsets for new run
      setStreamingContentOffset(0)
      setToolCallsOffset(0)
      setThinkingSegmentsOffset(0)
    })

    // Ensure a session exists
    let currentSess = session.currentSession()
    if (!currentSess) {
      await session.createNewSession()
      currentSess = session.currentSession()
    }
    const sessionId = currentSess?.id ?? ''

    // ── 3. Add user message + assistant placeholder ──────────────────
    const userMsg: Message = {
      id: generateMessageId('user'),
      sessionId,
      role: 'user',
      content: newContent,
      createdAt: Date.now(),
    }
    session.addMessage(userMsg)

    const budget = getCoreBudget()
    if (budget) {
      budget.addMessage(userMsg.id, userMsg.content)
      window.dispatchEvent(
        new CustomEvent('ava:core-settings-changed', { detail: { category: 'context' } })
      )
    }

    const selectedModelId = session.selectedModel()
    const selectedProviderId = session.selectedProvider() || undefined

    const assistantMsgId = generateMessageId('asst')
    const placeholderMsg: Message = {
      id: assistantMsgId,
      sessionId,
      role: 'assistant',
      content: '',
      createdAt: Date.now(),
      model: selectedModelId,
    }
    session.addMessage(placeholderMsg)
    setLiveMessageId(assistantMsgId)

    // ── 4. Call the backend's edit-resend API ─────────────────────────
    // This tells the backend to truncate the session at the edited message
    // and start a fresh agent run, instead of submit_goal which would load
    // the full (un-truncated) session history.
    try {
      const runStartedAt = Date.now()
      const result = await rustAgent.editAndResendRun(messageId, newContent)
      const errorText = rustAgent.error()

      if (errorText) {
        const isCancelled =
          errorText === 'Agent run cancelled by user' || errorText.includes('cancelled by user')
        if (isCancelled) {
          const partialContent = rustAgent.streamingContent()
          const elapsedMs = Date.now() - runStartedAt
          if (partialContent) {
            session.updateMessage(assistantMsgId, {
              content: partialContent,
              metadata: {
                provider: selectedProviderId,
                model: selectedModelId,
                elapsedMs,
                cancelled: true,
              },
            })
          } else {
            session.deleteMessage(assistantMsgId)
          }
          return
        }
        session.updateMessage(assistantMsgId, {
          content: '',
          error: { type: 'unknown', message: errorText, timestamp: Date.now() },
        })
        return
      }

      // Settle the assistant response
      const content = rustAgent.streamingContent()
      const elapsedMs = Date.now() - runStartedAt
      const thinking = rustAgent.thinkingContent()
      const segments = rustAgent.thinkingSegments()

      if (content) {
        session.updateMessage(assistantMsgId, {
          content,
          tokensUsed: rustAgent.tokenUsage().output,
          costUSD: rustAgent.tokenUsage().cost || undefined,
          toolCalls: rustAgent.activeToolCalls(),
          metadata: {
            provider: selectedProviderId,
            model: selectedModelId,
            mode: isPlanMode() ? 'plan' : 'code',
            elapsedMs,
            ...(thinking ? { thinking } : {}),
            ...(segments.length > 1 ? { thinkingSegments: segments } : {}),
          },
        })
      } else {
        session.deleteMessage(assistantMsgId)
      }

      // Sync from backend in web mode
      const backendSessionId = result?.sessionId || sessionId
      if (!isTauri() && backendSessionId) {
        try {
          const apiBase = (import.meta.env.VITE_API_URL as string) || ''
          const res = await fetch(`${apiBase}/api/sessions/${backendSessionId}/messages`)
          if (res.ok) {
            const rawMsgs = (await res.json()) as Array<Record<string, unknown>>
            const backendMsgs: Message[] = rawMsgs.map((m) => {
              const metaRaw = m.metadata
              const metadata =
                typeof metaRaw === 'string'
                  ? (JSON.parse(metaRaw) as Record<string, unknown>)
                  : (metaRaw as Record<string, unknown> | undefined)
              return {
                id: m.id as string,
                sessionId: backendSessionId,
                role: m.role as Message['role'],
                content: (m.content as string) ?? '',
                createdAt:
                  typeof m.created_at === 'number'
                    ? m.created_at
                    : typeof m.timestamp === 'string'
                      ? new Date(m.timestamp).getTime()
                      : Date.now(),
                tokensUsed: (m.tokens_used as number) || undefined,
                costUSD: (m.cost_usd as number | null) ?? undefined,
                model: (m.model as string | null) ?? undefined,
                metadata,
                toolCalls: (metadata?.toolCalls as Message['toolCalls']) ?? undefined,
              }
            })
            session.replaceMessagesFromBackend(backendMsgs)
            registerBackendSessionId(sessionId, backendSessionId)
          }
        } catch (syncErr) {
          log.warn('agent', 'Failed to sync messages from backend after edit-resend', {
            error: String(syncErr),
          })
        }
      }
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Unexpected error in editAndResend', { error: msg })
      session.updateMessage(assistantMsgId, {
        content: `**Error:** ${msg}`,
        error: { type: 'unknown', message: msg, timestamp: Date.now() },
      })
    } finally {
      batch(() => {
        setStreamingStartedAt(null)
        setLiveMessageId(null)
      })
    }
  }

  async function regenerateResponse(_assistantMessageId: string): Promise<void> {
    if (rustAgent.isRunning()) return
    log.info('agent', 'Regenerating response')
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
    // These use offset-aware derived signals so that after a steering message
    // splits the response, only the current placeholder's content is shown.
    isStreaming: rustAgent.isRunning, // alias for backward compat
    activeToolCalls: liveActiveToolCalls,
    streamingContent: liveStreamingContent,
    thinkingSegments: liveThinkingSegments,
    streamingTokenEstimate,
    streamingStartedAt,
    error,
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
