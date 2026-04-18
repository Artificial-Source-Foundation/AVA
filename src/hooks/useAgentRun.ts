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
import {
  persistAssistantPayloadToBackendSession,
  syncMessagesFromBackend,
} from '../services/agent-settlement'
import { decodeCompactionModel } from '../services/context-compaction'
import {
  getCoreBudget,
  markActiveSessionSynced,
  markSessionNeedsAuthoritativeRecovery,
} from '../services/core-bridge'
import { registerBackendSessionId } from '../services/web-session-identity'
import type { Message } from '../types'
import type { ToolActivity } from './agent'
import type { QueuedMessage } from './chat/types'
import type { StreamingOffsets } from './useAgentStreaming'

/** Small promise-based delay for async coordination. */
const delay = (ms: number): Promise<void> => new Promise((resolve) => setTimeout(resolve, ms))

function shouldPreserveAssistantCompletion(options: {
  content: string
  thinking: string
  segments: Array<{ thinking: string; toolCallIds: string[] }>
  toolCalls: Array<unknown>
}): boolean {
  return (
    options.content.length > 0 ||
    options.thinking.length > 0 ||
    options.toolCalls.length > 0 ||
    options.segments.some(
      (segment) => segment.thinking.length > 0 || segment.toolCallIds.length > 0
    )
  )
}

// ── Deps: signals and stores the run function needs ─────────────────

export interface RunDeps {
  rustAgent: ReturnType<typeof import('./use-rust-agent').useRustAgent>
  session: ReturnType<typeof import('../stores/session').useSession>
  settingsRef: ReturnType<typeof import('../stores/settings').useSettings>
  sessionHasActiveRun?: (sessionId?: string | null) => boolean
  onSessionRuntimeSettled?: (sessionId: string) => void

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
  runOwnership: {
    beginRun: () => number
    isCurrentRun: (token: number) => boolean
  }
}

export function createAgentRun(deps: RunDeps) {
  const {
    rustAgent,
    session,
    settingsRef,
    sessionHasActiveRun,
    onSessionRuntimeSettled,
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
    runOwnership,
  } = deps

  async function run(
    goal: string,
    config?: { model?: string; provider?: string }
  ): Promise<unknown> {
    const activeSessionId = session.currentSession()?.id
    const currentSessionHasActiveRun =
      sessionHasActiveRun?.(activeSessionId ?? null) ??
      (rustAgent.isRunning() &&
        ((typeof rustAgent.trackedSessionId === 'function' ? rustAgent.trackedSessionId() : null) ??
          activeSessionId ??
          null) === (activeSessionId ?? null))

    if (currentSessionHasActiveRun) {
      setMessageQueue((prev) => [...prev, { content: goal, sessionId: activeSessionId }])
      return null
    }

    const runToken = runOwnership.beginRun()

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
    const settleSessionRuntimeCache = (): void => {
      if (sessionId) {
        onSessionRuntimeSettled?.(sessionId)
      }
    }
    const ownsOriginSession = (): boolean => session.currentSession()?.id === sessionId
    const runIsCurrent = (): boolean => runOwnership.isCurrentRun(runToken)
    const canMutateOriginSession = (): boolean => runIsCurrent() && ownsOriginSession()
    const settleRunMessageId = (fallbackId: string): string => {
      if (runIsCurrent()) {
        return liveMessageId() || fallbackId
      }
      return fallbackId
    }
    const updateOriginMessage = (messageId: string, updates: Partial<Message>): void => {
      if (ownsOriginSession()) {
        session.updateMessage(messageId, updates)
        return
      }
      session.updateMessageInSession?.(sessionId, messageId, updates)
    }
    const deleteOriginMessage = async (messageId: string): Promise<void> => {
      if (ownsOriginSession()) {
        await session.deleteMessage(messageId)
        return
      }
      await session.deleteMessageInSession?.(sessionId, messageId)
    }
    const addOriginMessage = (message: Message): void => {
      if (ownsOriginSession()) {
        session.addMessage(message)
        return
      }
      session.addMessageToSession?.(message)
    }
    const clearRunUiIfCurrent = (): void => {
      if (!runIsCurrent()) {
        return
      }
      batch(() => {
        rustAgent.endRun()
        setLiveMessageId(null)
        setStreamingStartedAt(null)
      })
    }

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

      const detachedSessionId =
        result?.detachedSessionId ??
        (typeof rustAgent.detachedSessionId === 'function' ? rustAgent.detachedSessionId() : null)

      if (!errorText && detachedSessionId === sessionId) {
        const backendSessionId = result?.sessionId || sessionId
        let persistenceSucceeded = isTauri() // In Tauri mode, we don't persist via HTTP so consider it succeeded
        if (!isTauri() && backendSessionId) {
          registerBackendSessionId(sessionId, backendSessionId)
          persistenceSucceeded = await persistAssistantPayloadToBackendSession(backendSessionId, {
            content: rustAgent.streamingContent(),
            thinking: rustAgent.thinkingContent(),
            segments: rustAgent.thinkingSegments(),
            toolCalls: rustAgent.activeToolCalls(),
            tokensUsed: rustAgent.tokenUsage().output,
            costUSD: rustAgent.tokenUsage().cost,
            elapsedMs: Date.now() - runStartedAt,
            provider: selectedProviderId,
            model: selectedModelId,
            mode: isPlanMode() ? 'plan' : 'code',
          })
        }
        // Only mark for authoritative recovery if persistence succeeded (or Tauri mode)
        // This avoids recovery attempts from stale backend state after failed persistence
        if (persistenceSucceeded) {
          markSessionNeedsAuthoritativeRecovery(sessionId)
        }
        const detachedMsgId = settleRunMessageId(assistantMsgId)
        const detachedContent = rustAgent.streamingContent()
        const detachedContentOffset = streaming.streamingContentOffset()
        const partialContent =
          detachedContentOffset > 0 ? detachedContent.slice(detachedContentOffset) : detachedContent
        const detachedThinking = rustAgent.thinkingContent()
        const detachedSegments = rustAgent.thinkingSegments()
        const detachedSegmentsOffset = streaming.thinkingSegmentsOffset()
        const partialSegments =
          detachedSegmentsOffset > 0
            ? detachedSegments.slice(detachedSegmentsOffset)
            : detachedSegments
        const detachedToolCalls = rustAgent.activeToolCalls()
        const detachedToolCallsOffset = streaming.toolCallsOffset()
        const partialToolCalls =
          detachedToolCallsOffset > 0
            ? detachedToolCalls.slice(detachedToolCallsOffset)
            : detachedToolCalls

        if (
          shouldPreserveAssistantCompletion({
            content: partialContent,
            thinking: detachedThinking,
            segments: partialSegments,
            toolCalls: partialToolCalls,
          })
        ) {
          updateOriginMessage(detachedMsgId, {
            content: partialContent,
            tokensUsed: rustAgent.tokenUsage().output,
            costUSD: rustAgent.tokenUsage().cost || undefined,
            toolCalls: partialToolCalls,
            metadata: {
              provider: selectedProviderId,
              model: selectedModelId,
              mode: isPlanMode() ? 'plan' : 'code',
              elapsedMs: Date.now() - runStartedAt,
              ...(detachedThinking ? { thinking: detachedThinking } : {}),
              ...(partialSegments.length > 1 ? { thinkingSegments: partialSegments } : {}),
            },
          })
        } else {
          await deleteOriginMessage(detachedMsgId)
        }
        rustAgent.clearDetachedSessionId?.()
        clearRunUiIfCurrent()
        settleSessionRuntimeCache()
        return null
      }

      // Check if the agent errored
      if (errorText) {
        const isCancelled =
          errorText === 'Agent run cancelled by user' || errorText.includes('cancelled by user')

        if (isCancelled) {
          log.info('agent', 'Run cancelled by user — preserving partial response')
          const cancelMsgId = settleRunMessageId(assistantMsgId)
          const fullPartial = rustAgent.streamingContent()
          const cOffset = streaming.streamingContentOffset()
          const partialContent = cOffset > 0 ? fullPartial.slice(cOffset) : fullPartial
          const partialThinking = rustAgent.thinkingContent()
          const allSegments = rustAgent.thinkingSegments()
          const sOffset = streaming.thinkingSegmentsOffset()
          const partialSegments = sOffset > 0 ? allSegments.slice(sOffset) : allSegments
          const allTc = rustAgent.activeToolCalls()
          const tOffset = streaming.toolCallsOffset()
          const partialToolCalls = tOffset > 0 ? allTc.slice(tOffset) : allTc
          const elapsedMs = Date.now() - runStartedAt
          if (
            shouldPreserveAssistantCompletion({
              content: partialContent,
              thinking: partialThinking,
              segments: partialSegments,
              toolCalls: partialToolCalls,
            })
          ) {
            updateOriginMessage(cancelMsgId, {
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
            await deleteOriginMessage(cancelMsgId)
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
          addOriginMessage(cancelNote)
          clearRunUiIfCurrent()
          settleSessionRuntimeCache()
          return null
        }

        log.error('agent', 'Run failed', { error: errorText })
        const errorMsgId = settleRunMessageId(assistantMsgId)
        updateOriginMessage(errorMsgId, {
          content: '',
          error: { type: 'unknown', message: errorText, timestamp: Date.now() },
        })
        clearRunUiIfCurrent()
        settleSessionRuntimeCache()
        return null
      }

      // Settle the assistant response into the placeholder.
      const finalMsgId = settleRunMessageId(assistantMsgId)
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

      if (
        shouldPreserveAssistantCompletion({
          content,
          thinking,
          segments,
          toolCalls,
        })
      ) {
        updateOriginMessage(finalMsgId, {
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
        await deleteOriginMessage(finalMsgId)
      }
      clearRunUiIfCurrent()

      log.info('agent', 'Run completed', {
        success: true,
        tokens: rustAgent.tokenUsage().output,
        cost: rustAgent.tokenUsage().cost,
        toolCalls: rustAgent.activeToolCalls().length,
        contentLength: content?.length ?? 0,
      })

      const backendSessionId = result?.sessionId || sessionId
      if (!isTauri() && backendSessionId && canMutateOriginSession()) {
        await syncMessagesFromBackend(
          backendSessionId,
          { session, markActiveSessionSynced },
          {
            originSessionId: sessionId,
            assistantPayload: {
              content: rustAgent.streamingContent(),
              thinking: rustAgent.thinkingContent(),
              segments: rustAgent.thinkingSegments(),
              toolCalls: rustAgent.activeToolCalls(),
              tokensUsed: rustAgent.tokenUsage().output,
              costUSD: rustAgent.tokenUsage().cost,
              elapsedMs: elapsedMs,
              provider: selectedProviderId,
              model: selectedModelId,
              mode: isPlanMode() ? 'plan' : 'code',
            },
          }
        ).catch((syncErr) => {
          log.warn('agent', 'Failed to sync messages from backend after run', {
            error: String(syncErr),
          })
        })
        registerBackendSessionId(sessionId, backendSessionId)
        log.info('agent', 'Backend session ID registered', { backendSessionId })
      }

      ranSuccessfully = result?.success === true
      settleSessionRuntimeCache()
      return result
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Unexpected agent error', { error: msg })
      const errorMsgId = settleRunMessageId(assistantMsgId)
      updateOriginMessage(errorMsgId, {
        content: `**Error:** ${msg}`,
        error: { type: 'unknown', message: msg, timestamp: Date.now() },
      })
      clearRunUiIfCurrent()
      settleSessionRuntimeCache()
      return null
    } finally {
      if (runIsCurrent()) {
        batch(() => {
          setStreamingStartedAt(null)
          setLiveMessageId(null)
        })
      }
      // Auto-submit queued messages only after successful runs.
      // Don't drain on cancel/error — the user should decide what to do.
      if (ranSuccessfully) {
        setMessageQueue((prev) =>
          prev.filter(
            (message) => !(message.sessionId === sessionId && message.backendManaged === true)
          )
        )
      }

      if (ranSuccessfully && canMutateOriginSession()) {
        const queue = messageQueue()
        const currentSessionId = session.currentSession()?.id
        const nextIndex = queue.findIndex(
          (message) => message.backendManaged !== true && message.sessionId === currentSessionId
        )
        if (nextIndex >= 0) {
          const next = queue[nextIndex]!
          setMessageQueue((prev) =>
            prev.filter(
              (message, index) =>
                !(
                  message.sessionId === currentSessionId &&
                  (message.backendManaged === true || index === nextIndex)
                )
            )
          )
          log.info('agent', 'Auto-submitting queued message', {
            content: next.content.slice(0, 80),
            remaining:
              queue.filter(
                (message) =>
                  message.sessionId === currentSessionId && message.backendManaged !== true
              ).length - 1,
          })
          // In web mode, the backend clears its `running` flag asynchronously
          // after sending the `complete` WebSocket event.  A small delay prevents
          // a 409 "Agent is already running" race when we immediately re-submit.
          if (!isTauri()) {
            void delay(150).then(() => run(next.content))
          } else {
            void run(next.content)
          }
        } else {
          setMessageQueue((prev) =>
            prev.filter(
              (message) =>
                !(message.sessionId === currentSessionId && message.backendManaged === true)
            )
          )
        }
      }
    }
  }

  return { run }
}
