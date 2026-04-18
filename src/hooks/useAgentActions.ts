/**
 * useAgentActions — Action functions for the agent hook.
 *
 * Extracted from useAgent.ts: steer, followUp, postComplete, cancel,
 * retry, regenerate, editAndResend, queue operations, approval/question/plan
 * resolution, plan mode, auto-approval, and state getters.
 *
 * Settlement coordination (message finalization, backend sync) is now delegated
 * to the agent-settlement service to reduce duplication across retry/edit/regenerate.
 * This file focuses on action orchestration and UI state management.
 *
 * @see src/services/agent-settlement.ts for settlement helpers
 */

import { isTauri } from '@tauri-apps/api/core'
import { type Accessor, batch, type Setter } from 'solid-js'

import { generateMessageId } from '../lib/ids'
import { log } from '../lib/logger'
import { checkAutoApproval as sharedCheckAutoApproval } from '../lib/tool-approval'
import {
  sessionOwnershipStillCurrent as checkSessionOwnershipStillCurrent,
  createRunGuard,
  isCancelledError,
  type RunGuard,
  resetAgentUiState,
  restoreMessagesAndSetError,
  settleCancelledAssistantMessage,
  settleDetachedSessionResult,
  settleErrorAssistantMessage,
  settleSuccessAssistantMessage,
  syncMessagesFromBackend,
} from '../services/agent-settlement'
import {
  ensureActiveSessionSynced,
  getCoreBudget,
  markActiveSessionSynced,
  markSessionNeedsAuthoritativeRecovery,
} from '../services/core-bridge'
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
  sessionHasActiveRun?: (sessionId?: string | null) => boolean
  onSessionRuntimeSettled?: (sessionId: string) => void

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
  pendingQuestion: Accessor<QuestionRequest | null>
  pendingPlan: Accessor<PlanData | null>
  setPendingApproval?: Setter<ApprovalRequest | null>
  setPendingQuestion?: Setter<QuestionRequest | null>
  setPendingPlan?: Setter<PlanData | null>
  removePendingApproval?: (requestId: string | null | undefined) => void
  removePendingQuestion?: (requestId: string | null | undefined) => void
  removePendingPlan?: (requestId: string | null | undefined) => void
  clearPendingInteractiveRequests?: () => void
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
    sessionHasActiveRun,
    onSessionRuntimeSettled,
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
    removePendingApproval,
    removePendingQuestion,
    removePendingPlan,
    clearPendingInteractiveRequests,
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
  const targetSessionHasActiveRun = (sessionId?: string | null): boolean => {
    if (sessionHasActiveRun) {
      return sessionHasActiveRun(sessionId ?? null)
    }

    if (!rustAgent.isRunning()) {
      return false
    }

    const trackedSessionId =
      typeof rustAgent.trackedSessionId === 'function' ? rustAgent.trackedSessionId() : null
    if (!trackedSessionId) {
      return true
    }

    return trackedSessionId === (sessionId ?? session.currentSession()?.id ?? null)
  }
  const currentSessionHasActiveRun = (): boolean =>
    targetSessionHasActiveRun(session.currentSession()?.id ?? null)
  const currentSessionRunId = (): string | null =>
    currentSessionHasActiveRun() ? rustAgent.currentRunId() : null
  const clearAllPendingInteractiveRequests = (): void => {
    if (clearPendingInteractiveRequests) {
      clearPendingInteractiveRequests()
      return
    }

    setPendingApproval?.(null)
    setPendingQuestion?.(null)
    setPendingPlan?.(null)
  }
  const clearResolvedApproval = (requestId: string | null | undefined): void => {
    if (removePendingApproval) {
      removePendingApproval(requestId)
      return
    }

    if (requestId && pendingApproval()?.id === requestId) {
      setPendingApproval?.(null)
    }
  }
  const clearResolvedQuestion = (requestId: string | null | undefined): void => {
    if (removePendingQuestion) {
      removePendingQuestion(requestId)
      return
    }

    if (requestId && deps.pendingQuestion()?.id === requestId) {
      setPendingQuestion?.(null)
    }
  }
  const clearResolvedPlan = (requestId: string | null | undefined): void => {
    if (removePendingPlan) {
      removePendingPlan(requestId)
      return
    }

    if (requestId && deps.pendingPlan()?.requestId === requestId) {
      setPendingPlan?.(null)
    }
  }

  type QueueMutationScope = 'all' | 'regular' | 'post-complete'
  const queueScopeForTier = (tier?: QueuedMessage['tier']): QueueMutationScope =>
    tier === 'post-complete' ? 'post-complete' : 'regular'

  const visibleQueueIndicesForCurrentSession = (
    queue: QueuedMessage[],
    scope: QueueMutationScope = 'all'
  ): number[] => {
    const currentSessionId = session.currentSession()?.id

    return queue.reduce<number[]>((indices, item, index) => {
      if (item.sessionId && item.sessionId !== currentSessionId) {
        return indices
      }

      if (scope === 'regular' && item.tier === 'post-complete') {
        return indices
      }

      if (scope === 'post-complete' && item.tier !== 'post-complete') {
        return indices
      }

      if (!item.sessionId || item.sessionId === currentSessionId) {
        indices.push(index)
      }
      return indices
    }, [])
  }

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

  /**
   * @deprecated Use createRunGuard from agent-settlement service directly.
   * Kept for incremental migration; will be removed in follow-up refactor.
   */
  function beginRunGuard(sessionId: string): RunGuard {
    return createRunGuard({
      sessionId,
      session,
      liveMessageId,
      setLiveMessageId,
      setStreamingStartedAt,
      runOwnership,
      rustAgent,
    })
  }

  const settleSessionRuntimeCache = (sessionId: string | null | undefined): void => {
    if (sessionId) {
      onSessionRuntimeSettled?.(sessionId)
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

  /**
   * @deprecated Use checkSessionOwnershipStillCurrent from agent-settlement service.
   * Kept as thin wrapper for incremental migration.
   */
  function sessionOwnershipStillCurrent(
    initiatingSessionId: string | null,
    action: 'retry' | 'edit-and-resend' | 'regenerate',
    messageId: string
  ): boolean {
    const currentSessionId = session.currentSession()?.id ?? null
    return checkSessionOwnershipStillCurrent(
      initiatingSessionId,
      currentSessionId,
      action,
      messageId
    )
  }

  function cancel(): void {
    if (!currentSessionHasActiveRun()) {
      return
    }
    log.info('agent', 'Cancel requested by user')
    void rustAgent.cancel()
    batch(() => {
      setMessageQueue((prev) =>
        prev.filter((message) => message.tier !== 'steering' && message.tier !== 'interrupt')
      )
      clearAllPendingInteractiveRequests()
      setStreamingStartedAt(null)
    })
  }

  function steer(content: string): void {
    if (!currentSessionHasActiveRun()) {
      return
    }
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

  async function followUp(content: string, sessionId?: string): Promise<void> {
    const queueSessionId = sessionId ?? session.currentSession()?.id

    if (!targetSessionHasActiveRun(queueSessionId ?? null)) {
      throw new Error('Agent is not running')
    }

    await rustAgent.followUp(content, queueSessionId)
    setMessageQueue((prev) => [
      ...prev,
      {
        content,
        tier: 'follow-up',
        backendManaged: true,
        sessionId: queueSessionId,
      },
    ])
  }

  async function postComplete(content: string, group?: number, sessionId?: string): Promise<void> {
    const queueSessionId = sessionId ?? session.currentSession()?.id

    if (!targetSessionHasActiveRun(queueSessionId ?? null)) {
      throw new Error('Agent is not running')
    }

    await rustAgent.postComplete(content, group, queueSessionId)
    setMessageQueue((prev) => [
      ...prev,
      {
        content,
        tier: 'post-complete',
        group: group ?? 1,
        backendManaged: true,
        sessionId: queueSessionId,
      },
    ])
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

  function resolveApproval(approved: boolean, alwaysAllow?: boolean): Promise<void> {
    log.info('tools', 'Approval resolved', { approved, alwaysAllow: alwaysAllow ?? false })
    const current = pendingApproval()
    if (!current) {
      return Promise.reject(new Error('Cannot resolve approval: no pending approval'))
    }
    const decision: 'once' | 'always' | 'denied' = !approved
      ? 'denied'
      : alwaysAllow
        ? 'always'
        : 'once'
    return rustAgentBridge
      .resolveApproval(current.id, approved, alwaysAllow ?? false)
      .then(() => {
        rustAgent.markToolApproval(current.toolName, decision, current.toolCallId)
        clearResolvedApproval(current.id)
      })
      .catch((err) => {
        log.error('error', 'Failed to resolve approval', { error: String(err) })
        throw err
      })
  }

  function resolveQuestion(answer: string): Promise<void> {
    log.info('agent', 'Question resolved', { answerLength: answer.length })
    const current = deps.pendingQuestion()
    if (!current) {
      return Promise.reject(new Error('Cannot resolve question: no pending question'))
    }
    return rustAgentBridge
      .resolveQuestion(current.id, answer)
      .then(() => {
        clearResolvedQuestion(current.id)
      })
      .catch((err) => {
        log.error('error', 'Failed to resolve question', { error: String(err) })
        throw err
      })
  }

  function resolvePlan(
    response: PlanResponse,
    modifiedPlan?: PlanData,
    feedback?: string,
    stepComments?: Record<string, string>
  ): Promise<void> {
    log.info('agent', 'Plan resolved', { response, hasFeedback: !!feedback })
    const current = deps.pendingPlan()
    if (!current) {
      return Promise.reject(new Error('Cannot resolve plan: no pending plan'))
    }
    if (!current.requestId) {
      const error = new Error('Cannot resolve plan without requestId')
      log.error('error', error.message)
      return Promise.reject(error)
    }
    const sanitizedModifiedPlan = modifiedPlan
      ? (() => {
          const { requestId: _requestId, ...planWithoutRequestId } = modifiedPlan
          return planWithoutRequestId
        })()
      : null
    const mergedFeedback = mergePlanFeedback(feedback, stepComments)
    return rustAgentBridge
      .resolvePlan(current.requestId, response, sanitizedModifiedPlan, mergedFeedback ?? null)
      .then(() => {
        clearResolvedPlan(current.requestId)
      })
      .catch((err) => {
        log.error('error', 'Failed to resolve plan', { error: String(err) })
        throw err
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

  function removeFromQueue(index: number, scope: QueueMutationScope = 'all'): void {
    setMessageQueue((prev) => {
      const visibleIndices = visibleQueueIndicesForCurrentSession(prev, scope)
      if (index < 0 || index >= visibleIndices.length) return prev

      const fullIndex = visibleIndices[index]
      if (fullIndex === undefined || prev[fullIndex]?.backendManaged) return prev

      return prev.filter((_, i) => i !== fullIndex)
    })
  }

  function reorderInQueue(
    fromIndex: number,
    toIndex: number,
    scope: QueueMutationScope = 'all'
  ): void {
    setMessageQueue((prev) => {
      const visibleIndices = visibleQueueIndicesForCurrentSession(prev, scope)
      if (fromIndex < 0 || fromIndex >= visibleIndices.length) return prev
      if (toIndex < 0 || toIndex >= visibleIndices.length) return prev

      const fromFullIndex = visibleIndices[fromIndex]
      const toFullIndex = visibleIndices[toIndex]
      if (fromFullIndex === undefined || toFullIndex === undefined) return prev
      if (prev[fromFullIndex]?.backendManaged || prev[toFullIndex]?.backendManaged) return prev
      const fromScope = queueScopeForTier(prev[fromFullIndex]?.tier)
      const toScope = queueScopeForTier(prev[toFullIndex]?.tier)
      if (fromScope !== toScope) {
        return prev
      }
      const step = fromIndex < toIndex ? 1 : -1
      for (
        let visibleIndex = fromIndex + step;
        visibleIndex !== toIndex + step;
        visibleIndex += step
      ) {
        const traversedFullIndex = visibleIndices[visibleIndex]
        if (traversedFullIndex === undefined) {
          return prev
        }
        if (queueScopeForTier(prev[traversedFullIndex]?.tier) !== fromScope) {
          return prev
        }
      }
      if (prev[toFullIndex]?.backendManaged) {
        return prev
      }

      const reorderedVisibleItems = visibleIndices.map((fullIndex) => prev[fullIndex]!)
      const [item] = reorderedVisibleItems.splice(fromIndex, 1)
      if (!item) return prev
      reorderedVisibleItems.splice(toIndex, 0, item)

      const next = [...prev]
      visibleIndices.forEach((fullIndex, visibleIndex) => {
        next[fullIndex] = reorderedVisibleItems[visibleIndex]!
      })

      return next
    })
  }

  function editInQueue(index: number, newContent: string, scope: QueueMutationScope = 'all'): void {
    setMessageQueue((prev) => {
      const visibleIndices = visibleQueueIndicesForCurrentSession(prev, scope)
      if (index < 0 || index >= visibleIndices.length) return prev

      const fullIndex = visibleIndices[index]
      if (fullIndex === undefined || prev[fullIndex]?.backendManaged) return prev

      return prev.map((item, i) => (i === fullIndex ? { ...item, content: newContent } : item))
    })
  }

  function clearQueue(force?: boolean, sessionId?: string): void {
    const targetSessionId = sessionId ?? session.currentSession()?.id
    const matchesTargetSession = (item: QueuedMessage): boolean =>
      !targetSessionId || !item.sessionId || item.sessionId === targetSessionId

    setMessageQueue((prev) => {
      if (force) {
        return prev.filter((item) => !matchesTargetSession(item))
      }

      return prev.filter((item) => !matchesTargetSession(item) || item.backendManaged === true)
    })
  }

  async function retryMessage(assistantMessageId: string): Promise<void> {
    if (currentSessionHasActiveRun()) return
    log.info('agent', 'Retrying last message', { assistantMessageId })
    const initiatingSessionId = session.currentSession()?.id ?? null

    if (!(await ensureDesktopSessionReady(assistantMessageId))) {
      return
    }
    if (!sessionOwnershipStillCurrent(initiatingSessionId, 'retry', assistantMessageId)) {
      return
    }

    const replaySessionId = session.currentSession()?.id
    if (!replaySessionId) {
      return
    }

    // ── 1. Reset agent UI state via settlement service ───────────────
    resetAgentUiState(
      {
        setCurrentThought,
        setDoomLoopDetected,
        setToolActivity,
        setStreamingTokenEstimate,
        setStreamingStartedAt,
        streaming,
      },
      Date.now()
    )

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
      const result = await rustAgent.retryRun(replaySessionId)
      const errorText = rustAgent.error()

      // Check for detached session (off-screen completion)
      const detachedSessionId =
        result?.detachedSessionId ??
        (typeof rustAgent.detachedSessionId === 'function' ? rustAgent.detachedSessionId() : null)
      if (!errorText && detachedSessionId === replaySessionId) {
        const wasDetachedSettlement = await settleDetachedSessionResult(
          runGuard,
          detachedSessionId,
          replaySessionId,
          {
            content: rustAgent.streamingContent(),
            thinking: rustAgent.thinkingContent(),
            segments: rustAgent.thinkingSegments(),
            toolCalls: rustAgent.activeToolCalls(),
            contentOffset: streaming.streamingContentOffset(),
            segmentsOffset: streaming.thinkingSegmentsOffset(),
            toolCallsOffset: streaming.toolCallsOffset(),
            tokensUsed: rustAgent.tokenUsage().output,
            costUSD: rustAgent.tokenUsage().cost,
            elapsedMs: Date.now() - runStartedAt,
            provider: selectedProviderId,
            model: selectedModelId,
            mode: isPlanMode() ? 'plan' : 'code',
          },
          assistantMessageId,
          markSessionNeedsAuthoritativeRecovery,
          rustAgent.clearDetachedSessionId
        )
        if (wasDetachedSettlement) {
          runGuard.clearRunUiIfCurrent()
          settleSessionRuntimeCache(replaySessionId)
          return
        }
      }

      if (errorText) {
        if (isCancelledError(errorText)) {
          await settleCancelledAssistantMessage(
            runGuard,
            {
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
            },
            assistantMessageId
          )
          runGuard.clearRunUiIfCurrent()
          return
        }
        await settleErrorAssistantMessage(runGuard, errorText, assistantMessageId)
        runGuard.clearRunUiIfCurrent()
        return
      }

      // ── 4. Settle the assistant response ───────────────────────────
      await settleSuccessAssistantMessage(
        runGuard,
        {
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
        },
        assistantMessageId
      )
      runGuard.clearRunUiIfCurrent()

      // ── 5. Sync from backend in web mode ──────────────────────────
      const sessionId = session.currentSession()?.id ?? ''
      const backendSessionId = result?.sessionId || sessionId
      if (backendSessionId && sessionId && runGuard.canMutateOriginSession()) {
        markActiveSessionSynced(sessionId)
      }
      if (!isTauri() && backendSessionId && runGuard.canMutateOriginSession()) {
        await syncMessagesFromBackend(
          backendSessionId,
          {
            session,
            markActiveSessionSynced,
          },
          {
            originSessionId: sessionId,
            assistantPayload: {
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
            },
          }
        )
      }
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Unexpected error in retryMessage', { error: msg })
      await settleErrorAssistantMessage(runGuard, msg, assistantMessageId)
      runGuard.clearRunUiIfCurrent()
    } finally {
      settleSessionRuntimeCache(replaySessionId)
      if (runGuard.runIsCurrent()) {
        batch(() => {
          setStreamingStartedAt(null)
          setLiveMessageId(null)
        })
      }
    }
  }

  async function editAndResend(messageId: string, newContent: string): Promise<void> {
    if (currentSessionHasActiveRun()) return
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

    // ── 2. Reset agent UI state via settlement service ───────────────
    resetAgentUiState(
      {
        setCurrentThought,
        setDoomLoopDetected,
        setToolActivity,
        setStreamingTokenEstimate,
        setStreamingStartedAt,
        streaming,
      },
      Date.now()
    )

    // Ensure a session exists
    let currentSess = session.currentSession()
    if (!currentSess) {
      await session.createNewSession()
      currentSess = session.currentSession()
    }
    const sessionId = currentSess?.id ?? ''
    if (!sessionId) {
      return
    }
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
      const replaySessionId = session.currentSession()?.id
      if (!replaySessionId) {
        throw new Error('No active session for edit and resend')
      }
      const result = await rustAgent.editAndResendRun(messageId, newContent, replaySessionId)
      const errorText = rustAgent.error()

      // Check for detached session (off-screen completion)
      const detachedSessionId =
        result?.detachedSessionId ??
        (typeof rustAgent.detachedSessionId === 'function' ? rustAgent.detachedSessionId() : null)
      if (!errorText && detachedSessionId === replaySessionId) {
        const wasDetachedSettlement = await settleDetachedSessionResult(
          runGuard,
          detachedSessionId,
          replaySessionId,
          {
            content: rustAgent.streamingContent(),
            thinking: rustAgent.thinkingContent(),
            segments: rustAgent.thinkingSegments(),
            toolCalls: rustAgent.activeToolCalls(),
            contentOffset: streaming.streamingContentOffset(),
            segmentsOffset: streaming.thinkingSegmentsOffset(),
            toolCallsOffset: streaming.toolCallsOffset(),
            tokensUsed: rustAgent.tokenUsage().output,
            costUSD: rustAgent.tokenUsage().cost,
            elapsedMs: Date.now() - runStartedAt,
            provider: selectedProviderId,
            model: selectedModelId,
            mode: isPlanMode() ? 'plan' : 'code',
          },
          assistantMsgId,
          markSessionNeedsAuthoritativeRecovery,
          rustAgent.clearDetachedSessionId
        )
        if (wasDetachedSettlement) {
          runGuard.clearRunUiIfCurrent()
          settleSessionRuntimeCache(replaySessionId)
          return
        }
      }

      if (errorText) {
        if (isCancelledError(errorText)) {
          await settleCancelledAssistantMessage(
            runGuard,
            {
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
            },
            assistantMsgId
          )
          runGuard.clearRunUiIfCurrent()
          return
        }
        if (!isTauri() && originalMessages) {
          restoreMessagesAndSetError(session, originalMessages, messageId, errorText)
        } else {
          await settleErrorAssistantMessage(runGuard, errorText, assistantMsgId)
        }
        runGuard.clearRunUiIfCurrent()
        return
      }

      // Settle the assistant response
      await settleSuccessAssistantMessage(
        runGuard,
        {
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
        },
        assistantMsgId
      )
      runGuard.clearRunUiIfCurrent()

      // Sync from backend in web mode
      const sessionId = session.currentSession()?.id ?? ''
      const backendSessionId = result?.sessionId || sessionId
      if (backendSessionId && sessionId && runGuard.canMutateOriginSession()) {
        markActiveSessionSynced(sessionId)
      }
      if (!isTauri() && backendSessionId && runGuard.canMutateOriginSession()) {
        await syncMessagesFromBackend(
          backendSessionId,
          {
            session,
            markActiveSessionSynced,
          },
          {
            originSessionId: sessionId,
            assistantPayload: {
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
            },
          }
        )
      }
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Unexpected error in editAndResend', { error: msg })
      if (!isTauri() && originalMessages) {
        restoreMessagesAndSetError(session, originalMessages, messageId, msg)
      } else {
        await settleErrorAssistantMessage(runGuard, `**Error:** ${msg}`, assistantMsgId)
      }
      runGuard.clearRunUiIfCurrent()
    } finally {
      settleSessionRuntimeCache(sessionId)
      if (runGuard.runIsCurrent()) {
        batch(() => {
          setStreamingStartedAt(null)
          setLiveMessageId(null)
        })
      }
    }
  }

  async function regenerateResponse(assistantMessageId: string): Promise<void> {
    if (currentSessionHasActiveRun()) return
    log.info('agent', 'Regenerating response', { assistantMessageId })
    const initiatingSessionId = session.currentSession()?.id ?? null

    if (!(await ensureDesktopSessionReady(assistantMessageId))) {
      return
    }
    if (!sessionOwnershipStillCurrent(initiatingSessionId, 'regenerate', assistantMessageId)) {
      return
    }

    const replaySessionId = session.currentSession()?.id
    if (!replaySessionId) {
      return
    }

    // ── 1. Reset agent UI state via settlement service ───────────────
    resetAgentUiState(
      {
        setCurrentThought,
        setDoomLoopDetected,
        setToolActivity,
        setStreamingTokenEstimate,
        setStreamingStartedAt,
        streaming,
      },
      Date.now()
    )

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
      const result = await rustAgent.regenerateRun(replaySessionId)
      const errorText = rustAgent.error()

      // Check for detached session (off-screen completion)
      const detachedSessionId =
        result?.detachedSessionId ??
        (typeof rustAgent.detachedSessionId === 'function' ? rustAgent.detachedSessionId() : null)
      if (!errorText && detachedSessionId === replaySessionId) {
        const wasDetachedSettlement = await settleDetachedSessionResult(
          runGuard,
          detachedSessionId,
          replaySessionId,
          {
            content: rustAgent.streamingContent(),
            thinking: rustAgent.thinkingContent(),
            segments: rustAgent.thinkingSegments(),
            toolCalls: rustAgent.activeToolCalls(),
            contentOffset: streaming.streamingContentOffset(),
            segmentsOffset: streaming.thinkingSegmentsOffset(),
            toolCallsOffset: streaming.toolCallsOffset(),
            tokensUsed: rustAgent.tokenUsage().output,
            costUSD: rustAgent.tokenUsage().cost,
            elapsedMs: Date.now() - runStartedAt,
            provider: selectedProviderId,
            model: selectedModelId,
            mode: isPlanMode() ? 'plan' : 'code',
          },
          assistantMessageId,
          markSessionNeedsAuthoritativeRecovery,
          rustAgent.clearDetachedSessionId
        )
        if (wasDetachedSettlement) {
          runGuard.clearRunUiIfCurrent()
          settleSessionRuntimeCache(replaySessionId)
          return
        }
      }

      if (errorText) {
        if (isCancelledError(errorText)) {
          await settleCancelledAssistantMessage(
            runGuard,
            {
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
            },
            assistantMessageId
          )
          runGuard.clearRunUiIfCurrent()
          return
        }
        await settleErrorAssistantMessage(runGuard, errorText, assistantMessageId)
        runGuard.clearRunUiIfCurrent()
        return
      }

      // ── 4. Settle the assistant response ───────────────────────────
      await settleSuccessAssistantMessage(
        runGuard,
        {
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
        },
        assistantMessageId
      )
      runGuard.clearRunUiIfCurrent()

      // ── 5. Sync from backend in web mode ──────────────────────────
      const sessionId = session.currentSession()?.id ?? ''
      const backendSessionId = result?.sessionId || sessionId
      if (backendSessionId && sessionId && runGuard.canMutateOriginSession()) {
        markActiveSessionSynced(sessionId)
      }
      if (!isTauri() && backendSessionId && runGuard.canMutateOriginSession()) {
        await syncMessagesFromBackend(
          backendSessionId,
          {
            session,
            markActiveSessionSynced,
          },
          {
            originSessionId: sessionId,
            assistantPayload: {
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
            },
          }
        )
      }
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Unexpected error in regenerateResponse', { error: msg })
      await settleErrorAssistantMessage(runGuard, msg, assistantMessageId)
      runGuard.clearRunUiIfCurrent()
    } finally {
      settleSessionRuntimeCache(replaySessionId)
      if (runGuard.runIsCurrent()) {
        batch(() => {
          setStreamingStartedAt(null)
          setLiveMessageId(null)
        })
      }
    }
  }

  async function undoLastEdit(): Promise<{ success: boolean; message: string }> {
    const result = await rustBackend.undoLastEdit({
      runId: currentSessionRunId() ?? undefined,
      sessionId: session.currentSession()?.id ?? undefined,
    })
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
