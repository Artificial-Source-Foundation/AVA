/**
 * Webhook System Types
 *
 * Real-time notifications to external services.
 */

import { z } from 'zod'

// =============================================================================
// Webhook Configuration
// =============================================================================

export const webhookConfigSchema = z.object({
  id: z.string(),
  name: z.string(),
  url: z.string().url(),
  /** HTTP method */
  method: z.enum(['POST', 'PUT']).default('POST'),
  /** Events to trigger on */
  events: z.array(z.string()),
  /** Custom headers */
  headers: z.record(z.string()).default({}),
  /** Secret for signature */
  secret: z.string().optional(),
  /** Enable/disable */
  enabled: z.boolean().default(true),
  /** Retry configuration */
  retry: z
    .object({
      maxRetries: z.number().int().min(0).max(5).default(3),
      backoffMs: z.number().int().min(100).max(30000).default(1000),
    })
    .default({}),
  /** Timeout in ms */
  timeout: z.number().int().min(1000).max(30000).default(5000),
  /** Payload format */
  format: z.enum(['json', 'form', 'slack', 'discord']).default('json'),
  /** Template for custom payload */
  template: z.string().optional(),
})

export type WebhookConfig = z.infer<typeof webhookConfigSchema>

// =============================================================================
// Webhook Delivery
// =============================================================================

export const webhookDeliverySchema = z.object({
  id: z.string(),
  webhookId: z.string(),
  event: z.string(),
  payload: z.unknown(),
  status: z.enum(['pending', 'sending', 'delivered', 'failed', 'retrying']),
  attempts: z.number().int().default(0),
  lastAttemptAt: z.string().optional(),
  response: z
    .object({
      statusCode: z.number(),
      body: z.string().optional(),
      headers: z.record(z.string()).optional(),
    })
    .optional(),
  error: z.string().optional(),
  createdAt: z.string(),
  deliveredAt: z.string().optional(),
})

export type WebhookDelivery = z.infer<typeof webhookDeliverySchema>

// =============================================================================
// Webhook Event
// =============================================================================

export interface WebhookEvent {
  type: string
  timestamp: string
  data: Record<string, unknown>
  missionId?: string
  taskId?: string
  sessionId?: string
}

// =============================================================================
// Webhook Payload Formats
// =============================================================================

export interface SlackPayload {
  text: string
  attachments?: Array<{
    color?: string
    title?: string
    text?: string
    fields?: Array<{
      title: string
      value: string
      short?: boolean
    }>
    footer?: string
    ts?: number
  }>
  channel?: string
  username?: string
  icon_emoji?: string
}

export interface DiscordPayload {
  content?: string
  embeds?: Array<{
    title?: string
    description?: string
    color?: number
    fields?: Array<{
      name: string
      value: string
      inline?: boolean
    }>
    footer?: { text: string }
    timestamp?: string
  }>
  username?: string
  avatar_url?: string
}

// =============================================================================
// Predefined Events
// =============================================================================

export const WEBHOOK_EVENTS = {
  // Mission events
  'mission.created': 'Mission created',
  'mission.started': 'Mission started',
  'mission.completed': 'Mission completed',
  'mission.failed': 'Mission failed',
  'mission.aborted': 'Mission aborted',
  'mission.resumed': 'Mission resumed',

  // Task events
  'task.started': 'Task started',
  'task.completed': 'Task completed',
  'task.failed': 'Task failed',
  'task.retrying': 'Task retrying',

  // Council events
  'council.convened': 'Council convened',
  'council.completed': 'Council completed',
  'council.conflict': 'Council conflict detected',

  // Validation events
  'validation.passed': 'Validation passed',
  'validation.failed': 'Validation failed',

  // Budget events
  'budget.warning': 'Budget warning',
  'budget.exceeded': 'Budget exceeded',

  // System events
  'system.error': 'System error',
  'system.recovery': 'System recovery',

  // Legion events
  'legion.strike.started': 'Legion strike started',
  'legion.strike.completed': 'Legion strike completed',
} as const

export type WebhookEventType = keyof typeof WEBHOOK_EVENTS

// =============================================================================
// Event Filters
// =============================================================================

export interface EventFilter {
  /** Event types to include */
  include?: string[]
  /** Event types to exclude */
  exclude?: string[]
  /** Only events for specific mission */
  missionId?: string
  /** Minimum severity level */
  minSeverity?: 'info' | 'warning' | 'error' | 'critical'
}
