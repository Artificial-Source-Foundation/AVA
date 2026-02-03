/**
 * useChat Hook
 * Provider-agnostic chat hook with streaming support and tool integration
 */

import { executeTool, getToolDefinitions, resetToolCallCount, type ToolContext } from '@estela/core'
import { createSignal } from 'solid-js'
import { DEFAULTS } from '../config/constants'
import { saveMessage, updateMessage } from '../services/database'
import { createClient, getProviderForModel, type LLMClient } from '../services/llm/bridge'
import { useSession } from '../stores/session'
import type { Message } from '../types'
import type { LLMProvider, StreamError, ToolUseBlock } from '../types/llm'

// ============================================================================
// Types
// ============================================================================

interface StreamOptions {
  sessionId: string
  model: string
  messages: Array<{ role: 'user' | 'assistant' | 'system'; content: string | unknown[] }>
  onContent: (content: string) => void
  onComplete: (content: string, tokens?: number) => void
  onError: (error: StreamError) => void
  signal: AbortSignal
  enableTools?: boolean
}

/** Get the working directory for tool execution */
function getWorkingDirectory(): string {
  // TODO: Use Tauri path API to get actual working directory
  // For now, default to process.cwd() equivalent or user's home
  return '.'
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
    // Get provider for the model
    const provider = getProviderForModel(options.model)
    setCurrentProvider(provider)

    // Create client via core (will handle auth internally)
    let client: LLMClient
    try {
      client = await createClient(options.model)
    } catch (err) {
      options.onError({
        type: 'auth',
        message: err instanceof Error ? err.message : 'Failed to create client',
      })
      return
    }

    // Reset tool call counter at the start of each message turn
    resetToolCallCount()

    // Build messages array (may include tool results)
    const currentMessages = [...options.messages]
    let fullContent = ''

    // Tool execution context
    const toolCtx: ToolContext = {
      sessionId: options.sessionId,
      workingDirectory: getWorkingDirectory(),
      signal: options.signal,
    }

    // Get tool definitions if tools are enabled
    const tools = options.enableTools !== false ? getToolDefinitions() : undefined

    // Stream loop - may iterate multiple times if tools are used
    let continueStreaming = true
    while (continueStreaming) {
      continueStreaming = false
      const pendingToolUses: ToolUseBlock[] = []

      try {
        for await (const delta of client.stream(
          currentMessages as Array<{ role: 'user' | 'assistant' | 'system'; content: string }>,
          {
            provider,
            model: options.model,
            authMethod: 'api-key', // Core handles auth method internally
            maxTokens: DEFAULTS.MAX_TOKENS,
            tools,
          },
          options.signal
        )) {
          if (delta.error) {
            options.onError(delta.error)
            return
          }

          // Handle text content
          if (delta.content) {
            fullContent += delta.content
            options.onContent(fullContent)
          }

          // Handle tool use
          if (delta.toolUse) {
            pendingToolUses.push(delta.toolUse)
          }

          if (delta.done) {
            // If there are pending tool uses, execute them
            if (pendingToolUses.length > 0) {
              // Add assistant message with tool uses to conversation
              const assistantContent = [
                ...(fullContent ? [{ type: 'text' as const, text: fullContent }] : []),
                ...pendingToolUses,
              ]
              currentMessages.push({
                role: 'assistant',
                content: assistantContent,
              })

              // Execute each tool and collect results
              const toolResults: Array<{
                type: 'tool_result'
                tool_use_id: string
                content: string
                is_error?: boolean
              }> = []

              for (const toolUse of pendingToolUses) {
                const result = await executeTool(toolUse.name, toolUse.input, toolCtx)
                toolResults.push({
                  type: 'tool_result',
                  tool_use_id: toolUse.id,
                  content: result.output,
                  is_error: !result.success,
                })
              }

              // Add tool results as user message
              currentMessages.push({
                role: 'user',
                content: toolResults,
              })

              // Continue streaming to get assistant's response
              continueStreaming = true
            } else {
              // No tools, we're done
              options.onComplete(fullContent, delta.usage?.totalTokens)
            }
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
        return
      }
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

    // Note: Auth validation happens in streamResponse via core client
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
