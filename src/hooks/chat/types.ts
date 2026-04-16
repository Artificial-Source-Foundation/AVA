/**
 * Chat Hook Types
 * Shared types for the chat/agent subsystem.
 */

export type MessageTier = 'queued' | 'interrupt' | 'post-complete' | 'steering' | 'follow-up'

export interface QueuedMessage {
  content: string
  tier?: MessageTier
  group?: number
  backendManaged?: boolean
  sessionId?: string
  model?: string
  images?: Array<{ data: string; mimeType: string; name?: string }>
}
