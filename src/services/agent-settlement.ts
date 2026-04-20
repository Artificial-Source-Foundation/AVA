/**
 * Agent Settlement Service — Transport-agnostic run settlement and backend sync
 *
 * Centralizes the duplicated settlement patterns from useAgentActions.ts:
 * - Run guard lifecycle (beginRunGuard pattern)
 * - Message settlement (cancelled/error/success paths)
 * - Web backend synchronization
 *
 * This is a thin coordination layer over rust-agent-ipc (runtime correlation owner)
 * and core-bridge (session sync owner). It does not own state; it coordinates
 * settlement actions across existing owners.
 */

import { isTauri } from '@tauri-apps/api/core'
import { type Accessor, batch, type Setter } from 'solid-js'
import type { ToolActivity } from '../hooks/agent/agent-types'
import { apiFetch } from '../lib/api-client'
import { log } from '../lib/logger'
import { mapWebSessionMessages } from '../lib/web-session-messages'
import type { Message, MessageError } from '../types'
import { registerBackendSessionId } from './web-session-identity'

// ═════════════════════════════════════════════════════════════════════════════
// Types
// ═════════════════════════════════════════════════════════════════════════════

export interface RunGuard {
  readonly runToken: number
  runIsCurrent: () => boolean
  ownsOriginSession: () => boolean
  canMutateOriginSession: () => boolean
  settleMessageId: (fallbackId: string) => string
  updateMessage: (messageId: string, updates: Partial<Message>) => void
  deleteMessage: (messageId: string) => Promise<void>
  clearRunUiIfCurrent: () => void
}

export interface RunGuardContext {
  sessionId: string
  session: {
    currentSession: () => { id: string } | null
    updateMessage: (messageId: string, updates: Partial<Message>) => void
    updateMessageInSession?: (
      sessionId: string,
      messageId: string,
      updates: Partial<Message>
    ) => void
    deleteMessage: (messageId: string) => Promise<void>
    deleteMessageInSession?: (sessionId: string, messageId: string) => Promise<void>
  }
  liveMessageId: Accessor<string | null>
  setLiveMessageId: Setter<string | null>
  setStreamingStartedAt: Setter<number | null>
  runOwnership: {
    beginRun: () => number
    isCurrentRun: (token: number) => boolean
  }
  rustAgent: {
    endRun: () => void
  }
}

export interface SettlementResult {
  readonly success: boolean
  readonly cancelled?: boolean
  readonly errorMessage?: string
}

export interface AssistantSettlementPayload {
  content: string
  thinking: string
  segments: Array<{ thinking: string; toolCallIds: string[] }>
  toolCalls: Array<unknown>
  tokensUsed: number
  costUSD: number
  elapsedMs: number
  provider?: string
  model?: string
  mode?: 'plan' | 'code'
  cancelled?: boolean
}

export interface WebSyncDeps {
  session: {
    currentSession: () => { id: string } | null
    replaceMessagesFromBackend: (messages: Message[]) => void
    setMessages?: (messages: Message[]) => void
    setMessageError?: (messageId: string, error: MessageError | null) => void
    messages?: () => Message[]
  }
  markActiveSessionSynced?: (sessionId: string, messageCount?: number) => void
}

interface WebSyncOptions {
  originSessionId?: string
  assistantPayload?: AssistantSettlementPayload
}

// ═════════════════════════════════════════════════════════════════════════════
// Run Guard Factory (extracted from useAgentActions.ts beginRunGuard)
// ═════════════════════════════════════════════════════════════════════════════

export function createRunGuard(context: RunGuardContext): RunGuard {
  const {
    sessionId,
    session,
    liveMessageId,
    setLiveMessageId,
    setStreamingStartedAt,
    runOwnership,
    rustAgent,
  } = context

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

// ═════════════════════════════════════════════════════════════════════════════
// Settlement Predicates (extracted from useAgentActions.ts)
// ═════════════════════════════════════════════════════════════════════════════

export function shouldPreserveAssistantCompletion(options: {
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

export function isCancelledError(errorText: string | null): boolean {
  if (!errorText) return false
  return errorText === 'Agent run cancelled by user' || errorText.includes('cancelled by user')
}

// ═════════════════════════════════════════════════════════════════════════════
// Settlement Actions (extracted from useAgentActions.ts settle patterns)
// ═════════════════════════════════════════════════════════════════════════════

export async function settleCancelledAssistantMessage(
  runGuard: RunGuard,
  payload: AssistantSettlementPayload,
  messageId: string
): Promise<void> {
  const finalMsgId = runGuard.settleMessageId(messageId)

  if (shouldPreserveAssistantCompletion(payload)) {
    runGuard.updateMessage(finalMsgId, {
      content: payload.content,
      tokensUsed: payload.tokensUsed,
      costUSD: payload.costUSD || undefined,
      toolCalls: payload.toolCalls as unknown as Message['toolCalls'],
      metadata: {
        provider: payload.provider,
        model: payload.model,
        mode: payload.mode ?? 'code',
        elapsedMs: payload.elapsedMs,
        cancelled: true,
        ...(payload.thinking ? { thinking: payload.thinking } : {}),
        ...(payload.segments.length > 1 ? { thinkingSegments: payload.segments } : {}),
      },
    })
  } else {
    // Await deletion as part of settlement semantics
    await runGuard.deleteMessage(finalMsgId)
  }
}

export async function settleErrorAssistantMessage(
  runGuard: RunGuard,
  errorMessage: string,
  messageId: string
): Promise<void> {
  const finalMsgId = runGuard.settleMessageId(messageId)
  runGuard.updateMessage(finalMsgId, {
    content: '',
    error: { type: 'unknown', message: errorMessage, timestamp: Date.now() },
  })
}

export async function settleSuccessAssistantMessage(
  runGuard: RunGuard,
  payload: AssistantSettlementPayload,
  messageId: string
): Promise<void> {
  const finalMsgId = runGuard.settleMessageId(messageId)

  if (shouldPreserveAssistantCompletion(payload)) {
    runGuard.updateMessage(finalMsgId, {
      content: payload.content,
      tokensUsed: payload.tokensUsed,
      costUSD: payload.costUSD || undefined,
      toolCalls: payload.toolCalls as unknown as Message['toolCalls'],
      metadata: {
        provider: payload.provider,
        model: payload.model,
        mode: payload.mode ?? 'code',
        elapsedMs: payload.elapsedMs,
        ...(payload.thinking ? { thinking: payload.thinking } : {}),
        ...(payload.segments.length > 1 ? { thinkingSegments: payload.segments } : {}),
      },
    })
  } else {
    // Await deletion as part of settlement semantics
    await runGuard.deleteMessage(finalMsgId)
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Detached Session Settlement (for off-screen completions in replay flows)
// ═════════════════════════════════════════════════════════════════════════════

export interface DetachedSettlementPayload {
  content: string
  thinking: string
  segments: Array<{ thinking: string; toolCallIds: string[] }>
  toolCalls: Array<unknown>
  contentOffset: number
  segmentsOffset: number
  toolCallsOffset: number
  tokensUsed: number
  costUSD: number
  elapsedMs: number
  provider?: string
  model?: string
  mode?: 'plan' | 'code'
}

/**
 * Settle a detached session result (off-screen completion).
 * Returns true if settlement was performed (detached session matched).
 */
export async function settleDetachedSessionResult(
  runGuard: RunGuard,
  detachedSessionId: string | null | undefined,
  currentSessionId: string,
  payload: DetachedSettlementPayload,
  messageId: string,
  markSessionNeedsAuthoritativeRecovery?: (sessionId: string) => void,
  clearDetachedSessionId?: () => void
): Promise<boolean> {
  if (detachedSessionId !== currentSessionId) {
    return false
  }

  // Mark for authoritative recovery (Tauri only)
  if (markSessionNeedsAuthoritativeRecovery) {
    markSessionNeedsAuthoritativeRecovery(currentSessionId)
  }

  const finalMsgId = runGuard.settleMessageId(messageId)
  const partialContent =
    payload.contentOffset > 0 ? payload.content.slice(payload.contentOffset) : payload.content
  const partialSegments =
    payload.segmentsOffset > 0 ? payload.segments.slice(payload.segmentsOffset) : payload.segments
  const partialToolCalls =
    payload.toolCallsOffset > 0
      ? payload.toolCalls.slice(payload.toolCallsOffset)
      : payload.toolCalls

  if (
    shouldPreserveAssistantCompletion({
      content: partialContent,
      thinking: payload.thinking,
      segments: partialSegments,
      toolCalls: partialToolCalls,
    })
  ) {
    runGuard.updateMessage(finalMsgId, {
      content: partialContent,
      tokensUsed: payload.tokensUsed,
      costUSD: payload.costUSD || undefined,
      toolCalls: partialToolCalls as unknown as Message['toolCalls'],
      metadata: {
        provider: payload.provider,
        model: payload.model,
        mode: payload.mode ?? 'code',
        elapsedMs: payload.elapsedMs,
        ...(payload.thinking ? { thinking: payload.thinking } : {}),
        ...(partialSegments.length > 1 ? { thinkingSegments: partialSegments } : {}),
      },
    })
  } else {
    await runGuard.deleteMessage(finalMsgId)
  }

  clearDetachedSessionId?.()
  return true
}

// ═════════════════════════════════════════════════════════════════════════════
// Web Backend Sync (extracted duplicated fetch/sync from retry/edit/regenerate)
// ═════════════════════════════════════════════════════════════════════════════

function buildPersistedAssistantMetadata(
  payload: AssistantSettlementPayload,
  existing: Record<string, unknown> | undefined
): Record<string, unknown> {
  return {
    ...(existing ?? {}),
    ...(payload.thinking ? { thinking: payload.thinking } : {}),
    ...(payload.segments.length > 1 ? { thinkingSegments: payload.segments } : {}),
    ...(payload.toolCalls.length > 0 ? { toolCalls: payload.toolCalls } : {}),
    ...(payload.provider ? { provider: payload.provider } : {}),
    ...(payload.model ? { model: payload.model } : {}),
    ...(payload.mode ? { mode: payload.mode } : {}),
    elapsedMs: payload.elapsedMs,
  }
}

async function persistLatestAssistantMessage(
  backendSessionId: string,
  rawMessages: Array<Record<string, unknown>>,
  payload: AssistantSettlementPayload
): Promise<Array<Record<string, unknown>>> {
  const targetIndex = [...rawMessages]
    .map((message, index) => ({ message, index }))
    .reverse()
    .find(({ message }) => message.role === 'assistant')?.index

  if (targetIndex === undefined) {
    return rawMessages
  }

  const target = rawMessages[targetIndex]!
  const messageId = typeof target.id === 'string' ? target.id : null
  if (!messageId) {
    return rawMessages
  }

  const metadataBase =
    target.metadata && typeof target.metadata === 'object' && !Array.isArray(target.metadata)
      ? (target.metadata as Record<string, unknown>)
      : undefined
  const metadata = buildPersistedAssistantMetadata(payload, metadataBase)

  try {
    const response = await apiFetch(`/api/sessions/${backendSessionId}/messages/${messageId}`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        content: target.content,
        metadata,
        tokens_used: payload.tokensUsed,
        cost_usd: payload.costUSD || null,
        model: payload.model ?? null,
      }),
    })

    if (!response.ok) {
      log.warn('agent-settlement', 'Failed to persist enriched assistant metadata', {
        backendSessionId,
        messageId,
        status: response.status,
      })
      return rawMessages
    }

    return rawMessages.map((message, index) =>
      index === targetIndex
        ? {
            ...message,
            metadata,
            tool_calls: payload.toolCalls,
            tokens_used: payload.tokensUsed,
            cost_usd: payload.costUSD || null,
            model: payload.model ?? null,
          }
        : message
    )
  } catch (error) {
    log.warn('agent-settlement', 'Failed to persist enriched assistant metadata', {
      backendSessionId,
      messageId,
      error: String(error),
    })
    return rawMessages
  }
}

export async function persistAssistantPayloadToBackendSession(
  backendSessionId: string,
  payload: AssistantSettlementPayload
): Promise<boolean> {
  if (isTauri() || !backendSessionId) {
    return false
  }

  try {
    const response = await apiFetch(`/api/sessions/${backendSessionId}/messages`)
    if (!response.ok) {
      return false
    }

    const rawMessages = (await response.json()) as Array<Record<string, unknown>>
    const persistedMessages = await persistLatestAssistantMessage(
      backendSessionId,
      rawMessages,
      payload
    )
    return persistedMessages !== rawMessages
  } catch {
    return false
  }
}

export async function syncMessagesFromBackend(
  backendSessionId: string,
  deps: WebSyncDeps,
  options: WebSyncOptions = {}
): Promise<boolean> {
  if (isTauri() || !backendSessionId) {
    return false
  }

  const { session, markActiveSessionSynced } = deps
  const originSessionId = options.originSessionId ?? session.currentSession()?.id ?? ''

  try {
    const res = await apiFetch(`/api/sessions/${backendSessionId}/messages`)

    if (!res.ok) {
      log.warn('agent-settlement', 'Backend sync fetch failed', {
        backendSessionId,
        status: res.status,
      })
      return false
    }

    let rawMsgs = (await res.json()) as Array<Record<string, unknown>>
    if (options.assistantPayload) {
      rawMsgs = await persistLatestAssistantMessage(
        backendSessionId,
        rawMsgs,
        options.assistantPayload
      )
    }

    const backendMsgs: Message[] = mapWebSessionMessages(rawMsgs, originSessionId)

    session.replaceMessagesFromBackend(backendMsgs)
    registerBackendSessionId(originSessionId, backendSessionId)

    if (markActiveSessionSynced && originSessionId) {
      markActiveSessionSynced(originSessionId, backendMsgs.length)
    }

    log.info('agent-settlement', 'Synced messages from backend', {
      backendSessionId,
      messageCount: backendMsgs.length,
    })
    return true
  } catch (syncErr) {
    log.warn('agent-settlement', 'Failed to sync messages from backend', {
      backendSessionId,
      error: String(syncErr),
    })
    return false
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Error Recovery for Web Edit Operations
// ═════════════════════════════════════════════════════════════════════════════

export function restoreMessagesAndSetError(
  session: {
    setMessages?: (messages: Message[]) => void
    setMessageError?: (messageId: string, error: MessageError | null) => void
    messages?: () => Message[]
  },
  originalMessages: Message[] | null,
  messageId: string,
  errorMessage: string
): void {
  if (!isTauri() && originalMessages && session.setMessages) {
    session.setMessages(originalMessages)
    session.setMessageError?.(messageId, {
      type: 'unknown',
      message: errorMessage,
      timestamp: Date.now(),
    })
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Session Runtime Cache Coordination (thin facade over existing owners)
// ═════════════════════════════════════════════════════════════════════════════

export interface RuntimeCacheCoordinator {
  markSettled: (sessionId: string) => void
  canMutate: (sessionId: string, runGuard: RunGuard) => boolean
}

export function createRuntimeCacheCoordinator(
  onSessionSettled?: (sessionId: string) => void,
  markActiveSessionSynced?: (sessionId: string, messageCount?: number) => void
): RuntimeCacheCoordinator {
  return {
    markSettled: (sessionId: string): void => {
      onSessionSettled?.(sessionId)
      markActiveSessionSynced?.(sessionId)
    },
    canMutate: (_sessionId: string, runGuard: RunGuard): boolean => {
      return runGuard.canMutateOriginSession()
    },
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Session Ownership Checks (extracted from useAgentActions.ts)
// ═════════════════════════════════════════════════════════════════════════════

export function sessionOwnershipStillCurrent(
  initiatingSessionId: string | null,
  currentSessionId: string | null,
  action: 'retry' | 'edit-and-resend' | 'regenerate',
  messageId: string
): boolean {
  if (!initiatingSessionId) {
    return true
  }

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

// ═════════════════════════════════════════════════════════════════════════════
// UI State Reset Helpers (extracted batch operations)
// ═════════════════════════════════════════════════════════════════════════════

export interface UiResetSignals {
  setCurrentThought: Setter<string>
  setDoomLoopDetected: Setter<boolean>
  setToolActivity: Setter<ToolActivity[]>
  setStreamingTokenEstimate: Setter<number>
  setStreamingStartedAt: Setter<number | null>
  streaming: {
    setStreamingContentOffset: (v: number) => void
    setToolCallsOffset: (v: number) => void
    setThinkingSegmentsOffset: (v: number) => void
  }
}

export function resetAgentUiState(ui: UiResetSignals, startTime?: number): void {
  batch(() => {
    ui.setCurrentThought('')
    ui.setDoomLoopDetected(false)
    ui.setToolActivity([])
    ui.setStreamingTokenEstimate(0)
    ui.setStreamingStartedAt(startTime ?? Date.now())
    ui.streaming.setStreamingContentOffset(0)
    ui.streaming.setToolCallsOffset(0)
    ui.streaming.setThinkingSegmentsOffset(0)
  })
}
