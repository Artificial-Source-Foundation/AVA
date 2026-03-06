/**
 * Message Actions
 *
 * Higher-level message operations that build on top of the streaming executor:
 * retry, edit-and-resend, regenerate, and undo. Also contains the shared
 * _regenerate helper that creates a fresh assistant turn from the last user
 * message while optionally excluding specific message IDs.
 */

import type { AgentEvent, AgentResult } from '@ava/core-v2/agent'
import { getPlatform } from '@ava/core-v2/platform'
import { batch } from 'solid-js'
import { updateMessage } from '../../services/database'
import { flushLogs, logError, logInfo } from '../../services/logger'
import { buildConversationHistory } from '../chat/history-builder'
import type { ConfigDeps } from './config-builder'
import { execute } from './streaming'
import { createAssistantMessage } from './turn-manager'
import type { AgentRefs, AgentSignals, SessionBridge } from './types'

// ============================================================================
// Dependencies
// ============================================================================

export interface MessageActionDeps {
  signals: AgentSignals
  refs: AgentRefs
  session: SessionBridge
  handleAgentEvent: (event: AgentEvent) => void
  configDeps: ConfigDeps
}

// ============================================================================
// Shared Regeneration Helper
// ============================================================================

/**
 * Shared regeneration logic used by retry, edit-and-resend, and regenerate.
 * Creates a fresh assistant turn from the last user message, optionally
 * excluding specified message IDs from history.
 */
export async function regenerate(
  deps: MessageActionDeps,
  excludeIds?: Set<string>
): Promise<AgentResult | null> {
  const { signals, refs, session, handleAgentEvent, configDeps } = deps

  if (signals.isRunning()) return null

  const sessionId = session.currentSession()?.id
  if (!sessionId) return null

  batch(() => {
    signals.setIsRunning(true)
    signals.setCurrentThought('')
    signals.setLastError(null)
    signals.setError(null)
    signals.setDoomLoopDetected(false)
    signals.setActiveToolCalls([])
    signals.setStreamingTokenEstimate(0)
    signals.setStreamingStartedAt(Date.now())
  })

  refs.abortRef.current = new AbortController()

  try {
    const msgs = session.messages()
    const lastUserMsg = [...msgs].reverse().find((m) => m.role === 'user')
    const goal = lastUserMsg?.content || 'Continue.'

    // Exclude specified IDs + the last user message (goal carries it)
    const allExcluded = new Set(excludeIds)
    if (lastUserMsg) allExcluded.add(lastUserMsg.id)

    const assistantMsg = await createAssistantMessage(sessionId, session)
    allExcluded.add(assistantMsg.id)

    const priorMessages = buildConversationHistory(msgs, allExcluded)
    const model = session.selectedModel()
    session.updateMessage(assistantMsg.id, { model })

    return await execute(goal, sessionId, assistantMsg, priorMessages, model, {
      signals,
      refs,
      session,
      handleAgentEvent,
      configDeps,
    })
  } catch (err) {
    if (err instanceof Error && err.name === 'AbortError') {
      logInfo('Agent', 'Regenerate aborted')
      return null
    }
    const errorMsg = err instanceof Error ? err.message : String(err)
    logError('Agent', 'Regenerate failed', { error: errorMsg })
    void flushLogs()
    batch(() => {
      signals.setLastError(errorMsg)
    })
    return null
  } finally {
    batch(() => {
      signals.setIsRunning(false)
      signals.setStreamingStartedAt(null)
    })
    refs.abortRef.current = null
  }
}

// ============================================================================
// Retry
// ============================================================================

/** Delete the failed assistant message and regenerate from the prior user turn. */
export async function retryMessage(
  deps: MessageActionDeps,
  assistantMessageId: string
): Promise<void> {
  const { session } = deps
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
    await regenerate(deps)
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
  const { session } = deps

  session.updateMessageContent(messageId, newContent)
  await updateMessage(messageId, {
    content: newContent,
    metadata: { editedAt: Date.now() },
  })

  session.deleteMessagesAfter(messageId)
  session.stopEditing()

  logInfo('Agent', 'Edit and resend', { messageId })
  await regenerate(deps)
}

// ============================================================================
// Regenerate Response
// ============================================================================

/** Delete the specified assistant message and regenerate from its preceding user turn. */
export async function regenerateResponse(
  deps: MessageActionDeps,
  assistantMessageId: string
): Promise<void> {
  const { session } = deps
  const msgs = session.messages()
  const index = msgs.findIndex((m) => m.id === assistantMessageId)
  if (index === -1) return

  const userMsg = msgs
    .slice(0, index)
    .reverse()
    .find((m) => m.role === 'user')
  if (!userMsg) return

  session.deleteMessage(assistantMessageId)
  await regenerate(deps)
}

// ============================================================================
// Undo Last Edit
// ============================================================================

/** Revert the most recent [ava]-tagged git commit. */
export async function undoLastEdit(
  configDeps: ConfigDeps
): Promise<{ success: boolean; message: string }> {
  const cwd = configDeps.currentProjectDir()
  if (!cwd) return { success: false, message: 'No project directory' }

  const shell = getPlatform().shell
  const gitCheck = await shell.exec('git rev-parse --is-inside-work-tree', { cwd })
  if (gitCheck.exitCode !== 0) return { success: false, message: 'Not a git repository' }

  const log = await shell.exec('git log --oneline -20', { cwd })
  const lines = log.stdout.split('\n').filter(Boolean)
  const avaLine = lines.find((l) => l.includes('[ava]'))
  if (!avaLine) return { success: false, message: 'No AI edit to undo' }

  const sha = avaLine.split(' ')[0]
  const revert = await shell.exec(`git revert --no-edit ${sha}`, { cwd })
  logInfo('Agent', 'Undo last edit', { success: revert.exitCode === 0 })

  return revert.exitCode === 0
    ? { success: true, message: `Reverted last AI edit: ${avaLine}` }
    : { success: false, message: revert.stderr || 'Revert failed' }
}
