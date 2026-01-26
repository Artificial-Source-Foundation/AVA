/**
 * Delta9 Agent Messaging System - Message Store
 *
 * JSONL-based message store with:
 * - Persistent storage to .delta9/messages.jsonl
 * - Inbox indexing for fast queries
 * - TTL-based automatic expiration
 * - Broadcast message resolution
 * - Event emission for monitoring
 */

import { existsSync, mkdirSync, readFileSync, appendFileSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { nanoid } from 'nanoid'
import {
  type Message,
  type MessageRecipient,
  type SendMessageOptions,
  type InboxQuery,
  type MessageStoreConfig,
  type MessageEvent,
  type MessageEventListener,
  type SendResult,
  type ReadResult,
  type InboxResult,
  MessageSchema,
  SendMessageOptionsSchema,
  InboxQuerySchema,
  DEFAULT_MESSAGE_CONFIG,
  AGENT_GROUPS,
} from './types.js'

// =============================================================================
// Message Store
// =============================================================================

/**
 * JSONL-based message store with inbox indexing
 */
export class MessageStore {
  private messages: Map<string, Message> = new Map()
  private inboxIndex: Map<string, Set<string>> = new Map() // agentId -> Set<messageId>
  private config: Required<MessageStoreConfig>
  private messagesFile: string
  private cleanupTimer: ReturnType<typeof setInterval> | null = null
  private eventListeners: Set<MessageEventListener> = new Set()

  constructor(config?: MessageStoreConfig) {
    this.config = { ...DEFAULT_MESSAGE_CONFIG, ...config }
    this.messagesFile = join(this.config.baseDir, '.delta9', 'messages.jsonl')

    this.ensureDirectory()
    this.loadMessages()

    if (this.config.enableAutoCleanup) {
      this.startCleanup()
    }
  }

  // ===========================================================================
  // Send Operations
  // ===========================================================================

  /**
   * Send a message to one or more agents
   */
  send(from: string, options: SendMessageOptions): SendResult {
    // Validate options
    const parsed = SendMessageOptionsSchema.safeParse(options)
    if (!parsed.success) {
      return { success: false, error: `Invalid options: ${parsed.error.message}` }
    }

    const validOptions = parsed.data
    const now = new Date()
    const messageId = nanoid(12)

    // Calculate expiration
    const ttlMs = validOptions.ttlMs ?? this.config.defaultTtlMs
    const expiresAt = new Date(now.getTime() + ttlMs).toISOString()

    // Resolve recipients
    const recipients = this.resolveRecipients(validOptions.to)

    if (recipients.length === 0) {
      return { success: false, error: 'No valid recipients' }
    }

    // Create message
    const message: Message = {
      id: messageId,
      from,
      to: validOptions.to,
      type: validOptions.type,
      subject: validOptions.subject,
      body: validOptions.body,
      priority: validOptions.priority ?? 'normal',
      replyTo: validOptions.replyTo,
      taskId: validOptions.taskId,
      missionId: validOptions.missionId,
      metadata: validOptions.metadata,
      sentAt: now.toISOString(),
      expiresAt,
    }

    // Validate message
    const validatedMessage = MessageSchema.parse(message)

    // Store message
    this.messages.set(messageId, validatedMessage)

    // Index for each recipient
    for (const recipient of recipients) {
      this.addToInbox(recipient, messageId)
    }

    // Persist to disk
    this.persistMessage(validatedMessage)

    // Emit event
    this.emit({
      type: 'sent',
      messageId,
      from,
      to: validOptions.to,
      timestamp: now,
      subject: validOptions.subject,
    })

    // Emit broadcast event if applicable
    if (
      validOptions.to === 'broadcast' ||
      validOptions.to === 'council' ||
      validOptions.to === 'operators' ||
      validOptions.to === 'support'
    ) {
      this.emit({
        type: 'sent',
        messageId,
        from,
        to: validOptions.to,
        timestamp: now,
        subject: `[${validOptions.to.toUpperCase()}] ${validOptions.subject}`,
      })
    }

    return {
      success: true,
      messageId,
      recipients,
    }
  }

  // ===========================================================================
  // Read Operations
  // ===========================================================================

  /**
   * Get inbox for an agent
   */
  getInbox(query: InboxQuery): InboxResult {
    // Validate query
    const parsed = InboxQuerySchema.safeParse(query)
    if (!parsed.success) {
      return {
        success: false,
        messages: [],
        unreadCount: 0,
        totalCount: 0,
        error: `Invalid query: ${parsed.error.message}`,
      }
    }

    const validQuery = parsed.data
    const { agentId, unreadOnly, types, from, taskId, missionId, since, limit } = validQuery

    // Get message IDs for this agent
    const messageIds = this.inboxIndex.get(agentId) ?? new Set()
    const now = new Date()

    let messages: Message[] = []
    let unreadCount = 0

    for (const messageId of messageIds) {
      const message = this.messages.get(messageId)
      if (!message) continue

      // Check expiration
      if (message.expiresAt && new Date(message.expiresAt) <= now) {
        this.expireMessage(messageId)
        continue
      }

      // Track unread count
      if (!message.readAt) {
        unreadCount++
      }

      // Apply filters
      if (unreadOnly && message.readAt) continue
      if (types && !types.includes(message.type)) continue
      if (from && message.from !== from) continue
      if (taskId && message.taskId !== taskId) continue
      if (missionId && message.missionId !== missionId) continue
      if (since && new Date(message.sentAt) < new Date(since)) continue

      messages.push(message)
    }

    // Sort by sentAt descending (newest first)
    messages.sort((a, b) => new Date(b.sentAt).getTime() - new Date(a.sentAt).getTime())

    // Apply limit
    const totalCount = messages.length
    if (limit && messages.length > limit) {
      messages = messages.slice(0, limit)
    }

    return {
      success: true,
      messages,
      unreadCount,
      totalCount,
    }
  }

  /**
   * Read a specific message
   */
  read(messageId: string, agentId: string): ReadResult {
    const message = this.messages.get(messageId)

    if (!message) {
      return { success: false, error: 'Message not found' }
    }

    // Check if agent is a valid recipient
    const recipients = this.resolveRecipients(message.to)
    if (!recipients.includes(agentId) && message.from !== agentId) {
      return { success: false, error: 'Not authorized to read this message' }
    }

    // Check expiration
    const now = new Date()
    if (message.expiresAt && new Date(message.expiresAt) <= now) {
      this.expireMessage(messageId)
      return { success: false, error: 'Message has expired' }
    }

    return { success: true, message }
  }

  /**
   * Mark a message as read
   */
  markRead(messageId: string, agentId: string): boolean {
    const message = this.messages.get(messageId)

    if (!message) return false

    // Check if already read
    if (message.readAt) return true

    // Check if agent is a valid recipient
    const recipients = this.resolveRecipients(message.to)
    if (!recipients.includes(agentId)) return false

    // Update message
    const updatedMessage: Message = {
      ...message,
      readAt: new Date().toISOString(),
    }
    this.messages.set(messageId, updatedMessage)

    // Emit event
    this.emit({
      type: 'read',
      messageId,
      from: message.from,
      to: message.to,
      timestamp: new Date(),
    })

    return true
  }

  /**
   * Get a message thread (all replies)
   */
  getThread(rootMessageId: string): Message[] {
    const thread: Message[] = []
    const visited = new Set<string>()

    // Add root message
    const root = this.messages.get(rootMessageId)
    if (root) {
      thread.push(root)
      visited.add(rootMessageId)
    }

    // Find all replies
    for (const [id, message] of this.messages) {
      if (visited.has(id)) continue
      if (message.replyTo === rootMessageId || this.isInThread(message, rootMessageId, visited)) {
        thread.push(message)
        visited.add(id)
      }
    }

    // Sort by sentAt ascending (oldest first for threads)
    thread.sort((a, b) => new Date(a.sentAt).getTime() - new Date(b.sentAt).getTime())

    return thread
  }

  private isInThread(message: Message, rootId: string, visited: Set<string>): boolean {
    if (!message.replyTo) return false
    if (message.replyTo === rootId) return true
    if (visited.has(message.replyTo)) return true

    const parent = this.messages.get(message.replyTo)
    if (!parent) return false

    return this.isInThread(parent, rootId, visited)
  }

  // ===========================================================================
  // Reply Operation
  // ===========================================================================

  /**
   * Reply to a message
   */
  reply(
    from: string,
    messageId: string,
    body: string,
    options: Partial<SendMessageOptions> = {}
  ): SendResult {
    const originalMessage = this.messages.get(messageId)

    if (!originalMessage) {
      return { success: false, error: 'Original message not found' }
    }

    // Create reply
    return this.send(from, {
      to: originalMessage.from, // Reply to sender
      type: options.type ?? 'response',
      subject: options.subject ?? `Re: ${originalMessage.subject}`,
      body,
      priority: options.priority ?? originalMessage.priority,
      replyTo: messageId,
      taskId: options.taskId ?? originalMessage.taskId,
      missionId: options.missionId ?? originalMessage.missionId,
      metadata: options.metadata,
      ttlMs: options.ttlMs,
    })
  }

  // ===========================================================================
  // Recipient Resolution
  // ===========================================================================

  /**
   * Resolve recipient to list of agent IDs
   */
  private resolveRecipients(to: MessageRecipient): string[] {
    if (to === 'broadcast') {
      // All known agents
      return [
        ...AGENT_GROUPS.council,
        ...AGENT_GROUPS.operators,
        ...AGENT_GROUPS.support,
        'commander',
      ]
    }

    if (to === 'council') {
      return [...AGENT_GROUPS.council]
    }

    if (to === 'operators') {
      return [...AGENT_GROUPS.operators]
    }

    if (to === 'support') {
      return [...AGENT_GROUPS.support]
    }

    // Specific agent ID
    return [to]
  }

  // ===========================================================================
  // Index Management
  // ===========================================================================

  private addToInbox(agentId: string, messageId: string): void {
    let inbox = this.inboxIndex.get(agentId)
    if (!inbox) {
      inbox = new Set()
      this.inboxIndex.set(agentId, inbox)
    }

    // Check max messages per inbox
    if (inbox.size >= this.config.maxMessagesPerInbox) {
      // Remove oldest message
      const oldestId = this.findOldestMessageInInbox(agentId)
      if (oldestId) {
        inbox.delete(oldestId)
      }
    }

    inbox.add(messageId)
  }

  private findOldestMessageInInbox(agentId: string): string | null {
    const inbox = this.inboxIndex.get(agentId)
    if (!inbox || inbox.size === 0) return null

    let oldestId: string | null = null
    let oldestTime = Infinity

    for (const messageId of inbox) {
      const message = this.messages.get(messageId)
      if (message) {
        const sentTime = new Date(message.sentAt).getTime()
        if (sentTime < oldestTime) {
          oldestTime = sentTime
          oldestId = messageId
        }
      }
    }

    return oldestId
  }

  private removeFromAllInboxes(messageId: string): void {
    for (const inbox of this.inboxIndex.values()) {
      inbox.delete(messageId)
    }
  }

  // ===========================================================================
  // Expiration & Cleanup
  // ===========================================================================

  private expireMessage(messageId: string): void {
    const message = this.messages.get(messageId)
    if (!message) return

    // Remove from indexes
    this.removeFromAllInboxes(messageId)

    // Remove from store
    this.messages.delete(messageId)

    // Emit event
    this.emit({
      type: 'expired',
      messageId,
      from: message.from,
      to: message.to,
      timestamp: new Date(),
    })
  }

  private startCleanup(): void {
    if (this.cleanupTimer) return

    this.cleanupTimer = setInterval(() => {
      this.cleanupExpired()
    }, this.config.cleanupIntervalMs)
  }

  /**
   * Clean up expired messages
   */
  cleanupExpired(): number {
    let count = 0
    const now = new Date()

    for (const [messageId, message] of this.messages) {
      if (message.expiresAt && new Date(message.expiresAt) <= now) {
        this.expireMessage(messageId)
        count++
      }
    }

    return count
  }

  // ===========================================================================
  // Event System
  // ===========================================================================

  /**
   * Add event listener
   */
  on(listener: MessageEventListener): void {
    this.eventListeners.add(listener)
  }

  /**
   * Remove event listener
   */
  off(listener: MessageEventListener): void {
    this.eventListeners.delete(listener)
  }

  private emit(event: MessageEvent): void {
    for (const listener of this.eventListeners) {
      try {
        listener(event)
      } catch {
        // Ignore listener errors
      }
    }
  }

  // ===========================================================================
  // Persistence
  // ===========================================================================

  private ensureDirectory(): void {
    const dir = join(this.config.baseDir, '.delta9')
    if (!existsSync(dir)) {
      mkdirSync(dir, { recursive: true })
    }
  }

  private loadMessages(): void {
    if (!existsSync(this.messagesFile)) {
      return
    }

    try {
      const content = readFileSync(this.messagesFile, 'utf-8')
      const lines = content.trim().split('\n').filter(Boolean)
      const now = new Date()

      for (const line of lines) {
        try {
          const message = JSON.parse(line)
          const parsed = MessageSchema.parse(message)

          // Skip expired messages
          if (parsed.expiresAt && new Date(parsed.expiresAt) <= now) {
            continue
          }

          // Add to store
          this.messages.set(parsed.id, parsed)

          // Add to inbox indexes
          const recipients = this.resolveRecipients(parsed.to)
          for (const recipient of recipients) {
            this.addToInbox(recipient, parsed.id)
          }
        } catch {
          // Skip invalid messages
        }
      }
    } catch {
      // File read error, start fresh
      this.messages.clear()
      this.inboxIndex.clear()
    }
  }

  private persistMessage(message: Message): void {
    try {
      appendFileSync(this.messagesFile, JSON.stringify(message) + '\n')
    } catch {
      // Persistence error, log but don't throw
    }
  }

  /**
   * Compact the message log (remove expired)
   */
  compact(): number {
    const originalCount = this.messages.size
    this.cleanupExpired()
    const removedCount = originalCount - this.messages.size

    // Rewrite file with only valid messages
    if (removedCount > 0) {
      const validMessages = Array.from(this.messages.values())
      writeFileSync(
        this.messagesFile,
        validMessages.map((m) => JSON.stringify(m)).join('\n') + '\n'
      )
    }

    return removedCount
  }

  // ===========================================================================
  // Statistics
  // ===========================================================================

  /**
   * Get message store statistics
   */
  getStats(): {
    totalMessages: number
    messagesByType: Map<string, number>
    inboxSizes: Map<string, number>
    unreadCounts: Map<string, number>
  } {
    const messagesByType = new Map<string, number>()
    const inboxSizes = new Map<string, number>()
    const unreadCounts = new Map<string, number>()

    // Count by type
    for (const message of this.messages.values()) {
      const count = messagesByType.get(message.type) ?? 0
      messagesByType.set(message.type, count + 1)
    }

    // Count inbox sizes and unread
    for (const [agentId, inbox] of this.inboxIndex) {
      inboxSizes.set(agentId, inbox.size)

      let unread = 0
      for (const messageId of inbox) {
        const message = this.messages.get(messageId)
        if (message && !message.readAt) {
          unread++
        }
      }
      unreadCounts.set(agentId, unread)
    }

    return {
      totalMessages: this.messages.size,
      messagesByType,
      inboxSizes,
      unreadCounts,
    }
  }

  // ===========================================================================
  // Cleanup
  // ===========================================================================

  /**
   * Stop cleanup timer and clear all data
   */
  destroy(): void {
    if (this.cleanupTimer) {
      clearInterval(this.cleanupTimer)
      this.cleanupTimer = null
    }
    this.messages.clear()
    this.inboxIndex.clear()
    this.eventListeners.clear()
  }

  /**
   * Clear all messages (for testing)
   */
  clear(): void {
    this.messages.clear()
    this.inboxIndex.clear()
    if (existsSync(this.messagesFile)) {
      writeFileSync(this.messagesFile, '')
    }
  }
}

// =============================================================================
// Singleton Instance
// =============================================================================

let globalMessageStore: MessageStore | null = null

/**
 * Get the global message store instance
 */
export function getMessageStore(config?: MessageStoreConfig): MessageStore {
  if (!globalMessageStore) {
    globalMessageStore = new MessageStore(config)
  }
  return globalMessageStore
}

/**
 * Reset the global message store (for testing)
 */
export function resetMessageStore(): void {
  if (globalMessageStore) {
    globalMessageStore.destroy()
    globalMessageStore = null
  }
}
