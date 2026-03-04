/**
 * Chat Hook — Barrel Export
 * Re-exports types and sub-modules for the chat/agent subsystem.
 */

export { buildConversationHistory } from './history-builder'
export { buildSystemPromptAfterInstructions } from './prompt-builder'
export type { QueuedMessage } from './types'
