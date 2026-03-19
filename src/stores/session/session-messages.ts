/**
 * Session Message Actions
 * Add, update, delete, and rollback messages within a session.
 */

import { log } from '../../lib/logger'
import {
  deleteMessageFromDb as dbDeleteMessage,
  deleteMessagesFromTimestamp as dbDeleteMessagesFromTimestamp,
  getMessages,
} from '../../services/database'
import { logError } from '../../services/logger'
import type { Message, MessageError } from '../../types'
import {
  currentSession,
  messages,
  setIsLoadingMessages,
  setMessages,
  setSessions,
} from './session-state'

// ============================================================================
// Message Management
// ============================================================================

export async function loadSessionMessages(sessionId: string): Promise<void> {
  setIsLoadingMessages(true)
  try {
    const dbMessages = await getMessages(sessionId)
    setMessages(dbMessages)
  } catch (err) {
    logError('Session', 'Failed to load messages', err)
    setMessages([])
  } finally {
    setIsLoadingMessages(false)
  }
}

export function addMessage(message: Message): void {
  log.debug('session', 'Message added', {
    id: message.id,
    role: message.role,
    sessionId: message.sessionId,
  })
  setMessages((prev) => [...prev, message])

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

export function updateMessageContent(id: string, content: string): void {
  setMessages((prev) => prev.map((msg) => (msg.id === id ? { ...msg, content } : msg)))
}

export function updateMessage(id: string, updates: Partial<Message>): void {
  setMessages((prev) => {
    const idx = prev.findIndex((msg) => msg.id === id)
    if (idx === -1) return prev
    const next = prev.slice()
    next[idx] = { ...prev[idx], ...updates }
    return next
  })
}

export function setMessageError(messageId: string, error: MessageError | null): void {
  setMessages((prev) =>
    prev.map((msg) => (msg.id === messageId ? { ...msg, error: error || undefined } : msg))
  )
}

export async function deleteMessage(id: string): Promise<void> {
  log.debug('session', 'Message deleted', { id })
  setMessages((prev) => prev.filter((msg) => msg.id !== id))
  try {
    await dbDeleteMessage(id)
  } catch (err) {
    logError('Session', 'Failed to delete message from DB', err)
  }
}

export function deleteMessagesAfter(messageId: string): void {
  setMessages((prev) => {
    const index = prev.findIndex((m) => m.id === messageId)
    return index >= 0 ? prev.slice(0, index + 1) : prev
  })
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

  // Lazy import to avoid circular — setCheckpoints is from session-state
  const { setCheckpoints } = await import('./session-state')
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
