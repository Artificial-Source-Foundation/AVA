/**
 * Delta9 Agent Messaging System - Type Definitions
 *
 * Inbox/outbox messaging for agent-to-agent coordination.
 * Messages persist to JSONL and support TTL expiration.
 */

import { z } from 'zod'

// =============================================================================
// Message Types
// =============================================================================

/** Message recipient types */
export const MessageRecipientSchema = z.union([
  z.string(), // Specific agent ID
  z.literal('broadcast'), // All agents
  z.literal('council'), // All oracle agents
  z.literal('operators'), // All operator agents
  z.literal('support'), // All support agents (Delta Team)
])

export type MessageRecipient = z.infer<typeof MessageRecipientSchema>

/** Message type categories */
export const MessageTypeSchema = z.enum([
  'request', // Task/action request
  'response', // Response to a request
  'status', // Status update
  'coordination', // Coordination message
  'alert', // Important alert
  'ack', // Acknowledgment
])

export type MessageType = z.infer<typeof MessageTypeSchema>

/** Message priority levels */
export const MessagePrioritySchema = z.enum(['low', 'normal', 'high', 'critical'])

export type MessagePriority = z.infer<typeof MessagePrioritySchema>

/** Core message schema */
export const MessageSchema = z.object({
  /** Unique message ID */
  id: z.string(),
  /** Sender agent ID */
  from: z.string(),
  /** Recipient agent or broadcast group */
  to: MessageRecipientSchema,
  /** Message type */
  type: MessageTypeSchema,
  /** Message subject/title */
  subject: z.string(),
  /** Message body content */
  body: z.string(),
  /** Priority level */
  priority: MessagePrioritySchema.default('normal'),
  /** Parent message ID for threads */
  replyTo: z.string().optional(),
  /** Related task ID */
  taskId: z.string().optional(),
  /** Related mission ID */
  missionId: z.string().optional(),
  /** Additional metadata */
  metadata: z.record(z.unknown()).optional(),
  /** When the message was sent */
  sentAt: z.string(),
  /** When the message was read (per recipient) */
  readAt: z.string().optional(),
  /** When the message expires (TTL) */
  expiresAt: z.string().optional(),
})

export type Message = z.infer<typeof MessageSchema>

// =============================================================================
// Inbox Query Types
// =============================================================================

/** Query parameters for inbox retrieval */
export const InboxQuerySchema = z.object({
  /** Agent ID to get inbox for */
  agentId: z.string(),
  /** Only return unread messages */
  unreadOnly: z.boolean().default(false),
  /** Filter by message types */
  types: z.array(MessageTypeSchema).optional(),
  /** Filter by sender */
  from: z.string().optional(),
  /** Filter by task ID */
  taskId: z.string().optional(),
  /** Filter by mission ID */
  missionId: z.string().optional(),
  /** Messages since timestamp */
  since: z.string().optional(),
  /** Maximum messages to return */
  limit: z.number().default(50),
})

export type InboxQuery = z.infer<typeof InboxQuerySchema>

// =============================================================================
// Send Options
// =============================================================================

/** Options for sending a message */
export const SendMessageOptionsSchema = z.object({
  /** Recipient */
  to: MessageRecipientSchema,
  /** Message type */
  type: MessageTypeSchema,
  /** Subject */
  subject: z.string().min(1),
  /** Body */
  body: z.string(),
  /** Priority */
  priority: MessagePrioritySchema.optional(),
  /** Parent message ID for thread */
  replyTo: z.string().optional(),
  /** Related task ID */
  taskId: z.string().optional(),
  /** Related mission ID */
  missionId: z.string().optional(),
  /** Additional metadata */
  metadata: z.record(z.unknown()).optional(),
  /** TTL in milliseconds (default: 24 hours) */
  ttlMs: z.number().positive().optional(),
})

export type SendMessageOptions = z.infer<typeof SendMessageOptionsSchema>

// =============================================================================
// Store Configuration
// =============================================================================

/** Message store configuration */
export interface MessageStoreConfig {
  /** Base directory for storage */
  baseDir?: string
  /** Default TTL in milliseconds (default: 24 hours) */
  defaultTtlMs?: number
  /** Cleanup interval in milliseconds (default: 5 minutes) */
  cleanupIntervalMs?: number
  /** Maximum messages per inbox (default: 1000) */
  maxMessagesPerInbox?: number
  /** Enable auto-cleanup of expired messages */
  enableAutoCleanup?: boolean
}

/** Default configuration values */
export const DEFAULT_MESSAGE_CONFIG: Required<MessageStoreConfig> = {
  baseDir: process.cwd(),
  defaultTtlMs: 24 * 60 * 60 * 1000, // 24 hours
  cleanupIntervalMs: 5 * 60 * 1000, // 5 minutes
  maxMessagesPerInbox: 1000,
  enableAutoCleanup: true,
}

// =============================================================================
// Event Types
// =============================================================================

/** Message event types */
export type MessageEventType = 'sent' | 'read' | 'expired' | 'deleted'

/** Message event */
export interface MessageEvent {
  type: MessageEventType
  messageId: string
  from: string
  to: MessageRecipient
  timestamp: Date
  subject?: string
}

/** Message event listener */
export type MessageEventListener = (event: MessageEvent) => void

// =============================================================================
// Result Types
// =============================================================================

/** Send result */
export interface SendResult {
  success: boolean
  messageId?: string
  error?: string
  recipients?: string[]
}

/** Read result */
export interface ReadResult {
  success: boolean
  message?: Message
  error?: string
}

/** Inbox result */
export interface InboxResult {
  success: boolean
  messages: Message[]
  unreadCount: number
  totalCount: number
  error?: string
}

// =============================================================================
// Agent Groups (for broadcast resolution)
// =============================================================================

/** Known agent groups for broadcast targeting */
export const AGENT_GROUPS = {
  council: ['cipher', 'vector', 'prism', 'apex'],
  operators: ['operator', 'operator_complex'],
  support: ['recon', 'sigint', 'taccom', 'surgeon', 'sentinel', 'scribe', 'facade', 'spectre'],
} as const

export type AgentGroup = keyof typeof AGENT_GROUPS
