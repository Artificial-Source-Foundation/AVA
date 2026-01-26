/**
 * Delta9 Messaging Tools
 *
 * Tools for agent-to-agent messaging:
 * - send_message: Send a message to an agent or broadcast group
 * - check_inbox: Get messages for an agent
 * - read_message: Read a specific message
 * - reply_message: Reply to a message in a thread
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import { getMessageStore, type MessageType, type MessagePriority } from '../messaging/index.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Factory
// =============================================================================

export interface MessagingToolsConfig {
  /** Default agent ID for operations (if not specified in tool args) */
  defaultAgentId?: string
  /** Default TTL in milliseconds */
  defaultTtlMs?: number
  /** Logger function */
  log?: (level: string, message: string, data?: Record<string, unknown>) => void
}

/**
 * Create messaging tools with bound context
 */
export function createMessagingTools(
  config: MessagingToolsConfig = {}
): Record<string, ToolDefinition> {
  const { defaultAgentId, defaultTtlMs, log } = config
  const store = getMessageStore()

  /**
   * Send a message to an agent or broadcast group
   */
  const send_message = tool({
    description: `Send a message to an agent or broadcast group.
Recipients can be:
- Specific agent ID (e.g., "operator", "commander")
- "broadcast" - All agents
- "council" - Oracle agents (cipher, vector, prism, apex)
- "operators" - Operator agents
- "support" - Delta Team support agents

Message types: request, response, status, coordination, alert, ack
Priority: low, normal, high, critical`,
    args: {
      to: s
        .string()
        .describe('Recipient: agent ID, "broadcast", "council", "operators", or "support"'),
      type: s
        .string()
        .describe('Message type: request, response, status, coordination, alert, ack'),
      subject: s.string().describe('Message subject/title'),
      body: s.string().describe('Message body content'),
      priority: s.string().optional().describe('Priority: low, normal (default), high, critical'),
      replyTo: s.string().optional().describe('Parent message ID for threads'),
      taskId: s.string().optional().describe('Related task ID'),
      missionId: s.string().optional().describe('Related mission ID'),
      ttlMs: s.number().optional().describe('Time-to-live in milliseconds (default: 24 hours)'),
    },

    async execute(args, ctx) {
      const fromAgentId = defaultAgentId ?? ctx.sessionID ?? 'unknown'

      log?.('info', 'Sending message', { to: args.to, type: args.type, subject: args.subject })

      const result = store.send(fromAgentId, {
        to: args.to,
        type: args.type as MessageType,
        subject: args.subject,
        body: args.body,
        priority: (args.priority as MessagePriority) ?? 'normal',
        replyTo: args.replyTo,
        taskId: args.taskId,
        missionId: args.missionId,
        ttlMs: args.ttlMs ?? defaultTtlMs,
      })

      if (result.success) {
        return JSON.stringify({
          success: true,
          messageId: result.messageId,
          recipients: result.recipients,
          message: `Message sent to ${args.to}`,
        })
      } else {
        return JSON.stringify({
          success: false,
          error: result.error,
        })
      }
    },
  })

  /**
   * Check inbox for messages
   */
  const check_inbox = tool({
    description: `Check an agent's inbox for messages. Returns messages matching filters, sorted by newest first.
By default returns all messages. Use filters to narrow results.`,
    args: {
      agentId: s
        .string()
        .optional()
        .describe('Agent ID to check inbox for (defaults to current session)'),
      unreadOnly: s.boolean().optional().describe('Only return unread messages'),
      type: s.string().optional().describe('Filter by message type (comma-separated)'),
      from: s.string().optional().describe('Filter by sender agent ID'),
      taskId: s.string().optional().describe('Filter by related task ID'),
      missionId: s.string().optional().describe('Filter by related mission ID'),
      since: s.string().optional().describe('Only messages after this ISO timestamp'),
      limit: s.number().optional().describe('Maximum messages to return (default: 50)'),
    },

    async execute(args, ctx) {
      const agentId = args.agentId ?? defaultAgentId ?? ctx.sessionID ?? 'unknown'

      log?.('debug', 'Checking inbox', { agentId, unreadOnly: args.unreadOnly })

      // Parse comma-separated types
      const types = args.type
        ? (args.type.split(',').map((t) => t.trim()) as MessageType[])
        : undefined

      const result = store.getInbox({
        agentId,
        unreadOnly: args.unreadOnly ?? false,
        types,
        from: args.from,
        taskId: args.taskId,
        missionId: args.missionId,
        since: args.since,
        limit: args.limit ?? 50,
      })

      if (result.success) {
        return JSON.stringify({
          success: true,
          unreadCount: result.unreadCount,
          totalCount: result.totalCount,
          returned: result.messages.length,
          messages: result.messages.map((m) => ({
            id: m.id,
            from: m.from,
            to: m.to,
            type: m.type,
            subject: m.subject,
            priority: m.priority,
            sentAt: m.sentAt,
            readAt: m.readAt,
            replyTo: m.replyTo,
            taskId: m.taskId,
            missionId: m.missionId,
            // Include body preview (first 200 chars)
            bodyPreview: m.body.length > 200 ? m.body.substring(0, 200) + '...' : m.body,
          })),
        })
      } else {
        return JSON.stringify({
          success: false,
          error: result.error,
        })
      }
    },
  })

  /**
   * Read a specific message
   */
  const read_message = tool({
    description: 'Read a specific message by ID and optionally mark it as read.',
    args: {
      messageId: s.string().describe('Message ID to read'),
      agentId: s
        .string()
        .optional()
        .describe('Agent ID reading the message (defaults to current session)'),
      markRead: s.boolean().optional().describe('Mark message as read (default: true)'),
    },

    async execute(args, ctx) {
      const agentId = args.agentId ?? defaultAgentId ?? ctx.sessionID ?? 'unknown'
      const markRead = args.markRead !== false

      log?.('debug', 'Reading message', { messageId: args.messageId, agentId })

      const result = store.read(args.messageId, agentId)

      if (result.success && result.message) {
        // Mark as read if requested
        if (markRead) {
          store.markRead(args.messageId, agentId)
        }

        const message = result.message
        return JSON.stringify({
          success: true,
          message: {
            id: message.id,
            from: message.from,
            to: message.to,
            type: message.type,
            subject: message.subject,
            body: message.body,
            priority: message.priority,
            sentAt: message.sentAt,
            readAt: markRead ? new Date().toISOString() : message.readAt,
            replyTo: message.replyTo,
            taskId: message.taskId,
            missionId: message.missionId,
            expiresAt: message.expiresAt,
            metadata: message.metadata,
          },
        })
      } else {
        return JSON.stringify({
          success: false,
          error: result.error,
        })
      }
    },
  })

  /**
   * Reply to a message
   */
  const reply_message = tool({
    description:
      'Reply to a message, creating a threaded conversation. The reply is sent to the original sender.',
    args: {
      messageId: s.string().describe('Message ID to reply to'),
      body: s.string().describe('Reply body content'),
      type: s.string().optional().describe('Message type (default: response)'),
      priority: s.string().optional().describe('Priority level'),
      taskId: s.string().optional().describe('Override task ID'),
      missionId: s.string().optional().describe('Override mission ID'),
    },

    async execute(args, ctx) {
      const fromAgentId = defaultAgentId ?? ctx.sessionID ?? 'unknown'

      log?.('info', 'Replying to message', { messageId: args.messageId })

      const result = store.reply(fromAgentId, args.messageId, args.body, {
        type: (args.type as MessageType) ?? 'response',
        priority: args.priority as MessagePriority,
        taskId: args.taskId,
        missionId: args.missionId,
      })

      if (result.success) {
        return JSON.stringify({
          success: true,
          replyId: result.messageId,
          message: 'Reply sent',
        })
      } else {
        return JSON.stringify({
          success: false,
          error: result.error,
        })
      }
    },
  })

  /**
   * Get message thread
   */
  const get_thread = tool({
    description: 'Get all messages in a thread, starting from the root message.',
    args: {
      messageId: s.string().describe('Root message ID of the thread'),
    },

    async execute(args, _ctx) {
      log?.('debug', 'Getting thread', { messageId: args.messageId })

      const messages = store.getThread(args.messageId)

      return JSON.stringify({
        success: true,
        count: messages.length,
        thread: messages.map((m) => ({
          id: m.id,
          from: m.from,
          to: m.to,
          type: m.type,
          subject: m.subject,
          body: m.body,
          priority: m.priority,
          sentAt: m.sentAt,
          readAt: m.readAt,
          replyTo: m.replyTo,
        })),
      })
    },
  })

  /**
   * Get messaging statistics
   */
  const message_stats = tool({
    description:
      'Get statistics about the messaging system including inbox sizes and unread counts.',
    args: {},

    async execute(_args, _ctx) {
      log?.('debug', 'Getting message stats')

      const stats = store.getStats()

      // Convert Maps to plain objects for JSON
      const messagesByType: Record<string, number> = {}
      for (const [type, count] of stats.messagesByType) {
        messagesByType[type] = count
      }

      const inboxSizes: Record<string, number> = {}
      for (const [agentId, size] of stats.inboxSizes) {
        inboxSizes[agentId] = size
      }

      const unreadCounts: Record<string, number> = {}
      for (const [agentId, count] of stats.unreadCounts) {
        unreadCounts[agentId] = count
      }

      return JSON.stringify({
        success: true,
        totalMessages: stats.totalMessages,
        messagesByType,
        inboxSizes,
        unreadCounts,
      })
    },
  })

  return {
    send_message,
    check_inbox,
    read_message,
    reply_message,
    get_thread,
    message_stats,
  }
}
