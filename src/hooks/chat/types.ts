/**
 * Chat Hook Types
 * Shared types for the chat/agent subsystem.
 */

export type MessageTier = 'steering' | 'follow-up' | 'post-complete'

export interface QueuedMessage {
  content: string
  tier?: MessageTier
  group?: number
  model?: string
  images?: Array<{ data: string; mimeType: string; name?: string }>
}
