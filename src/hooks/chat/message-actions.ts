/**
 * Message Actions
 * Queue management, cancel/steer, retry, edit-and-resend, undo.
 */

import { undoLastAutoCommit } from '@ava/core'
import { updateMessage } from '../../services/database'
import { logDebug, logInfo } from '../../services/logger'
import { regenerate, sendMessage } from './send-message'
import type { ChatDeps } from './types'

// Re-export for convenience
export { regenerate, sendMessage } from './send-message'

// ============================================================================
// Queue Management
// ============================================================================

/** Process the next queued follow-up message */
export async function processQueue(deps: ChatDeps): Promise<void> {
  const queue = deps.messageQueue()
  if (queue.length === 0) return
  const next = queue[0]
  deps.setMessageQueue((prev) => prev.slice(1))
  logDebug(deps.LOG_SRC, 'Dequeue message', { remaining: deps.messageQueue().length })
  await sendMessage(deps, next.content, next.model, next.images, processQueue)
}

/** Cancel ongoing stream and clear any queued messages */
export function cancel(deps: ChatDeps): void {
  deps.abortRef.current?.abort()
  deps.setMessageQueue([])
  deps.setActiveToolCalls([])
  deps.setIsStreaming(false)
  logInfo(deps.LOG_SRC, 'Cancel', {})
}

/**
 * Steer: cancel current stream and send a new message immediately.
 * Clears any queued follow-ups — the steer message takes priority.
 */
export function steer(
  deps: ChatDeps,
  content: string,
  model?: string,
  images?: Array<{ data: string; mimeType: string; name?: string }>
): void {
  deps.setMessageQueue([{ content, model, images }])
  deps.abortRef.current?.abort()
  deps.setIsStreaming(false)
  logInfo(deps.LOG_SRC, 'Steer', { queued: 1 })
}

/** Clear all queued messages */
export function clearQueue(deps: ChatDeps): void {
  deps.setMessageQueue([])
  logDebug(deps.LOG_SRC, 'Clear queue')
}

/** Clear error state */
export function clearError(deps: ChatDeps): void {
  deps.setError(null)
}

// ============================================================================
// Retry & Edit
// ============================================================================

/** Retry a failed message */
export async function retryMessage(deps: ChatDeps, assistantMessageId: string): Promise<void> {
  const msgs = deps.session.messages()
  const failedIndex = msgs.findIndex((m) => m.id === assistantMessageId)
  if (failedIndex === -1) return

  // Find preceding user message
  const userMsg = msgs
    .slice(0, failedIndex)
    .reverse()
    .find((m) => m.role === 'user')
  if (!userMsg) return

  // Clear error and mark retrying
  deps.session.setRetryingMessageId(assistantMessageId)
  deps.session.setMessageError(assistantMessageId, null)
  deps.session.deleteMessage(assistantMessageId)
  logInfo(deps.LOG_SRC, 'Retry message', { messageId: assistantMessageId })

  try {
    await regenerate(deps)
  } finally {
    deps.session.setRetryingMessageId(null)
  }
}

/** Edit a user message and resend from that point */
export async function editAndResend(
  deps: ChatDeps,
  messageId: string,
  newContent: string
): Promise<void> {
  deps.session.updateMessageContent(messageId, newContent)
  await updateMessage(messageId, {
    content: newContent,
    metadata: { editedAt: Date.now() },
  })

  deps.session.deleteMessagesAfter(messageId)
  deps.session.stopEditing()

  logInfo(deps.LOG_SRC, 'Edit and resend', { messageId })
  await regenerate(deps)
}

/** Regenerate an assistant response */
export async function regenerateResponse(
  deps: ChatDeps,
  assistantMessageId: string
): Promise<void> {
  const msgs = deps.session.messages()
  const index = msgs.findIndex((m) => m.id === assistantMessageId)
  if (index === -1) return

  // Find preceding user message to validate
  const userMsg = msgs
    .slice(0, index)
    .reverse()
    .find((m) => m.role === 'user')
  if (!userMsg) return

  deps.session.deleteMessage(assistantMessageId)
  await regenerate(deps)
}

// ============================================================================
// Undo
// ============================================================================

/**
 * Undo the last auto-committed AI edit.
 * Finds the most recent ava-prefixed commit and reverts it.
 */
export async function undoLastEdit(deps: ChatDeps): Promise<{ success: boolean; message: string }> {
  const cwd = deps.currentProject()?.directory
  if (!cwd) {
    return { success: false, message: 'No project directory' }
  }
  const result = await undoLastAutoCommit(cwd)
  logInfo(deps.LOG_SRC, 'Undo last edit', { success: result.success })
  return {
    success: result.success,
    message: result.success
      ? `Reverted last AI edit: ${result.output}`
      : result.error || 'No AI edit to undo',
  }
}
