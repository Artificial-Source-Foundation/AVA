/**
 * useChat Hook
 * Provider-agnostic chat hook with streaming support and tool integration
 */

import {
  estimateCost,
  executeTool,
  getToolDefinitions,
  resetToolCallCount,
  type ToolContext,
  undoLastAutoCommit,
} from '@estela/core'
import { createSignal } from 'solid-js'
import { checkAutoApproval, createApprovalGate } from '../lib/tool-approval'
import { getCoreCompactor, getCoreMemory, getCoreTracker } from '../services/core-bridge'
import { deleteMessageFromDb, saveMessage, updateMessage } from '../services/database'
import { createClient, getProviderForModel, type LLMClient } from '../services/llm/bridge'
import { notifyCompletion } from '../services/notifications'
import { useProject } from '../stores/project'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import type { Message, ToolCall } from '../types'
import type { LLMProvider, StreamError, ToolUseBlock } from '../types/llm'

// ============================================================================
// Types
// ============================================================================

interface StreamOptions {
  sessionId: string
  model: string
  messages: Array<{ role: 'user' | 'assistant' | 'system'; content: string | unknown[] }>
  onContent: (content: string) => void
  onComplete: (content: string, tokens?: number, toolCalls?: ToolCall[]) => void
  onError: (error: StreamError) => void
  onToolUpdate?: (toolCalls: ToolCall[]) => void
  signal: AbortSignal
  enableTools?: boolean
}

interface QueuedMessage {
  content: string
  model?: string
  images?: Array<{ data: string; mimeType: string; name?: string }>
}

// ============================================================================
// Hook Implementation
// ============================================================================

export interface ContextStats {
  total: number
  limit: number
  remaining: number
  percentUsed: number
}

export function useChat() {
  const [isStreaming, setIsStreaming] = createSignal(false)
  const [error, setError] = createSignal<StreamError | null>(null)
  const [currentProvider, setCurrentProvider] = createSignal<LLMProvider | null>(null)
  const [contextStats, setContextStats] = createSignal<ContextStats | null>(null)
  const [streamingTokenEstimate, setStreamingTokenEstimate] = createSignal(0)
  const [streamingStartedAt, setStreamingStartedAt] = createSignal<number | null>(null)
  const [activeToolCalls, setActiveToolCalls] = createSignal<ToolCall[]>([])
  const [messageQueue, setMessageQueue] = createSignal<QueuedMessage[]>([])

  const abortRef = { current: null as AbortController | null }
  const session = useSession()
  const { currentProject } = useProject()
  const settings = useSettings()
  const approval = createApprovalGate()

  /** Sync tracker stats → signal */
  function syncTrackerStats() {
    const tracker = getCoreTracker()
    if (!tracker) return
    const s = tracker.getStats()
    setContextStats({
      total: s.total,
      limit: s.limit,
      remaining: s.remaining,
      percentUsed: s.percentUsed,
    })
  }

  /**
   * Auto-compact conversation when context exceeds 80%.
   * Uses sliding window to trim to ~50%, syncs state + DB.
   */
  async function maybeCompact(): Promise<void> {
    const tracker = getCoreTracker()
    const compactor = getCoreCompactor()
    if (!tracker || !compactor || !compactor.needsCompaction(80)) return

    const currentMsgs = session.messages()
    if (currentMsgs.length <= 4) return

    // Convert frontend messages to core Message format
    const coreMessages = currentMsgs.map((m) => ({
      id: m.id,
      sessionId: m.sessionId,
      role: m.role as 'user' | 'assistant' | 'system',
      content: m.content,
      createdAt: m.createdAt,
    }))

    try {
      const result = await compactor.compact(coreMessages)
      if (result.tokensSaved === 0) return

      // Determine which messages were removed
      const keptIds = new Set(result.messages.map((m) => m.id))
      const removedMsgs = currentMsgs.filter((m) => !keptIds.has(m.id))

      // Update frontend state: keep only surviving messages
      session.setMessages(currentMsgs.filter((m) => keptIds.has(m.id)))

      // Sync database: delete removed messages
      await Promise.all(removedMsgs.map((m) => deleteMessageFromDb(m.id)))

      // Rebuild tracker with remaining messages
      tracker.clear()
      for (const m of result.messages) {
        tracker.addMessage(m.id, m.content)
      }
      syncTrackerStats()

      console.info(
        `[Compaction] Removed ${result.originalCount - result.compactedCount} messages, saved ~${result.tokensSaved} tokens (${result.strategyUsed})`
      )
    } catch (err) {
      console.warn('[Compaction] Failed:', err)
    }
  }

  // ==========================================================================
  // Lint Check Helpers (for iterative lint-fix loop)
  // ==========================================================================

  /** Extract the file path from a file-modifying tool's input */
  function getModifiedFilePath(toolName: string, input: Record<string, unknown>): string | null {
    if (toolName === 'write_file' || toolName === 'create_file')
      return (input.path as string) || null
    if (toolName === 'edit') return (input.filePath as string) || null
    if (toolName === 'apply_patch') return (input.filePath as string) || null
    return null
  }

  /** Run linter on a file and return errors, or null if clean */
  async function checkLintErrors(filePath: string, ctx: ToolContext): Promise<string | null> {
    try {
      const result = await executeTool(
        'bash',
        {
          command: `npx biome check "${filePath}" 2>&1 || npx eslint "${filePath}" 2>&1`,
          timeout: 10000,
        },
        ctx
      )
      if (!result.success && result.output) {
        return result.output.split('\n').slice(0, 50).join('\n')
      }
      return null
    } catch {
      return null
    }
  }

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
      workingDirectory: currentProject()?.directory || '.',
      signal: options.signal,
    }

    // Get tool definitions if tools are enabled
    const tools = options.enableTools !== false ? getToolDefinitions() : undefined

    // Accumulate all tool calls across streaming iterations
    const allToolCalls: ToolCall[] = []

    // Stream loop - may iterate multiple times if tools are used
    let continueStreaming = true
    while (continueStreaming) {
      continueStreaming = false
      const pendingToolUses: ToolUseBlock[] = []

      try {
        // Read generation settings at call time
        const gen = settings.settings().generation
        for await (const delta of client.stream(
          currentMessages as Array<{ role: 'user' | 'assistant' | 'system'; content: string }>,
          {
            provider,
            model: options.model,
            authMethod: 'api-key', // Core handles auth method internally
            maxTokens: gen.maxTokens,
            temperature: gen.temperature,
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

            // Create pending ToolCall and notify UI
            const input = delta.toolUse.input as Record<string, unknown>
            const tc: ToolCall = {
              id: delta.toolUse.id,
              name: delta.toolUse.name,
              args: input,
              status: 'pending',
              startedAt: Date.now(),
              filePath: getModifiedFilePath(delta.toolUse.name, input) ?? undefined,
            }
            allToolCalls.push(tc)
            options.onToolUpdate?.([...allToolCalls])
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
                // Find the matching ToolCall to update status
                const tc = allToolCalls.find((t) => t.id === toolUse.id)

                // Check auto-approval before executing
                const autoResult = checkAutoApproval(
                  toolUse.name,
                  toolUse.input as Record<string, unknown>,
                  settings.isToolAutoApproved
                )

                if (!autoResult.approved) {
                  const approved = await approval.requestApproval(
                    toolUse.name,
                    toolUse.input as Record<string, unknown>
                  )
                  if (!approved) {
                    if (tc) {
                      tc.status = 'error'
                      tc.error = 'User denied'
                      tc.completedAt = Date.now()
                    }
                    options.onToolUpdate?.([...allToolCalls])
                    toolResults.push({
                      type: 'tool_result',
                      tool_use_id: toolUse.id,
                      content: 'User denied tool execution',
                      is_error: true,
                    })
                    continue
                  }
                }

                // Mark running
                if (tc) tc.status = 'running'
                options.onToolUpdate?.([...allToolCalls])

                const result = await executeTool(toolUse.name, toolUse.input, toolCtx)
                let toolOutput = result.output

                // Lint check: if file was modified and autoFixLint is on, run linter
                if (result.success && settings.settings().agentLimits.autoFixLint) {
                  const filePath = getModifiedFilePath(
                    toolUse.name,
                    toolUse.input as Record<string, unknown>
                  )
                  if (filePath) {
                    const lintErrors = await checkLintErrors(filePath, toolCtx)
                    if (lintErrors) {
                      toolOutput += `\n\nLint errors found:\n${lintErrors}\nPlease fix these issues.`
                    }
                  }
                }

                // Update ToolCall with result
                if (tc) {
                  tc.status = result.success ? 'success' : 'error'
                  tc.output = toolOutput.slice(0, 2000) // Cap stored output
                  tc.error = result.error
                  tc.completedAt = Date.now()
                }
                options.onToolUpdate?.([...allToolCalls])

                toolResults.push({
                  type: 'tool_result',
                  tool_use_id: toolUse.id,
                  content: toolOutput,
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
              options.onComplete(
                fullContent,
                delta.usage?.totalTokens,
                allToolCalls.length > 0 ? allToolCalls : undefined
              )
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

  /**
   * Recall relevant memories for the current user message.
   * Returns a formatted system message string, or empty if unavailable.
   */
  async function recallMemoryContext(userMessage: string): Promise<string> {
    const memory = getCoreMemory()
    if (!memory) return ''

    try {
      const [similar, procedural] = await Promise.all([
        memory.recallSimilar(userMessage, 3),
        memory.recall({
          type: 'procedural',
          minImportance: 0.5,
          limit: 3,
          orderBy: 'importance',
          order: 'desc',
        }),
      ])

      if (similar.length === 0 && procedural.length === 0) return ''

      const parts: string[] = ['## Relevant Memories\n']

      if (similar.length > 0) {
        parts.push('### Past Experiences')
        for (const r of similar) {
          const pct = (r.similarity * 100).toFixed(0)
          parts.push(`- ${r.memory.content.slice(0, 200)} (${pct}% match)`)
        }
        parts.push('')
      }

      if (procedural.length > 0) {
        parts.push('### Learned Patterns')
        for (const p of procedural) {
          const meta = p.metadata
          const rate =
            meta.successRate != null ? ` (${(meta.successRate * 100).toFixed(0)}% success)` : ''
          parts.push(`- ${p.content.slice(0, 200)}${rate}`)
        }
      }

      return parts.join('\n')
    } catch {
      return '' // Graceful degradation — memory is optional
    }
  }

  async function buildApiMessages(excludeId?: string, userMessage?: string) {
    const msgs = session
      .messages()
      .filter((m) => m.id !== excludeId)
      .map((m) => {
        // Build multimodal content if message has images
        const imgs = (m.metadata?.images ?? []) as Array<{
          data: string
          mimeType: string
        }>
        if (imgs.length > 0) {
          return {
            role: m.role as 'user' | 'assistant' | 'system',
            content: [
              ...imgs.map((img) => ({
                type: 'image' as const,
                source: { type: 'base64' as const, media_type: img.mimeType, data: img.data },
              })),
              { type: 'text' as const, text: m.content },
            ] as unknown as string,
          }
        }
        return {
          role: m.role as 'user' | 'assistant' | 'system',
          content: m.content,
        }
      })

    // Prepend custom instructions as system message
    const instructions = settings.settings().generation.customInstructions.trim()
    if (instructions) {
      msgs.unshift({ role: 'system', content: instructions })
    }

    // Prepend memory context (before custom instructions so instructions take priority)
    if (userMessage) {
      const memoryContext = await recallMemoryContext(userMessage)
      if (memoryContext) {
        msgs.unshift({ role: 'system', content: memoryContext })
      }
    }

    return msgs
  }

  // ==========================================================================
  // Public API
  // ==========================================================================

  /**
   * Send a new message and stream the response
   */
  async function sendMessage(
    content: string,
    model?: string,
    images?: Array<{ data: string; mimeType: string; name?: string }>
  ): Promise<void> {
    if (isStreaming()) {
      setMessageQueue((prev) => [...prev, { content, model, images }])
      return
    }

    const targetModel = model || session.selectedModel()

    // Note: Auth validation happens in streamResponse via core client
    setError(null)
    setIsStreaming(true)
    setStreamingStartedAt(Date.now())
    setStreamingTokenEstimate(0)
    setActiveToolCalls([])
    abortRef.current = new AbortController()

    try {
      // Ensure session exists
      let sessionId = session.currentSession()?.id
      if (!sessionId) {
        const newSession = await session.createNewSession()
        sessionId = newSession.id
      }

      // Create user message (store images in metadata)
      const userMsg = await saveMessage({
        sessionId,
        role: 'user',
        content,
        metadata: images?.length ? { images } : undefined,
      })
      session.addMessage(userMsg)
      getCoreTracker()?.addMessage(userMsg.id, content)
      syncTrackerStats()

      // Create assistant placeholder
      const assistantMsg = await createAssistantMessage(sessionId)

      // Stream response
      await streamResponse({
        sessionId,
        model: targetModel,
        messages: await buildApiMessages(assistantMsg.id, content),
        onContent: (text) => {
          session.updateMessageContent(assistantMsg.id, text)
          setStreamingTokenEstimate(Math.ceil(text.length / 4))
        },
        onToolUpdate: (toolCalls) => {
          setActiveToolCalls(toolCalls)
          session.updateMessage(assistantMsg.id, { toolCalls })
        },
        onComplete: async (text, tokens, toolCalls) => {
          setStreamingTokenEstimate(0)
          setActiveToolCalls([])
          // Estimate cost from token usage
          const totalTokens = tokens || Math.ceil(text.length / 4)
          const inputTokens = Math.floor(totalTokens * 0.7)
          const outputTokens = Math.ceil(totalTokens * 0.3)
          const cost = estimateCost(targetModel, inputTokens, outputTokens) ?? undefined
          const meta: Record<string, unknown> = { costUSD: cost, model: targetModel }
          if (toolCalls) meta.toolCalls = toolCalls
          await updateMessage(assistantMsg.id, {
            content: text,
            tokensUsed: tokens,
            metadata: meta,
          })
          session.updateMessage(assistantMsg.id, { costUSD: cost, model: targetModel, toolCalls })
          getCoreTracker()?.addMessage(assistantMsg.id, text)
          syncTrackerStats()
          await maybeCompact()
          notifyCompletion('Chat complete', text.slice(0, 100))
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
      setStreamingStartedAt(null)
      abortRef.current = null
      void processQueue()
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
    setStreamingStartedAt(Date.now())
    setActiveToolCalls([])
    abortRef.current = new AbortController()

    try {
      const assistantMsg = await createAssistantMessage(sessionId)

      await streamResponse({
        sessionId,
        model: targetModel,
        messages: await buildApiMessages(assistantMsg.id),
        onContent: (text) => session.updateMessageContent(assistantMsg.id, text),
        onToolUpdate: (toolCalls) => {
          setActiveToolCalls(toolCalls)
          session.updateMessage(assistantMsg.id, { toolCalls })
        },
        onComplete: async (text, tokens, toolCalls) => {
          setActiveToolCalls([])
          const meta: Record<string, unknown> = {}
          if (toolCalls) meta.toolCalls = toolCalls
          await updateMessage(assistantMsg.id, {
            content: text,
            tokensUsed: tokens,
            metadata: Object.keys(meta).length > 0 ? meta : undefined,
          })
          syncTrackerStats()
          await maybeCompact()
          notifyCompletion('Regeneration complete', text.slice(0, 100))
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
      setStreamingStartedAt(null)
      abortRef.current = null
    }
  }

  /**
   * Cancel ongoing stream and clear any queued messages
   */
  function cancel(): void {
    abortRef.current?.abort()
    setMessageQueue([])
    setIsStreaming(false)
  }

  /** Process the next queued follow-up message */
  async function processQueue(): Promise<void> {
    const queue = messageQueue()
    if (queue.length === 0) return
    const next = queue[0]
    setMessageQueue((prev) => prev.slice(1))
    await sendMessage(next.content, next.model, next.images)
  }

  /**
   * Steer: cancel current stream and send a new message immediately.
   * Clears any queued follow-ups — the steer message takes priority.
   */
  function steer(
    content: string,
    model?: string,
    images?: Array<{ data: string; mimeType: string; name?: string }>
  ): void {
    setMessageQueue([{ content, model, images }])
    abortRef.current?.abort()
    setIsStreaming(false)
  }

  /** Clear all queued messages */
  function clearQueue(): void {
    setMessageQueue([])
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
   * Undo the last auto-committed AI edit.
   * Finds the most recent estela-prefixed commit and reverts it.
   * @returns true if undo succeeded, false otherwise
   */
  async function undoLastEdit(): Promise<{ success: boolean; message: string }> {
    const cwd = currentProject()?.directory
    if (!cwd) {
      return { success: false, message: 'No project directory' }
    }
    const result = await undoLastAutoCommit(cwd)
    return {
      success: result.success,
      message: result.success
        ? `Reverted last AI edit: ${result.output}`
        : result.error || 'No AI edit to undo',
    }
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
    contextStats,
    streamingTokenEstimate,
    streamingStartedAt,
    activeToolCalls,
    pendingApproval: approval.pendingApproval,

    // Queue
    queuedCount: () => messageQueue().length,
    steer,
    clearQueue,

    // Actions
    sendMessage,
    cancel,
    clearError,
    retryMessage,
    editAndResend,
    regenerateResponse,
    undoLastEdit,
    resolveApproval: approval.resolveApproval,
  }
}
