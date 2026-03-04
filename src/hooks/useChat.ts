/**
 * useChat Hook — Thin backward-compat wrapper over useAgent
 *
 * All business logic now lives in useAgent.ts. This wrapper re-exports
 * the same API shape that components expect so no component changes are needed.
 */

import type { QueuedMessage } from './chat/types'
import { useAgent } from './useAgent'

// Re-export public types so existing consumers keep working
export type { QueuedMessage }
export interface ContextStats {
  total: number
  limit: number
  remaining: number
  percentUsed: number
}

// ============================================================================
// Singleton (delegates to useAgent singleton)
// ============================================================================

export function useChat() {
  const agent = useAgent()

  return {
    // State (read-only accessors)
    isStreaming: agent.isRunning,
    error: agent.error,
    currentProvider: () => null, // no longer tracked separately
    contextStats: () => null as ContextStats | null,
    streamingTokenEstimate: agent.streamingTokenEstimate,
    streamingStartedAt: agent.streamingStartedAt,
    activeToolCalls: agent.activeToolCalls,
    pendingApproval: agent.pendingApproval,

    // Queue
    messageQueue: agent.messageQueue,
    queuedCount: agent.queuedCount,
    removeFromQueue: agent.removeFromQueue,
    steer: agent.steer,
    clearQueue: agent.clearQueue,

    // Actions (delegate to useAgent)
    sendMessage: (
      content: string,
      _model?: string,
      _images?: Array<{ data: string; mimeType: string; name?: string }>
    ) => agent.run(content),
    cancel: agent.cancel,
    clearError: agent.clearError,
    retryMessage: agent.retryMessage,
    editAndResend: agent.editAndResend,
    regenerateResponse: agent.regenerateResponse,
    undoLastEdit: agent.undoLastEdit,
    resolveApproval: agent.resolveApproval,
  }
}
