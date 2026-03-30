/**
 * useAgentRun — The main run() function for the agent hook.
 *
 * Extracted from useAgent.ts: sets up streaming, creates placeholders,
 * calls rustAgent.run(), handles completion/cancellation/errors,
 * finalizes messages, and processes the message queue.
 */

import { isTauri } from '@tauri-apps/api/core'
import { type Accessor, batch, type Setter } from 'solid-js'

import { DEFAULTS } from '../config/constants'
import { debugLog } from '../lib/debug-log'
import { generateMessageId } from '../lib/ids'
import { log } from '../lib/logger'
import { deriveSessionTitle } from '../lib/title-utils'
import { decodeCompactionModel } from '../services/context-compaction'
import { getCoreBudget } from '../services/core-bridge'
import { registerBackendSessionId } from '../services/db-web-fallback'
import { rustBackend } from '../services/rust-bridge'
import type { Message } from '../types'
import type { ToolActivity } from './agent'
import type { QueuedMessage } from './chat/types'
import type { StreamingOffsets } from './useAgentStreaming'

/** Small promise-based delay for async coordination. */
const delay = (ms: number): Promise<void> => new Promise((resolve) => setTimeout(resolve, ms))

// ── Deps: signals and stores the run function needs ─────────────────

export interface RunDeps {
  rustAgent: ReturnType<typeof import('./use-rust-agent').useRustAgent>
  session: ReturnType<typeof import('../stores/session').useSession>
  settingsRef: ReturnType<typeof import('../stores/settings').useSettings>

  // Team mode
  isTeamMode: () => boolean

  // Signals
  isPlanMode: Accessor<boolean>
  setCurrentThought: Setter<string>
  setDoomLoopDetected: Setter<boolean>
  setToolActivity: Setter<ToolActivity[]>
  setStreamingTokenEstimate: Setter<number>
  streamingStartedAt: Accessor<number | null>
  setStreamingStartedAt: Setter<number | null>
  messageQueue: Accessor<QueuedMessage[]>
  setMessageQueue: Setter<QueuedMessage[]>
  liveMessageId: Accessor<string | null>
  setLiveMessageId: Setter<string | null>

  // Streaming offsets
  streaming: StreamingOffsets
}

export function createAgentRun(deps: RunDeps) {
  const {
    rustAgent,
    session,
    settingsRef,
    isTeamMode,
    isPlanMode,
    setCurrentThought,
    setDoomLoopDetected,
    setToolActivity,
    setStreamingTokenEstimate,
    setStreamingStartedAt,
    messageQueue,
    setMessageQueue,
    liveMessageId,
    setLiveMessageId,
    streaming,
  } = deps

  async function run(
    goal: string,
    config?: { model?: string; provider?: string }
  ): Promise<unknown> {
    if (rustAgent.isRunning()) {
      setMessageQueue((prev) => [...prev, { content: goal }])
      return null
    }

    // Track whether the run completed successfully (not cancelled / errored).
    // The queue should only auto-submit after successful completions.
    let ranSuccessfully = false

    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
      // Reset steering offsets for new run
      streaming.setStreamingContentOffset(0)
      streaming.setToolCallsOffset(0)
      streaming.setThinkingSegmentsOffset(0)
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

      // Team mode: route to HQ Director instead of solo agent
      if (isTeamMode()) {
        log.info('agent', 'Team mode — routing to HQ Director', {
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
              customPrompt: l.customPrompt,
            })),
          }
          await rustBackend.startHq(goal, undefined, teamConfigPayload)
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err)
          log.error('agent', 'HQ failed', { error: msg })
          session.updateMessage(assistantMsgId, {
            content: '',
            error: { type: 'unknown', message: msg, timestamp: Date.now() },
          })
        }
        return null
      }

      const compactionModel = decodeCompactionModel(
        settingsRef.settings().generation.compactionModel
      )
      const result = await rustAgent.run(goal, {
        model: selectedModelId,
        provider: selectedProviderId,
        thinkingLevel,
        sessionId,
        autoCompact: settingsRef.settings().generation.autoCompact,
        compactionThreshold: settingsRef.settings().generation.compactionThreshold,
        compactionProvider: compactionModel?.provider,
        compactionModel: compactionModel?.model,
      })
      const errorText = rustAgent.error()

      // Check if the agent errored
      if (errorText) {
        const isCancelled =
          errorText === 'Agent run cancelled by user' || errorText.includes('cancelled by user')

        if (isCancelled) {
          log.info('agent', 'Run cancelled by user — preserving partial response')
          const cancelMsgId = liveMessageId() || assistantMsgId
          const fullPartial = rustAgent.streamingContent()
          const cOffset = streaming.streamingContentOffset()
          const partialContent = cOffset > 0 ? fullPartial.slice(cOffset) : fullPartial
          const partialThinking = rustAgent.thinkingContent()
          const elapsedMs = Date.now() - runStartedAt
          if (partialContent || partialThinking) {
            const allSegments = rustAgent.thinkingSegments()
            const sOffset = streaming.thinkingSegmentsOffset()
            const partialSegments = sOffset > 0 ? allSegments.slice(sOffset) : allSegments
            const allTc = rustAgent.activeToolCalls()
            const tOffset = streaming.toolCallsOffset()
            const partialToolCalls = tOffset > 0 ? allTc.slice(tOffset) : allTc
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
        const errorMsgId = liveMessageId() || assistantMsgId
        batch(() => {
          session.updateMessage(errorMsgId, {
            content: '',
            error: { type: 'unknown', message: errorText, timestamp: Date.now() },
          })
          rustAgent.endRun()
        })
        return null
      }

      // Settle the assistant response into the placeholder.
      const finalMsgId = liveMessageId() || assistantMsgId
      const fullContent = rustAgent.streamingContent()
      const contentOffset = streaming.streamingContentOffset()
      const content = contentOffset > 0 ? fullContent.slice(contentOffset) : fullContent
      const elapsedMs = Date.now() - runStartedAt
      const thinking = rustAgent.thinkingContent()
      const allSegments = rustAgent.thinkingSegments()
      const tsOffset = streaming.thinkingSegmentsOffset()
      const segments = tsOffset > 0 ? allSegments.slice(tsOffset) : allSegments
      const allToolCalls = rustAgent.activeToolCalls()
      const tcOffset = streaming.toolCallsOffset()
      const toolCalls = tcOffset > 0 ? allToolCalls.slice(tcOffset) : allToolCalls
      debugLog(
        'thinking',
        'message metadata:',
        thinking ? `yes (${thinking.length} chars)` : 'no',
        segments.length > 0 ? `${segments.length} segments` : ''
      )

      batch(() => {
        if (content) {
          session.updateMessage(finalMsgId, {
            content,
            tokensUsed: rustAgent.tokenUsage().output,
            costUSD: rustAgent.tokenUsage().cost || undefined,
            toolCalls,
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
          session.deleteMessage(finalMsgId)
        }
        rustAgent.endRun()
        setLiveMessageId(null)
        setStreamingStartedAt(null)
      })

      log.info('agent', 'Run completed', {
        success: true,
        tokens: rustAgent.tokenUsage().output,
        cost: rustAgent.tokenUsage().cost,
        toolCalls: rustAgent.activeToolCalls().length,
        contentLength: content?.length ?? 0,
      })

      const backendSessionId = result?.sessionId || sessionId
      if (!isTauri() && backendSessionId) {
        registerBackendSessionId(sessionId, backendSessionId)
        log.info('agent', 'Backend session ID registered', { backendSessionId })
      }

      ranSuccessfully = true
      return result
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Unexpected agent error', { error: msg })
      batch(() => {
        session.updateMessage(assistantMsgId, {
          content: `**Error:** ${msg}`,
          error: { type: 'unknown', message: msg, timestamp: Date.now() },
        })
        rustAgent.endRun()
      })
      return null
    } finally {
      batch(() => {
        setStreamingStartedAt(null)
        setLiveMessageId(null)
      })
      // Auto-submit queued messages only after successful runs.
      // Don't drain on cancel/error — the user should decide what to do.
      if (ranSuccessfully) {
        const queue = messageQueue()
        if (queue.length > 0) {
          const next = queue[0]!
          setMessageQueue((prev) => prev.slice(1))
          log.info('agent', 'Auto-submitting queued message', {
            content: next.content.slice(0, 80),
            remaining: queue.length - 1,
          })
          // In web mode, the backend clears its `running` flag asynchronously
          // after sending the `complete` WebSocket event.  A small delay prevents
          // a 409 "Agent is already running" race when we immediately re-submit.
          if (!isTauri()) {
            void delay(150).then(() => run(next.content))
          } else {
            void run(next.content)
          }
        }
      }
    }
  }

  return { run }
}
