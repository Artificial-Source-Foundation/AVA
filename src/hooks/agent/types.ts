/**
 * Agent Module Types
 *
 * Shared type definitions used across the extracted agent hook modules.
 * These bridge interfaces decouple modules from direct store dependencies.
 */

import type { Accessor, Setter } from 'solid-js'
import type { FileOperation, Message, ToolCall } from '../../types'
import type { StreamError } from '../../types/llm'
import type { QueuedMessage } from '../chat/types'
import type { ApprovalRequest, ToolActivity } from './agent-types'

// ============================================================================
// Session Bridge
// ============================================================================

/**
 * Subset of session store operations needed by agent modules.
 * Prevents extracted modules from importing the full session store directly.
 */
export interface SessionBridge {
  messages(): Message[]
  currentSession(): { id: string; name?: string } | null
  addMessage(msg: Message): void
  updateMessage(id: string, updates: Partial<Message>): void
  updateMessageContent(id: string, content: string): void
  setMessageError(id: string, error: Message['error'] | null): void
  deleteMessage(id: string): void
  deleteMessagesAfter(id: string): void
  addFileOperation(op: FileOperation): void
  selectedModel(): string
  setRetryingMessageId(id: string | null): void
  stopEditing(): void
  createNewSession(): Promise<{ id: string }>
  renameSession(id: string, name: string): Promise<void>
}

// ============================================================================
// Agent Signals
// ============================================================================

/** All reactive signals used by the agent store */
export interface AgentSignals {
  // Agent state signals
  isRunning: Accessor<boolean>
  setIsRunning: Setter<boolean>
  isPlanMode: Accessor<boolean>
  setIsPlanMode: Setter<boolean>
  currentTurn: Accessor<number>
  setCurrentTurn: Setter<number>
  tokensUsed: Accessor<number>
  setTokensUsed: Setter<number>
  currentThought: Accessor<string>
  setCurrentThought: Setter<string>
  toolActivity: Accessor<ToolActivity[]>
  setToolActivity: Setter<ToolActivity[]>
  pendingApproval: Accessor<ApprovalRequest | null>
  setPendingApproval: Setter<ApprovalRequest | null>
  doomLoopDetected: Accessor<boolean>
  setDoomLoopDetected: Setter<boolean>
  lastError: Accessor<string | null>
  setLastError: Setter<string | null>
  currentAgentId: Accessor<string | null>
  setCurrentAgentId: Setter<string | null>

  // Chat/streaming signals
  activeToolCalls: Accessor<ToolCall[]>
  setActiveToolCalls: Setter<ToolCall[]>
  streamingContent: Accessor<string>
  setStreamingContent: Setter<string>
  streamingTokenEstimate: Accessor<number>
  setStreamingTokenEstimate: Setter<number>
  streamingStartedAt: Accessor<number | null>
  setStreamingStartedAt: Setter<number | null>
  error: Accessor<StreamError | null>
  setError: Setter<StreamError | null>
  messageQueue: Accessor<QueuedMessage[]>
  setMessageQueue: Setter<QueuedMessage[]>
}

// ============================================================================
// Refs
// ============================================================================

/** Mutable refs for abort controller and executor instance */
export interface AgentRefs {
  abortRef: { current: AbortController | null }
  /** @deprecated Executor is now in the Rust backend */
  executorRef: { current: unknown | null }
}
