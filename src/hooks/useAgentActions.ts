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
import { getCoreBudget } from '../services/core-bridge'
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
  } = deps

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
        batch(() => {
          session.updateMessage(assistantMsgId, {
            content: '',
            error: { type: 'unknown', message: errorText, timestamp: Date.now() },
          })
          rustAgent.endRun()
        })
        return
      }

      // Settle the assistant response
      const content = rustAgent.streamingContent()
      const elapsedMs = Date.now() - runStartedAt
      const thinking = rustAgent.thinkingContent()
      const segments = rustAgent.thinkingSegments()

      batch(() => {
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
        rustAgent.endRun()
        setLiveMessageId(null)
        setStreamingStartedAt(null)
      })

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
      batch(() => {
        session.updateMessage(assistantMsgId, {
          content: `**Error:** ${msg}`,
          error: { type: 'unknown', message: msg, timestamp: Date.now() },
        })
        rustAgent.endRun()
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
