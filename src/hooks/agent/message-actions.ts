/**
 * Message Actions — DEPRECATED
 *
 * Higher-level message operations (retry, edit-and-resend, regenerate, undo)
 * are now handled by the Rust backend. This module is retained for type
 * compatibility only.
 */

import { invoke } from '@tauri-apps/api/core'
import { batch } from 'solid-js'
import { updateMessage } from '../../services/database'
import { logError, logInfo } from '../../services/logger'
import type { ConfigDeps } from './config-builder'
import type { AgentRefs, AgentSignals, SessionBridge } from './types'

// ============================================================================
// Dependencies
// ============================================================================

export interface MessageActionDeps {
  signals: AgentSignals
  refs: AgentRefs
  session: SessionBridge
  handleAgentEvent: (event: unknown) => void
  configDeps: ConfigDeps
}

// ============================================================================
// Retry
// ============================================================================

/** Delete the failed assistant message and regenerate from the prior user turn. */
export async function retryMessage(
  deps: MessageActionDeps,
  assistantMessageId: string
): Promise<void> {
  const { session, signals } = deps
  const msgs = session.messages()
  const failedIndex = msgs.findIndex((m) => m.id === assistantMessageId)
  if (failedIndex === -1) return

  const userMsg = msgs
    .slice(0, failedIndex)
    .reverse()
    .find((m) => m.role === 'user')
  if (!userMsg) return

  session.setRetryingMessageId(assistantMessageId)
  session.setMessageError(assistantMessageId, null)
  session.deleteMessage(assistantMessageId)
  logInfo('Agent', 'Retry message', { messageId: assistantMessageId })

  try {
    // Delegate to Rust backend
    await invoke('submit_goal', {
      args: { goal: userMsg.content, maxTurns: 0, provider: null, model: null },
    })
  } catch (err) {
    const errorMsg = err instanceof Error ? err.message : String(err)
    logError('Agent', 'Retry failed', { error: errorMsg })
    batch(() => {
      signals.setLastError(errorMsg)
    })
  } finally {
    session.setRetryingMessageId(null)
  }
}

// ============================================================================
// Edit and Resend
// ============================================================================

/** Update a message's content, delete everything after it, and regenerate. */
export async function editAndResend(
  deps: MessageActionDeps,
  messageId: string,
  newContent: string
): Promise<void> {
  const { session, signals } = deps

  session.updateMessageContent(messageId, newContent)
  await updateMessage(messageId, {
    content: newContent,
    metadata: { editedAt: Date.now() },
  })

  session.deleteMessagesAfter(messageId)
  session.stopEditing()

  logInfo('Agent', 'Edit and resend', { messageId })

  try {
    await invoke('submit_goal', {
      args: { goal: newContent, maxTurns: 0, provider: null, model: null },
    })
  } catch (err) {
    const errorMsg = err instanceof Error ? err.message : String(err)
    logError('Agent', 'Edit and resend failed', { error: errorMsg })
    batch(() => {
      signals.setLastError(errorMsg)
    })
  }
}

// ============================================================================
// Regenerate Response
// ============================================================================

/** Delete the specified assistant message and regenerate from its preceding user turn. */
export async function regenerateResponse(
  deps: MessageActionDeps,
  assistantMessageId: string
): Promise<void> {
  const { session, signals } = deps
  const msgs = session.messages()
  const index = msgs.findIndex((m) => m.id === assistantMessageId)
  if (index === -1) return

  const userMsg = msgs
    .slice(0, index)
    .reverse()
    .find((m) => m.role === 'user')
  if (!userMsg) return

  session.deleteMessage(assistantMessageId)

  try {
    await invoke('submit_goal', {
      args: { goal: userMsg.content, maxTurns: 0, provider: null, model: null },
    })
  } catch (err) {
    const errorMsg = err instanceof Error ? err.message : String(err)
    logError('Agent', 'Regenerate failed', { error: errorMsg })
    batch(() => {
      signals.setLastError(errorMsg)
    })
  }
}

// ============================================================================
// Undo Last Edit
// ============================================================================

/** Revert the most recent [ava]-tagged git commit via Rust backend. */
export async function undoLastEdit(
  _configDeps: ConfigDeps
): Promise<{ success: boolean; message: string }> {
  try {
    const result = await invoke<{ success: boolean; message: string }>('undo_last_edit')
    return result
  } catch (err) {
    const errorMsg = err instanceof Error ? err.message : String(err)
    return { success: false, message: errorMsg }
  }
}
