/**
 * useChat Hook
 * Provider-agnostic chat hook with streaming support and tool integration.
 *
 * This is a thin orchestrator: business logic lives in `./chat/` sub-modules.
 */

import { createSignal } from 'solid-js'
import { createApprovalGate } from '../lib/tool-approval'
import { useProject } from '../stores/project'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import type { ToolCall } from '../types'
import type { LLMProvider, StreamError } from '../types/llm'
import {
  cancel,
  clearError,
  clearQueue,
  editAndResend,
  processQueue,
  regenerateResponse,
  retryMessage,
  sendMessage,
  steer,
  undoLastEdit,
} from './chat/message-actions'
import type { ChatDeps, ContextStats, QueuedMessage } from './chat/types'

// Re-export public types so existing consumers keep working
export type { ContextStats } from './chat/types'

// ============================================================================
// Singleton
// ============================================================================

type ChatStore = ReturnType<typeof createChatStore>
let chatStoreSingleton: ChatStore | null = null

export function useChat(): ChatStore {
  if (!chatStoreSingleton) {
    chatStoreSingleton = createChatStore()
  }
  return chatStoreSingleton
}

// ============================================================================
// Store Factory
// ============================================================================

function createChatStore() {
  // Signals
  const [isStreaming, setIsStreaming] = createSignal(false)
  const [error, setError] = createSignal<StreamError | null>(null)
  const [currentProvider, setCurrentProvider] = createSignal<LLMProvider | null>(null)
  const [contextStats, setContextStats] = createSignal<ContextStats | null>(null)
  const [streamingTokenEstimate, setStreamingTokenEstimate] = createSignal(0)
  const [streamingStartedAt, setStreamingStartedAt] = createSignal<number | null>(null)
  const [activeToolCalls, setActiveToolCalls] = createSignal<ToolCall[]>([])
  const [messageQueue, setMessageQueue] = createSignal<QueuedMessage[]>([])

  // External stores / refs
  const session = useSession()
  const { currentProject } = useProject()
  const settings = useSettings()
  const approval = createApprovalGate()
  const abortRef = { current: null as AbortController | null }

  // Shared dependency bag for sub-modules
  const deps: ChatDeps = {
    LOG_SRC: 'chat',
    isStreaming,
    setIsStreaming,
    error,
    setError,
    setCurrentProvider,
    contextStats,
    setContextStats,
    setStreamingTokenEstimate,
    setStreamingStartedAt,
    activeToolCalls,
    setActiveToolCalls,
    messageQueue,
    setMessageQueue,
    abortRef,
    session,
    settings,
    currentProject,
    approval,
  }

  return {
    // State (read-only accessors)
    isStreaming,
    error,
    currentProvider,
    contextStats,
    streamingTokenEstimate,
    streamingStartedAt,
    activeToolCalls,
    pendingApproval: approval.pendingApproval,

    // Queue
    queuedCount: () => messageQueue().length,
    steer: (
      content: string,
      model?: string,
      images?: Array<{ data: string; mimeType: string; name?: string }>
    ) => steer(deps, content, model, images),
    clearQueue: () => clearQueue(deps),

    // Actions (delegate to sub-modules with bound deps)
    sendMessage: (
      content: string,
      model?: string,
      images?: Array<{ data: string; mimeType: string; name?: string }>
    ) => sendMessage(deps, content, model, images, processQueue),
    cancel: () => cancel(deps),
    clearError: () => clearError(deps),
    retryMessage: (id: string) => retryMessage(deps, id),
    editAndResend: (id: string, content: string) => editAndResend(deps, id, content),
    regenerateResponse: (id: string) => regenerateResponse(deps, id),
    undoLastEdit: () => undoLastEdit(deps),
    resolveApproval: approval.resolveApproval,
  }
}
