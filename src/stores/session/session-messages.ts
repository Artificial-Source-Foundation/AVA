/**
 * Session Message Actions
 * Add, update, delete, and rollback messages within a session.
 */

import { isTauri } from '@tauri-apps/api/core'
import { log } from '../../lib/logger'
import { mergeMessagesWithExisting, mergeMessageWithBackend } from '../../lib/tool-call-state'
import {
  deleteMessageFromDb as dbDeleteMessage,
  deleteMessagesFromTimestamp as dbDeleteMessagesFromTimestamp,
  deleteSessionMessages as dbDeleteSessionMessages,
  insertMessages as dbInsertMessages,
  updateMessage as dbUpdateMessage,
  getMessages,
} from '../../services/database'
import { logError } from '../../services/logger'
import type { Message, MessageError } from '../../types'
import { createLatestRequestGate } from './request-gate'
import { getCachedSessionArtifacts, updateCachedSessionArtifacts } from './session-artifact-cache'
import {
  currentSession,
  messages,
  setIsLoadingMessages,
  setMessages,
  setSessions,
} from './session-state'

// ============================================================================
// Pending-insert registry
//
// addMessage() fires the DB INSERT asynchronously so it never blocks the UI.
// However updateMessage() and deleteMessage() must not run their DB ops until
// the INSERT for that message ID has settled — otherwise a fast agent could
// issue an UPDATE/DELETE before the row even exists, turning it into a no-op
// and leaving the empty placeholder in the DB forever.
//
// We store the in-flight INSERT promise (resolved to void) keyed by message ID
// and await it inside updateMessage / deleteMessage before touching the DB.
// ============================================================================

const pendingInserts = new Map<string, Promise<void>>()
const loadMessagesGate = createLatestRequestGate()

function updateCachedSessionMessages(
  sessionId: string,
  updater: (messages: Message[]) => Message[]
): void {
  updateCachedSessionArtifacts(sessionId, (snapshot) => ({
    ...snapshot,
    messages: updater(snapshot.messages),
  }))
}

function persistMessageInsert(message: Message): void {
  // In web mode the Rust agent is the single source of truth for persistence.
  // Skip the DB insert here — messages will be loaded from the backend API
  // after the agent run completes. Only write to DB in Tauri (desktop) mode.
  if (!isTauri()) {
    return
  }

  const insertPromise = dbInsertMessages([message])
    .catch((err: unknown) => logError('Session', 'Failed to persist message', err))
    .finally(() => {
      pendingInserts.delete(message.id)
    })
  pendingInserts.set(message.id, insertPromise)
}

function persistMessageUpdate(id: string, updates: Partial<Message>): void {
  // In web mode the Rust agent is the single source of truth for persistence.
  // Skip the DB update — the backend will have the authoritative state.
  // Only write to DB in Tauri (desktop) mode.
  if (!isTauri()) {
    return
  }

  const pending = pendingInserts.get(id)
  const persist = pending
    ? pending.then(() =>
        dbUpdateMessage(id, {
          content: updates.content,
          tokensUsed: updates.tokensUsed,
          costUSD: updates.costUSD,
          toolCalls: updates.toolCalls,
          images: updates.images,
          error: updates.error,
          metadata: updates.metadata,
        })
      )
    : dbUpdateMessage(id, {
        content: updates.content,
        tokensUsed: updates.tokensUsed,
        costUSD: updates.costUSD,
        toolCalls: updates.toolCalls,
        images: updates.images,
        error: updates.error,
        metadata: updates.metadata,
      })
  persist.catch((err: unknown) => logError('Session', 'Failed to persist message update', err))
}

async function persistMessageDelete(id: string): Promise<void> {
  if (!isTauri()) {
    return
  }

  try {
    const pending = pendingInserts.get(id)
    if (pending) await pending
    await dbDeleteMessage(id)
  } catch (err) {
    logError('Session', 'Failed to delete message from DB', err)
  }
}

// ============================================================================
// Message Management
// ============================================================================

export async function loadSessionMessages(sessionId: string): Promise<void> {
  const requestToken = loadMessagesGate.begin()
  setIsLoadingMessages(true)
  try {
    const dbMessages = await getMessages(sessionId)
    if (!loadMessagesGate.isCurrent(requestToken) || currentSession()?.id !== sessionId) {
      return
    }
    setMessages(dbMessages)
  } catch (err) {
    if (!loadMessagesGate.isCurrent(requestToken) || currentSession()?.id !== sessionId) {
      return
    }
    logError('Session', 'Failed to load messages', err)
    setMessages([])
  } finally {
    if (loadMessagesGate.isCurrent(requestToken) && currentSession()?.id === sessionId) {
      setIsLoadingMessages(false)
    }
  }
}

export function addMessage(message: Message): void {
  log.debug('session', 'Message added', {
    id: message.id,
    role: message.role,
    sessionId: message.sessionId,
  })

  persistMessageInsert(message)

  // Update frontend state immediately
  setMessages((prev) => [...prev, message])
  updateCachedSessionMessages(message.sessionId, (prev) => [...prev, message])

  setSessions((prev) =>
    prev.map((s) =>
      s.id === message.sessionId
        ? {
            ...s,
            messageCount: s.messageCount + 1,
            totalTokens: s.totalTokens + (message.tokensUsed || 0),
            lastPreview: message.content.slice(0, 80),
            updatedAt: Date.now(),
          }
        : s
    )
  )
}

/**
 * Persist a message for a known session without mutating whichever session is
 * currently active in the UI. Falls back to normal addMessage behavior when the
 * target session is still active.
 */
export function addMessageToSession(message: Message): void {
  if (currentSession()?.id === message.sessionId) {
    addMessage(message)
    return
  }

  log.debug('session', 'Persisting message for inactive session', {
    id: message.id,
    role: message.role,
    sessionId: message.sessionId,
  })
  persistMessageInsert(message)
  updateCachedSessionMessages(message.sessionId, (prev) => [...prev, message])
}

export function updateMessageContent(id: string, content: string): void {
  let updatedSessionId: string | null = null
  setMessages((prev) =>
    prev.map((msg) => {
      if (msg.id !== id) {
        return msg
      }
      updatedSessionId = msg.sessionId
      return { ...msg, content }
    })
  )
  if (updatedSessionId) {
    updateCachedSessionMessages(updatedSessionId, (prev) =>
      prev.map((msg) => (msg.id === id ? { ...msg, content } : msg))
    )
  }
}

export function updateMessage(id: string, updates: Partial<Message>): void {
  let updatedSessionId: string | null = null
  setMessages((prev) => {
    const idx = prev.findIndex((msg) => msg.id === id)
    if (idx === -1) return prev
    updatedSessionId = prev[idx]?.sessionId ?? null
    const next = prev.slice()
    next[idx] = { ...prev[idx], ...updates }
    return next
  })

  if (updatedSessionId) {
    updateCachedSessionMessages(updatedSessionId, (prev) =>
      prev.map((msg) => (msg.id === id ? { ...msg, ...updates } : msg))
    )
  }

  persistMessageUpdate(id, updates)
}

/**
 * Persist a message update for a known session without mutating the active UI
 * session when the target session has already been switched away from.
 */
export function updateMessageInSession(
  sessionId: string,
  id: string,
  updates: Partial<Message>
): void {
  if (currentSession()?.id === sessionId) {
    updateMessage(id, updates)
    return
  }

  log.debug('session', 'Persisting message update for inactive session', { id, sessionId })
  updateCachedSessionMessages(sessionId, (prev) =>
    prev.map((msg) => (msg.id === id ? { ...msg, ...updates } : msg))
  )
  persistMessageUpdate(id, updates)
}

export function setMessageError(messageId: string, error: MessageError | null): void {
  let updatedSessionId: string | null = null
  setMessages((prev) =>
    prev.map((msg) => {
      if (msg.id !== messageId) {
        return msg
      }
      updatedSessionId = msg.sessionId
      return { ...msg, error: error || undefined }
    })
  )
  if (updatedSessionId) {
    updateCachedSessionMessages(updatedSessionId, (prev) =>
      prev.map((msg) => (msg.id === messageId ? { ...msg, error: error || undefined } : msg))
    )
  }
}

export async function deleteMessage(id: string): Promise<void> {
  log.debug('session', 'Message deleted', { id })
  let deletedSessionId: string | null = null
  setMessages((prev) => {
    const match = prev.find((msg) => msg.id === id)
    deletedSessionId = match?.sessionId ?? deletedSessionId
    return prev.filter((msg) => msg.id !== id)
  })
  if (deletedSessionId) {
    updateCachedSessionMessages(deletedSessionId, (prev) => prev.filter((msg) => msg.id !== id))
  }
  await persistMessageDelete(id)
}

/**
 * Delete a message for a known session without mutating the currently active
 * session's in-memory message list when the target session is no longer open.
 */
export async function deleteMessageInSession(sessionId: string, id: string): Promise<void> {
  if (currentSession()?.id === sessionId) {
    await deleteMessage(id)
    return
  }

  log.debug('session', 'Persisting message delete for inactive session', { id, sessionId })
  updateCachedSessionMessages(sessionId, (prev) => prev.filter((msg) => msg.id !== id))
  await persistMessageDelete(id)
}

export function deleteMessagesAfter(messageId: string): void {
  const prev = messages()
  const index = prev.findIndex((m) => m.id === messageId)
  if (index < 0) return

  // Collect IDs of messages to be removed (everything after the target)
  const removedMessages = prev.slice(index + 1)

  // Update in-memory state immediately
  setMessages(prev.slice(0, index + 1))

  // Persist deletions to the database (Tauri mode only — in web mode the
  // backend handles its own session truncation via the edit-resend endpoint).
  if (isTauri() && removedMessages.length > 0) {
    // Delete each removed message from the DB, respecting pending inserts
    for (const msg of removedMessages) {
      const pending = pendingInserts.get(msg.id)
      const doDelete = pending
        ? pending.then(() => dbDeleteMessage(msg.id))
        : dbDeleteMessage(msg.id)
      doDelete.catch((err: unknown) =>
        logError('Session', 'Failed to delete message from DB in deleteMessagesAfter', err)
      )
    }
  }
}

/**
 * Replace the entire in-memory message list with the authoritative list from
 * the backend. Used in web mode after a run completes to sync the store with
 * what the Rust agent actually persisted.
 */
export function replaceMessagesFromBackend(msgs: Message[]): void {
  const current = messages()
  const recoveredMessages = mergeMessagesWithExisting(current, msgs)

  // Preserve locally-added tier messages (steering/follow-up/post-complete)
  // that the backend doesn't know about. These are user messages with a tier
  // in metadata that were added during mid-stream messaging.
  const localTierMsgs = current.filter(
    (m) => m.metadata?.tier && !recoveredMessages.some((bm) => bm.id === m.id)
  )

  // Merge: backend messages + any local tier messages that weren't in the backend set
  const merged =
    localTierMsgs.length > 0
      ? [...recoveredMessages, ...localTierMsgs].sort(
          (a, b) => (a.createdAt ?? 0) - (b.createdAt ?? 0)
        )
      : recoveredMessages

  // Smart comparison: IDs may differ between frontend-generated placeholders
  // and backend-persisted messages. Compare by role+content instead of just IDs
  // to avoid a full re-render flash when only IDs changed.
  const structurallyEqual =
    current.length === merged.length &&
    current.every((m, i) => {
      const incoming = merged[i]
      return m.role === incoming.role && m.content === incoming.content
    })

  if (structurallyEqual) {
    // Messages are structurally the same — update in-place (preserving DOM nodes)
    // but adopt the backend IDs and metadata for future consistency
    setMessages((prev) =>
      prev.map((existing, i) => {
        const incoming = merged[i]
        if (
          existing.id === incoming.id &&
          existing.content === incoming.content &&
          existing.toolCalls === incoming.toolCalls
        ) {
          return existing
        }
        // Adopt backend ID + metadata while preserving the SolidJS object identity
        // as much as possible to minimize DOM thrash
        return mergeMessageWithBackend(existing, incoming)
      })
    )
  } else {
    setMessages(merged)
  }
}

export async function replaceMessagesFromBackendForSession(
  sessionId: string,
  msgs: Message[]
): Promise<void> {
  const existingMessagesForSession =
    currentSession()?.id === sessionId
      ? messages()
      : (getCachedSessionArtifacts(sessionId)?.messages ?? [])
  const mergedMessages = mergeMessagesWithExisting(existingMessagesForSession, msgs)

  if (isTauri()) {
    await Promise.allSettled([...pendingInserts.values()])
    await dbDeleteSessionMessages(sessionId)
    await dbInsertMessages(mergedMessages)
  }

  const totalTokens = mergedMessages.reduce((sum, message) => sum + (message.tokensUsed || 0), 0)
  const lastPreview = mergedMessages[mergedMessages.length - 1]?.content.slice(0, 80) ?? ''

  setSessions((prev) =>
    prev.map((session) =>
      session.id === sessionId
        ? {
            ...session,
            messageCount: msgs.length,
            totalTokens,
            lastPreview,
            updatedAt: Date.now(),
          }
        : session
    )
  )

  if (currentSession()?.id === sessionId) {
    replaceMessagesFromBackend(mergedMessages)
  }

  updateCachedSessionMessages(sessionId, () => mergedMessages)
}

export async function rollbackToMessage(messageId: string): Promise<void> {
  const msgs = messages()
  const index = msgs.findIndex((m) => m.id === messageId)
  if (index === -1) return
  log.info('session', 'Rolling back to message', { messageId, removingCount: msgs.length - index })

  const target = msgs[index]
  const sessionId = target.sessionId
  const removedMessages = msgs.slice(index)

  setMessages((prev) => prev.slice(0, index))

  const removedTokens = removedMessages.reduce((sum, m) => sum + (m.tokensUsed || 0), 0)
  setSessions((prev) =>
    prev.map((s) =>
      s.id === sessionId
        ? {
            ...s,
            messageCount: Math.max(0, s.messageCount - removedMessages.length),
            totalTokens: Math.max(0, s.totalTokens - removedTokens),
            updatedAt: Date.now(),
          }
        : s
    )
  )

  try {
    await dbDeleteMessagesFromTimestamp(sessionId, target.createdAt)
  } catch (err) {
    logError('Session', 'Failed to delete messages from DB', err)
  }
}

// ============================================================================
// Checkpoints
// ============================================================================

export async function createCheckpoint(description: string): Promise<string | null> {
  const { saveMemoryItem } = await import('../../services/database')
  const sess = currentSession()
  if (!sess) return null
  const id = `ckpt-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`
  const snapshot = {
    messages: messages().map((m) => ({
      id: m.id,
      role: m.role,
      content: m.content,
      tokensUsed: m.tokensUsed,
      costUSD: m.costUSD,
      model: m.model,
      metadata: m.metadata,
    })),
  }
  await saveMemoryItem({
    id,
    sessionId: sess.id,
    type: 'checkpoint',
    title: description,
    preview: JSON.stringify(snapshot),
    tokens: 0,
    createdAt: Date.now(),
  })

  // Lazy import to avoid circular — setCheckpoints/setMemoryItems are from session-state
  const { setCheckpoints, setMemoryItems } = await import('./session-state')
  const memItem = {
    id,
    sessionId: sess.id,
    type: 'checkpoint' as const,
    title: description,
    preview: JSON.stringify(snapshot),
    tokens: 0,
    createdAt: Date.now(),
  }
  setMemoryItems((prev) => [...prev, memItem])
  setCheckpoints((prev) => [
    ...prev,
    { id, timestamp: Date.now(), description, messageCount: messages().length },
  ])
  return id
}

export async function rollbackToCheckpoint(checkpointId: string): Promise<boolean> {
  const { deleteSessionMessages: dbDeleteSessionMessages, insertMessages: dbInsertMessages } =
    await import('../../services/database')
  const { memoryItems } = await import('./session-state')

  const item = memoryItems().find((m) => m.id === checkpointId)
  if (!item) return false
  const sess = currentSession()
  if (!sess) return false
  try {
    const data = JSON.parse(item.preview) as {
      messages: Array<{
        id: string
        role: string
        content: string
        tokensUsed?: number
        costUSD?: number
        model?: string
        metadata?: Record<string, unknown>
      }>
    }
    const restored = data.messages.map((m) => ({
      ...m,
      sessionId: sess.id,
      createdAt: Date.now(),
      role: m.role as Message['role'],
    })) as Message[]
    setMessages(restored)
    await dbDeleteSessionMessages(sess.id)
    await dbInsertMessages(restored)
    return true
  } catch {
    return false
  }
}
