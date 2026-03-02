/**
 * Chat Hook Types
 * Shared types and dependency interface for the useChat subsystem.
 */

import type { ToolContext } from '@ava/core-v2/tools'
import type { Accessor, Setter } from 'solid-js'
import type { CompletionNotificationSettings } from '../../services/notifications'
import type { FileOperation, Message, MessageError, ToolCall } from '../../types'
import type { LLMProvider, StreamError } from '../../types/llm'

// ============================================================================
// Public Types
// ============================================================================

export interface ContextStats {
  total: number
  limit: number
  remaining: number
  percentUsed: number
}

export interface StreamOptions {
  sessionId: string
  model: string
  goal: string
  systemPrompt?: string
  conversationContext?: string
  onContent: (content: string) => void
  onThinking?: (thinking: string) => void
  onComplete: (content: string, tokens?: number, toolCalls?: ToolCall[]) => void
  onError: (error: StreamError) => void
  onToolUpdate?: (toolCalls: ToolCall[]) => void
  signal: AbortSignal
  enableTools?: boolean
}

export interface QueuedMessage {
  content: string
  model?: string
  images?: Array<{ data: string; mimeType: string; name?: string }>
}

// ============================================================================
// Internal Dependency Interface
// ============================================================================

/**
 * Shared mutable state + dependencies passed to all chat sub-modules.
 * Avoids circular imports by centralising shared refs in one place.
 */
export interface ChatDeps {
  readonly LOG_SRC: string

  // Signals
  isStreaming: Accessor<boolean>
  setIsStreaming: Setter<boolean>
  error: Accessor<StreamError | null>
  setError: Setter<StreamError | null>
  setCurrentProvider: Setter<LLMProvider | null>
  contextStats: Accessor<ContextStats | null>
  setContextStats: Setter<ContextStats | null>
  setStreamingTokenEstimate: Setter<number>
  setStreamingStartedAt: Setter<number | null>
  activeToolCalls: Accessor<ToolCall[]>
  setActiveToolCalls: Setter<ToolCall[]>
  messageQueue: Accessor<QueuedMessage[]>
  setMessageQueue: Setter<QueuedMessage[]>

  // Refs
  abortRef: { current: AbortController | null }

  // Stores
  session: SessionSlice
  settings: SettingsSlice
  currentProject: () => { directory: string } | null

  // Approval gate
  approval: {
    requestApproval: (toolName: string, args: Record<string, unknown>) => Promise<boolean>
  }
}

/** Subset of session store used by chat */
export interface SessionSlice {
  messages: Accessor<Message[]>
  selectedModel: () => string
  currentSession: () => { id: string; name: string } | null
  createNewSession: () => Promise<{ id: string }>
  renameSession: (id: string, name: string) => Promise<void>
  addMessage: (msg: Message) => void
  updateMessageContent: (id: string, content: string) => void
  updateMessage: (id: string, patch: Partial<Message>) => void
  setMessageError: (id: string, err: MessageError | null) => void
  setMessages: (msgs: Message[]) => void
  deleteMessage: (id: string) => void
  deleteMessagesAfter: (id: string) => void
  stopEditing: () => void
  setRetryingMessageId: (id: string | null) => void
  addFileOperation: (operation: FileOperation) => void
}

/** Subset of settings store used by chat */
export interface SettingsSlice {
  settings: () => {
    generation: {
      customInstructions: string
      maxTokens: number
      temperature: number
      thinkingEnabled: boolean
    }
    behavior: { sessionAutoTitle: boolean }
    agentLimits: { autoFixLint: boolean }
    notifications: CompletionNotificationSettings
  }
  isToolAutoApproved: (name: string) => boolean
}

/** Build a ToolContext from deps */
export function buildToolCtx(deps: ChatDeps, sessionId: string, signal: AbortSignal): ToolContext {
  return {
    sessionId,
    workingDirectory: deps.currentProject()?.directory || '.',
    signal,
  }
}
