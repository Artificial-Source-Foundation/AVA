/**
 * Chat Hook Types
 * Shared types for the chat/agent subsystem.
 */

export interface QueuedMessage {
  content: string
  model?: string
  images?: Array<{ data: string; mimeType: string; name?: string }>
}
