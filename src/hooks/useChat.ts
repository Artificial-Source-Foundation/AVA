/**
 * useChat Hook
 * Provider-agnostic chat hook with streaming support
 */

import { createSignal } from 'solid-js'
import { DEFAULTS } from '../config/constants'
import { saveMessage, updateMessage } from '../services/database'
import { createClient, resolveAuth } from '../services/llm/client'
import { useSession } from '../stores/session'
import type { Message } from '../types'
import type { LLMProvider, StreamError } from '../types/llm'

// ============================================================================
// Types
// ============================================================================

export interface ChatState {
  isStreaming: boolean
  error: StreamError | null
  currentProvider: LLMProvider | null
}

interface StreamOptions {
  sessionId: string
  model: string
  messages: Array<{ role: 'user' | 'assistant' | 'system'; content: string }>
  onContent: (content: string) => void
  onComplete: (content: string, tokens?: number) => void
  onError: (error: StreamError) => void
  signal: AbortSignal
}

// ============================================================================
// Hook Implementation
// ============================================================================

export function useChat() {
  const [isStreaming, setIsStreaming] = createSignal(false)
  const [error, setError] = createSignal<StreamError | null>(null)
  const [currentProvider, setCurrentProvider] = createSignal<LLMProvider | null>(null)

  const abortRef = { current: null as AbortController | null }
  const session = useSession()

  // ==========================================================================
  // Core Streaming Function (Internal)
  // ==========================================================================

  async function streamResponse(options: StreamOptions): Promise<void> {
    const resolved = resolveAuth(options.model)
    if (!resolved) {
      options.onError({
        type: 'auth',
        message: 'No credentials configured. Please add an API key in Settings.',
      })
      return
    }

    setCurrentProvider(resolved.provider)
    const client = await createClient(resolved.provider)
    let fullContent = ''

    try {
      for await (const delta of client.stream(
        options.messages,
        {
          provider: resolved.provider,
          model: options.model,
          authMethod: resolved.credentials.type === 'oauth-token' ? 'oauth' : 'api-key',
          maxTokens: DEFAULTS.MAX_TOKENS,
        },
        options.signal
      )) {
        if (delta.error) {
          options.onError(delta.error)
          return
        }

        if (delta.content) {
          fullContent += delta.content
          options.onContent(fullContent)
        }

        if (delta.done) {
          options.onComplete(fullContent, delta.usage?.totalTokens)
        }
      }
    } catch (err) {
      if (err instanceof Error && err.name === 'AbortError') {
        return // Silently handle abort
      }
      options.onError({
        type: 'unknown',
        message: err instanceof Error ? err.message : 'Unknown error',
      })
    }
  }

  // ==========================================================================
  // Message Creation Helpers
  // ==========================================================================

  async function createAssistantMessage(sessionId: string): Promise<Message> {
    const msg = await saveMessage({
      sessionId,
      role: 'assistant',
      content: '',
    })
    session.addMessage(msg)
    return msg
  }

  function buildApiMessages(excludeId?: string) {
    return session
      .messages()
      .filter((m) => m.id !== excludeId)
      .map((m) => ({
        role: m.role as 'user' | 'assistant' | 'system',
        content: m.content,
      }))
  }

  // ==========================================================================
  // Public API
  // ==========================================================================

  /**
   * Send a new message and stream the response
   */
  async function sendMessage(content: string, model?: string): Promise<void> {
    if (isStreaming()) return

    const targetModel = model || session.selectedModel()

    // Validate auth before creating messages
    const resolved = resolveAuth(targetModel)
    if (!resolved) {
      setError({
        type: 'auth',
        message: 'No credentials configured. Please add an API key in Settings.',
      })
      return
    }

    setError(null)
    setIsStreaming(true)
    abortRef.current = new AbortController()

    try {
      // Ensure session exists
      let sessionId = session.currentSession()?.id
      if (!sessionId) {
        const newSession = await session.createNewSession()
        sessionId = newSession.id
      }

      // Create user message
      const userMsg = await saveMessage({
        sessionId,
        role: 'user',
        content,
      })
      session.addMessage(userMsg)

      // Create assistant placeholder
      const assistantMsg = await createAssistantMessage(sessionId)

      // Stream response
      await streamResponse({
        sessionId,
        model: targetModel,
        messages: buildApiMessages(assistantMsg.id),
        onContent: (text) => session.updateMessageContent(assistantMsg.id, text),
        onComplete: async (text, tokens) => {
          await updateMessage(assistantMsg.id, {
            content: text,
            tokensUsed: tokens,
          })
        },
        onError: (err) => {
          setError(err)
          session.setMessageError(assistantMsg.id, {
            type: err.type,
            message: err.message,
            retryAfter: err.retryAfter,
            timestamp: Date.now(),
          })
        },
        signal: abortRef.current!.signal,
      })
    } finally {
      setIsStreaming(false)
      abortRef.current = null
    }
  }

  /**
   * Regenerate response without creating new user message
   * Used by retry and regenerate actions
   */
  async function regenerate(): Promise<void> {
    if (isStreaming()) return

    const targetModel = session.selectedModel()
    const sessionId = session.currentSession()?.id
    if (!sessionId) return

    setError(null)
    setIsStreaming(true)
    abortRef.current = new AbortController()

    try {
      const assistantMsg = await createAssistantMessage(sessionId)

      await streamResponse({
        sessionId,
        model: targetModel,
        messages: buildApiMessages(assistantMsg.id),
        onContent: (text) => session.updateMessageContent(assistantMsg.id, text),
        onComplete: async (text, tokens) => {
          await updateMessage(assistantMsg.id, {
            content: text,
            tokensUsed: tokens,
          })
        },
        onError: (err) => {
          setError(err)
          session.setMessageError(assistantMsg.id, {
            type: err.type,
            message: err.message,
            retryAfter: err.retryAfter,
            timestamp: Date.now(),
          })
        },
        signal: abortRef.current!.signal,
      })
    } finally {
      setIsStreaming(false)
      abortRef.current = null
    }
  }

  /**
   * Cancel ongoing stream
   */
  function cancel(): void {
    abortRef.current?.abort()
    setIsStreaming(false)
  }

  /**
   * Clear error state
   */
  function clearError(): void {
    setError(null)
  }

  /**
   * Retry a failed message
   */
  async function retryMessage(assistantMessageId: string): Promise<void> {
    const msgs = session.messages()
    const failedIndex = msgs.findIndex((m) => m.id === assistantMessageId)
    if (failedIndex === -1) return

    // Find preceding user message
    const userMsg = msgs
      .slice(0, failedIndex)
      .reverse()
      .find((m) => m.role === 'user')
    if (!userMsg) return

    // Clear error and mark retrying
    session.setRetryingMessageId(assistantMessageId)
    session.setMessageError(assistantMessageId, null)
    session.deleteMessage(assistantMessageId)

    try {
      await regenerate()
    } finally {
      session.setRetryingMessageId(null)
    }
  }

  /**
   * Edit a user message and resend from that point
   */
  async function editAndResend(messageId: string, newContent: string): Promise<void> {
    // Update message
    session.updateMessageContent(messageId, newContent)
    await updateMessage(messageId, {
      content: newContent,
      metadata: { editedAt: Date.now() },
    })

    // Delete messages after this one
    session.deleteMessagesAfter(messageId)
    session.stopEditing()

    // Regenerate response
    await regenerate()
  }

  /**
   * Regenerate an assistant response
   */
  async function regenerateResponse(assistantMessageId: string): Promise<void> {
    const msgs = session.messages()
    const index = msgs.findIndex((m) => m.id === assistantMessageId)
    if (index === -1) return

    // Find preceding user message to validate
    const userMsg = msgs
      .slice(0, index)
      .reverse()
      .find((m) => m.role === 'user')
    if (!userMsg) return

    // Delete the assistant message
    session.deleteMessage(assistantMessageId)

    // Regenerate
    await regenerate()
  }

  return {
    // State
    isStreaming,
    error,
    currentProvider,

    // Actions
    sendMessage,
    cancel,
    clearError,
    retryMessage,
    editAndResend,
    regenerateResponse,
  }
}
