/**
 * useAgentActions — Action functions for the agent hook.
 *
 * Extracted from useAgent.ts: steer, followUp, postComplete, cancel,
 * retry, regenerate, editAndResend, queue operations, approval/question/plan
 * resolution, plan mode, auto-approval, and state getters.
 */

import { isTauri } from '@tauri-apps/api/core'
import { type Accessor, batch, type Setter } from 'solid-js'

import { generateMessageId } from '../lib/ids'
import { log } from '../lib/logger'
import { checkAutoApproval as sharedCheckAutoApproval } from '../lib/tool-approval'
import {
  ensureActiveSessionSynced,
  getCoreBudget,
  markActiveSessionSynced,
} from '../services/core-bridge'
import { registerBackendSessionId } from '../services/db-web-fallback'
import { rustAgent as rustAgentBridge, rustBackend } from '../services/rust-bridge'
import type { Message } from '../types'
import type { PlanData, PlanResponse } from '../types/rust-ipc'
import type { AgentState, ApprovalRequest, ToolActivity } from './agent'
import type { QueuedMessage } from './chat/types'
import type { QuestionRequest } from './useAgent'
import type { StreamingOffsets } from './useAgentStreaming'

// ── Deps: signals and stores the actions need to read/write ─────────

export interface ActionDeps {
  rustAgent: ReturnType<typeof import('./use-rust-agent').useRustAgent>
  session: ReturnType<typeof import('../stores/session').useSession>
  settingsRef: ReturnType<typeof import('../stores/settings').useSettings>

  // Signals
  isPlanMode: Accessor<boolean>
  setIsPlanMode: Setter<boolean>
  currentTurn: Accessor<number>
  tokensUsed: Accessor<number>
  currentThought: Accessor<string>
  setCurrentThought: Setter<string>
  toolActivity: Accessor<ToolActivity[]>
  setToolActivity: Setter<ToolActivity[]>
  pendingApproval: Accessor<ApprovalRequest | null>
  setPendingApproval: Setter<ApprovalRequest | null>
  pendingQuestion: Accessor<QuestionRequest | null>
  setPendingQuestion: Setter<QuestionRequest | null>
  pendingPlan: Accessor<PlanData | null>
  setPendingPlan: Setter<PlanData | null>
  doomLoopDetected: Accessor<boolean>
  setDoomLoopDetected: Setter<boolean>
  streamingTokenEstimate: Accessor<number>
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

// ── Action creators ─────────────────────────────────────────────────

export function createAgentActions(deps: ActionDeps) {
  const {
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
    setPendingQuestion,
    setPendingPlan,
    doomLoopDetected,
    setDoomLoopDetected,
    setStreamingTokenEstimate,
    setStreamingStartedAt,
    setMessageQueue,
    liveMessageId,
    setLiveMessageId,
    streaming,
    runOwnership,
  } = deps

  function mergePlanFeedback(
    feedback?: string,
    stepComments?: Record<string, string>
  ): string | undefined {
    const commentLines = Object.entries(stepComments ?? {})
      .filter(([, comment]) => comment.trim().length > 0)
      .map(([stepId, comment]) => `${stepId}: ${comment.trim()}`)

    if (commentLines.length === 0) {
      return feedback?.trim() || undefined
    }

    const commentsBlock = `Step comments:\n${commentLines.join('\n')}`
    return feedback?.trim() ? `${feedback.trim()}\n\n${commentsBlock}` : commentsBlock
  }

  function beginRunGuard(sessionId: string): {
    runToken: number
    runIsCurrent: () => boolean
    ownsOriginSession: () => boolean
    canMutateOriginSession: () => boolean
    settleMessageId: (fallbackId: string) => string
    updateMessage: (messageId: string, updates: Partial<Message>) => void
    deleteMessage: (messageId: string) => Promise<void>
    clearRunUiIfCurrent: () => void
  } {
    const runToken = runOwnership.beginRun()
    const runIsCurrent = (): boolean => runOwnership.isCurrentRun(runToken)
    const ownsOriginSession = (): boolean => session.currentSession()?.id === sessionId
    const canMutateOriginSession = (): boolean => runIsCurrent() && ownsOriginSession()

    return {
      runToken,
      runIsCurrent,
      ownsOriginSession,
      canMutateOriginSession,
      settleMessageId: (fallbackId: string): string => {
        if (runIsCurrent()) {
          return liveMessageId() || fallbackId
        }
        return fallbackId
      },
      updateMessage: (messageId: string, updates: Partial<Message>): void => {
        if (ownsOriginSession()) {
          session.updateMessage(messageId, updates)
          return
        }
        session.updateMessageInSession?.(sessionId, messageId, updates)
      },
      deleteMessage: async (messageId: string): Promise<void> => {
        if (ownsOriginSession()) {
          await session.deleteMessage(messageId)
          return
        }
        await session.deleteMessageInSession?.(sessionId, messageId)
      },
      clearRunUiIfCurrent: (): void => {
        if (!runIsCurrent()) {
          return
        }
        batch(() => {
          rustAgent.endRun()
          setLiveMessageId(null)
          setStreamingStartedAt(null)
        })
      },
    }
  }

  async function ensureDesktopSessionReady(messageId: string): Promise<boolean> {
    const sessionId = session.currentSession()?.id ?? ''
    if (!sessionId || !isTauri()) {
      return true
    }

    try {
      await ensureActiveSessionSynced(sessionId)
      return true
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error)
      session.setMessageError(messageId, {
        type: 'unknown',
        message,
        timestamp: Date.now(),
      })
      log.warn('agent', 'Desktop session action blocked by backend sync status', {
        sessionId,
        messageId,
        error: message,
      })
      return false
    }
  }

  function sessionOwnershipStillCurrent(
    initiatingSessionId: string | null,
    action: 'retry' | 'edit-and-resend' | 'regenerate',
    messageId: string
  ): boolean {
    if (!initiatingSessionId) {
      return true
    }

    const currentSessionId = session.currentSession()?.id ?? null
    if (currentSessionId === initiatingSessionId) {
      return true
    }

    log.info('agent', 'Desktop session action aborted after session switch', {
      action,
      messageId,
      initiatingSessionId,
      currentSessionId,
    })
    return false
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
    // steering content.
    const currentLiveId = liveMessageId()
    if (currentLiveId) {
      const currentContent = rustAgent.streamingContent()
      const currentToolCalls = rustAgent.activeToolCalls()
      const currentThinkingText = rustAgent.thinkingContent()
      const currentSegments = rustAgent.thinkingSegments()
      const contentOffset = streaming.streamingContentOffset()
      const tcOffset = streaming.toolCallsOffset()
      const tsOffset = streaming.thinkingSegmentsOffset()

      // Finalize the current placeholder with content accumulated since last offset
      const slicedContent = currentContent.slice(contentOffset)
      const slicedToolCalls = currentToolCalls.slice(tcOffset)
      const slicedThinking = currentThinkingText
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
        streaming.setStreamingContentOffset(newContentOffset)
        streaming.setToolCallsOffset(newTcOffset)
        streaming.setThinkingSegmentsOffset(newTsOffset)
      })
    }

    void rustAgent.steer(content)
  }

  function followUp(content: string): void {
    void rustAgent.followUp(content).then(
      () => setMessageQueue((prev) => [...prev, { content, tier: 'queued' }]),
      () => {
        log.warn('agent', 'Queue rejected by backend', { content: content.slice(0, 80) })
      }
    )
  }

  function postComplete(content: string, group?: number): void {
    void rustAgent.postComplete(content, group).then(
      () =>
        setMessageQueue((prev) => [...prev, { content, tier: 'post-complete', group: group ?? 1 }]),
      () => {
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
    const current = pendingApproval()
    if (!current) {
      return
    }
    const decision: 'once' | 'always' | 'denied' = !approved
      ? 'denied'
      : alwaysAllow
        ? 'always'
        : 'once'
    void rustAgentBridge
      .resolveApproval(current.id, approved, alwaysAllow ?? false)
      .then(() => {
        rustAgent.markToolApproval(current.toolName, decision, current.toolCallId)
        if (pendingApproval()?.id === current.id) {
          setPendingApproval(null)
        }
      })
      .catch((err) => {
        log.error('error', 'Failed to resolve approval', { error: String(err) })
      })
  }

  function resolveQuestion(answer: string): void {
    log.info('agent', 'Question resolved', { answerLength: answer.length })
    const current = deps.pendingQuestion()
    if (!current) {
      return
    }
    void rustAgentBridge
      .resolveQuestion(current.id, answer)
      .then(() => {
        if (deps.pendingQuestion()?.id === current.id) {
          setPendingQuestion(null)
        }
      })
      .catch((err) => {
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
    const current = deps.pendingPlan()
    if (!current) {
      return
    }
    if (!current.requestId) {
      log.error('error', 'Cannot resolve plan without requestId')
      return
    }
    const sanitizedModifiedPlan = modifiedPlan
      ? (() => {
          const { requestId: _requestId, ...planWithoutRequestId } = modifiedPlan
          return planWithoutRequestId
        })()
      : null
    const mergedFeedback = mergePlanFeedback(feedback, stepComments)
    void rustAgentBridge
      .resolvePlan(current.requestId, response, sanitizedModifiedPlan, mergedFeedback ?? null)
      .then(() => {
        if (deps.pendingPlan()?.requestId === current.requestId) {
          setPendingPlan(null)
        }
      })
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

  function reorderInQueue(fromIndex: number, toIndex: number): void {
    setMessageQueue((prev) => {
      if (fromIndex < 0 || fromIndex >= prev.length) return prev
      if (toIndex < 0 || toIndex >= prev.length) return prev
      const next = [...prev]
      const [item] = next.splice(fromIndex, 1)
      next.splice(toIndex, 0, item)
      return next
    })
  }

  function editInQueue(index: number, newContent: string): void {
    setMessageQueue((prev) => {
      if (index < 0 || index >= prev.length) return prev
      return prev.map((item, i) => (i === index ? { ...item, content: newContent } : item))
    })
  }

  function clearQueue(): void {
    setMessageQueue([])
  }

  async function retryMessage(assistantMessageId: string): Promise<void> {
    if (rustAgent.isRunning()) return
    log.info('agent', 'Retrying last message', { assistantMessageId })
    const initiatingSessionId = session.currentSession()?.id ?? null

    if (!(await ensureDesktopSessionReady(assistantMessageId))) {
      return
    }
    if (!sessionOwnershipStillCurrent(initiatingSessionId, 'retry', assistantMessageId)) {
      return
    }

    // ── 1. Reset agent UI state ──────────────────────────────────────
    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
      streaming.setStreamingContentOffset(0)
      streaming.setToolCallsOffset(0)
      streaming.setThinkingSegmentsOffset(0)
    })

    // ── 2. Clear the error from the existing assistant message and reuse it ──
    const selectedModelId = session.selectedModel()
    const selectedProviderId = session.selectedProvider() || undefined
    const runGuard = beginRunGuard(session.currentSession()?.id ?? '')
    session.updateMessage(assistantMessageId, {
      content: '',
      error: undefined,
      toolCalls: undefined,
      tokensUsed: undefined,
      costUSD: undefined,
      metadata: undefined,
      model: selectedModelId,
    })
    setLiveMessageId(assistantMessageId)

    // ── 3. Call the backend's retry API via the streaming IPC layer ──
    try {
      const runStartedAt = Date.now()
      const result = await rustAgent.retryRun()
      const errorText = rustAgent.error()

      if (errorText) {
        const isCancelled =
          errorText === 'Agent run cancelled by user' || errorText.includes('cancelled by user')
        if (isCancelled) {
          const partialContent = rustAgent.streamingContent()
          const elapsedMs = Date.now() - runStartedAt
          const retryMsgId = runGuard.settleMessageId(assistantMessageId)
          if (partialContent) {
            runGuard.updateMessage(retryMsgId, {
              content: partialContent,
              metadata: {
                provider: selectedProviderId,
                model: selectedModelId,
                elapsedMs,
                cancelled: true,
              },
            })
          } else {
            await runGuard.deleteMessage(retryMsgId)
          }
          runGuard.clearRunUiIfCurrent()
          return
        }
        const retryMsgId = runGuard.settleMessageId(assistantMessageId)
        runGuard.updateMessage(retryMsgId, {
          content: '',
          error: { type: 'unknown', message: errorText, timestamp: Date.now() },
        })
        runGuard.clearRunUiIfCurrent()
        return
      }

      // ── 4. Settle the assistant response ───────────────────────────
      const content = rustAgent.streamingContent()
      const elapsedMs = Date.now() - runStartedAt
      const thinking = rustAgent.thinkingContent()
      const segments = rustAgent.thinkingSegments()

      const retryMsgId = runGuard.settleMessageId(assistantMessageId)
      if (content) {
        runGuard.updateMessage(retryMsgId, {
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
        await runGuard.deleteMessage(retryMsgId)
      }
      runGuard.clearRunUiIfCurrent()

      // ── 5. Sync from backend in web mode ──────────────────────────
      const sessionId = session.currentSession()?.id ?? ''
      const backendSessionId = result?.sessionId || sessionId
      if (backendSessionId && runGuard.canMutateOriginSession()) {
        markActiveSessionSynced(backendSessionId)
      }
      if (!isTauri() && backendSessionId && runGuard.canMutateOriginSession()) {
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
          log.warn('agent', 'Failed to sync messages from backend after retry', {
            error: String(syncErr),
          })
        }
      }
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Unexpected error in retryMessage', { error: msg })
      const retryMsgId = runGuard.settleMessageId(assistantMessageId)
      runGuard.updateMessage(retryMsgId, {
        content: '',
        error: { type: 'unknown', message: msg, timestamp: Date.now() },
      })
      runGuard.clearRunUiIfCurrent()
    } finally {
      if (runGuard.runIsCurrent()) {
        batch(() => {
          setStreamingStartedAt(null)
          setLiveMessageId(null)
        })
      }
    }
  }

  async function editAndResend(messageId: string, newContent: string): Promise<void> {
    if (rustAgent.isRunning()) return
    log.info('agent', 'Edit and resend', { messageId, contentLength: newContent.length })
    const initiatingSessionId = session.currentSession()?.id ?? null
    const originalMessages = !isTauri()
      ? session.messages().map((message) => ({
          ...message,
          metadata: message.metadata ? { ...message.metadata } : undefined,
          toolCalls: message.toolCalls ? [...message.toolCalls] : undefined,
        }))
      : null

    if (!(await ensureDesktopSessionReady(messageId))) {
      return
    }
    if (!sessionOwnershipStillCurrent(initiatingSessionId, 'edit-and-resend', messageId)) {
      return
    }

    // ── 1. Clean up in-memory messages (immediate UI update) ──────────
    batch(() => {
      session.deleteMessagesAfter(messageId)
      session.deleteMessage(messageId)
      session.stopEditing()
    })

    // ── 2. Reset agent UI state ──────────────────────────────────────
    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
      streaming.setStreamingContentOffset(0)
      streaming.setToolCallsOffset(0)
      streaming.setThinkingSegmentsOffset(0)
    })

    // Ensure a session exists
    let currentSess = session.currentSession()
    if (!currentSess) {
      await session.createNewSession()
      currentSess = session.currentSession()
    }
    const sessionId = currentSess?.id ?? ''
    const runGuard = beginRunGuard(sessionId)

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
          const finalMsgId = runGuard.settleMessageId(assistantMsgId)
          if (partialContent) {
            runGuard.updateMessage(finalMsgId, {
              content: partialContent,
              metadata: {
                provider: selectedProviderId,
                model: selectedModelId,
                elapsedMs,
                cancelled: true,
              },
            })
          } else {
            await runGuard.deleteMessage(finalMsgId)
          }
          runGuard.clearRunUiIfCurrent()
          return
        }
        const finalMsgId = runGuard.settleMessageId(assistantMsgId)
        if (!isTauri() && originalMessages) {
          session.setMessages(originalMessages)
          session.setMessageError(messageId, {
            type: 'unknown',
            message: errorText,
            timestamp: Date.now(),
          })
        } else {
          runGuard.updateMessage(finalMsgId, {
            content: '',
            error: { type: 'unknown', message: errorText, timestamp: Date.now() },
          })
        }
        runGuard.clearRunUiIfCurrent()
        return
      }

      // Settle the assistant response
      const content = rustAgent.streamingContent()
      const elapsedMs = Date.now() - runStartedAt
      const thinking = rustAgent.thinkingContent()
      const segments = rustAgent.thinkingSegments()

      const finalMsgId = runGuard.settleMessageId(assistantMsgId)
      if (content) {
        runGuard.updateMessage(finalMsgId, {
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
        await runGuard.deleteMessage(finalMsgId)
      }
      runGuard.clearRunUiIfCurrent()

      // Sync from backend in web mode
      const backendSessionId = result?.sessionId || sessionId
      if (backendSessionId && runGuard.canMutateOriginSession()) {
        markActiveSessionSynced(backendSessionId)
      }
      if (!isTauri() && backendSessionId && runGuard.canMutateOriginSession()) {
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
      if (!isTauri() && originalMessages) {
        session.setMessages(originalMessages)
        session.setMessageError(messageId, {
          type: 'unknown',
          message: msg,
          timestamp: Date.now(),
        })
      } else {
        const finalMsgId = runGuard.settleMessageId(assistantMsgId)
        runGuard.updateMessage(finalMsgId, {
          content: `**Error:** ${msg}`,
          error: { type: 'unknown', message: msg, timestamp: Date.now() },
        })
      }
      runGuard.clearRunUiIfCurrent()
    } finally {
      if (runGuard.runIsCurrent()) {
        batch(() => {
          setStreamingStartedAt(null)
          setLiveMessageId(null)
        })
      }
    }
  }

  async function regenerateResponse(assistantMessageId: string): Promise<void> {
    if (rustAgent.isRunning()) return
    log.info('agent', 'Regenerating response', { assistantMessageId })
    const initiatingSessionId = session.currentSession()?.id ?? null

    if (!(await ensureDesktopSessionReady(assistantMessageId))) {
      return
    }
    if (!sessionOwnershipStillCurrent(initiatingSessionId, 'regenerate', assistantMessageId)) {
      return
    }

    // ── 1. Reset agent UI state ──────────────────────────────────────
    batch(() => {
      setCurrentThought('')
      setDoomLoopDetected(false)
      setToolActivity([])
      setStreamingTokenEstimate(0)
      setStreamingStartedAt(Date.now())
      streaming.setStreamingContentOffset(0)
      streaming.setToolCallsOffset(0)
      streaming.setThinkingSegmentsOffset(0)
    })

    // ── 2. Clear the existing assistant message and reuse it ─────────
    const selectedModelId = session.selectedModel()
    const selectedProviderId = session.selectedProvider() || undefined
    const runGuard = beginRunGuard(session.currentSession()?.id ?? '')
    session.updateMessage(assistantMessageId, {
      content: '',
      error: undefined,
      toolCalls: undefined,
      tokensUsed: undefined,
      costUSD: undefined,
      metadata: undefined,
      model: selectedModelId,
    })
    setLiveMessageId(assistantMessageId)

    // ── 3. Call the backend's regenerate API via the streaming IPC layer ──
    try {
      const runStartedAt = Date.now()
      const result = await rustAgent.regenerateRun()
      const errorText = rustAgent.error()

      if (errorText) {
        const isCancelled =
          errorText === 'Agent run cancelled by user' || errorText.includes('cancelled by user')
        if (isCancelled) {
          const partialContent = rustAgent.streamingContent()
          const elapsedMs = Date.now() - runStartedAt
          const regenerateMsgId = runGuard.settleMessageId(assistantMessageId)
          if (partialContent) {
            runGuard.updateMessage(regenerateMsgId, {
              content: partialContent,
              metadata: {
                provider: selectedProviderId,
                model: selectedModelId,
                elapsedMs,
                cancelled: true,
              },
            })
          } else {
            await runGuard.deleteMessage(regenerateMsgId)
          }
          runGuard.clearRunUiIfCurrent()
          return
        }
        const regenerateMsgId = runGuard.settleMessageId(assistantMessageId)
        runGuard.updateMessage(regenerateMsgId, {
          content: '',
          error: { type: 'unknown', message: errorText, timestamp: Date.now() },
        })
        runGuard.clearRunUiIfCurrent()
        return
      }

      // ── 4. Settle the assistant response ───────────────────────────
      const content = rustAgent.streamingContent()
      const elapsedMs = Date.now() - runStartedAt
      const thinking = rustAgent.thinkingContent()
      const segments = rustAgent.thinkingSegments()

      const regenerateMsgId = runGuard.settleMessageId(assistantMessageId)
      if (content) {
        runGuard.updateMessage(regenerateMsgId, {
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
        await runGuard.deleteMessage(regenerateMsgId)
      }
      runGuard.clearRunUiIfCurrent()

      // ── 5. Sync from backend in web mode ──────────────────────────
      const sessionId = session.currentSession()?.id ?? ''
      const backendSessionId = result?.sessionId || sessionId
      if (backendSessionId && runGuard.canMutateOriginSession()) {
        markActiveSessionSynced(backendSessionId)
      }
      if (!isTauri() && backendSessionId && runGuard.canMutateOriginSession()) {
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
          log.warn('agent', 'Failed to sync messages from backend after regenerate', {
            error: String(syncErr),
          })
        }
      }
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Unexpected error in regenerateResponse', { error: msg })
      const regenerateMsgId = runGuard.settleMessageId(assistantMessageId)
      runGuard.updateMessage(regenerateMsgId, {
        content: '',
        error: { type: 'unknown', message: msg, timestamp: Date.now() },
      })
      runGuard.clearRunUiIfCurrent()
    } finally {
      if (runGuard.runIsCurrent()) {
        batch(() => {
          setStreamingStartedAt(null)
          setLiveMessageId(null)
        })
      }
    }
  }

  async function undoLastEdit(): Promise<{ success: boolean; message: string }> {
    const result = await rustBackend.undoLastEdit()
    return { success: result.success, message: result.message }
  }

  return {
    cancel,
    steer,
    followUp,
    postComplete,
    togglePlanMode,
    checkAutoApproval,
    resolveApproval,
    resolveQuestion,
    resolvePlan,
    clearError,
    getState,
    removeFromQueue,
    reorderInQueue,
    editInQueue,
    clearQueue,
    retryMessage,
    editAndResend,
    regenerateResponse,
    undoLastEdit,
  }
}
