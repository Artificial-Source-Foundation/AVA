/**
 * Send & Regenerate
 * Core send-message and regenerate-response functions that drive the stream lifecycle.
 */

import { batch } from 'solid-js'
import { DEFAULTS, LIMITS } from '../../config/constants'
import { estimateCost } from '../../lib/cost'
import { getCoreBudget } from '../../services/core-bridge'
import { saveMessage, updateMessage } from '../../services/database'
import { logError, logInfo } from '../../services/logger'
import { notifyCompletion } from '../../services/notifications'
import type { Message } from '../../types'
import { buildApiMessages, maybeCompact, syncTrackerStats } from './context-tracking'
import { streamResponse } from './stream-lifecycle'
import type { ChatDeps } from './types'

// ============================================================================
// Helper: Create assistant placeholder message
// ============================================================================

export async function createAssistantMessage(deps: ChatDeps, sessionId: string): Promise<Message> {
  const msg = await saveMessage({
    sessionId,
    role: 'assistant',
    content: '',
  })
  deps.session.addMessage(msg)
  return msg
}

// ============================================================================
// Send Message
// ============================================================================

export async function sendMessage(
  deps: ChatDeps,
  content: string,
  model?: string,
  images?: Array<{ data: string; mimeType: string; name?: string }>,
  processQueueFn?: (deps: ChatDeps) => Promise<void>
): Promise<void> {
  if (deps.isStreaming()) {
    deps.setMessageQueue((prev) => [...prev, { content, model, images }])
    logInfo(deps.LOG_SRC, 'Queued message', {
      queueLength: deps.messageQueue().length + 1,
    })
    return
  }

  const targetModel = model || deps.session.selectedModel()

  deps.setError(null)
  deps.setIsStreaming(true)
  deps.setStreamingStartedAt(Date.now())
  deps.setStreamingTokenEstimate(0)
  deps.setActiveToolCalls([])
  deps.abortRef.current = new AbortController()
  logInfo(deps.LOG_SRC, 'Send message', {
    model: targetModel,
    sessionId: deps.session.currentSession()?.id ?? 'new',
  })

  let streamFlushTimer: ReturnType<typeof setTimeout> | null = null
  let thinkingFlushTimer: ReturnType<typeof setTimeout> | null = null

  try {
    // Ensure session exists
    let sessionId = deps.session.currentSession()?.id
    if (!sessionId) {
      const newSession = await deps.session.createNewSession()
      sessionId = newSession.id
    }

    // Create user message (store images in metadata)
    const userMsg = await saveMessage({
      sessionId,
      role: 'user',
      content,
      metadata: images?.length ? { images } : undefined,
    })
    deps.session.addMessage(userMsg)
    getCoreBudget()?.addMessage(userMsg.id, content)
    syncTrackerStats(deps)

    // Auto-title new chats from first user message when enabled.
    const autoTitleEnabled = deps.settings.settings().behavior.sessionAutoTitle
    const currentSession = deps.session.currentSession()
    if (
      autoTitleEnabled &&
      currentSession?.id === sessionId &&
      currentSession.name.trim() === DEFAULTS.SESSION_NAME
    ) {
      const normalizedTitle = content.replace(/\s+/g, ' ').trim()
      if (normalizedTitle) {
        const nextTitle = normalizedTitle.slice(0, LIMITS.MESSAGE_PREVIEW_LENGTH).trim()
        if (nextTitle) {
          await deps.session.renameSession(sessionId, nextTitle)
        }
      }
    }

    // Create assistant placeholder with model set immediately for UI display
    const assistantMsg = await createAssistantMessage(deps, sessionId)
    deps.session.updateMessage(assistantMsg.id, { model: targetModel })

    // Stream response with buffered UI updates
    let latestStreamText = ''
    let lastFlushedStreamText = ''
    let latestThinking = ''
    let lastFlushedThinking = ''

    await streamResponse(
      {
        sessionId,
        model: targetModel,
        messages: await buildApiMessages(deps, assistantMsg.id),
        onContent: (text) => {
          latestStreamText = text
          // Flush first delta immediately for instant feedback
          if (lastFlushedStreamText === '' && latestStreamText !== '') {
            deps.session.updateMessageContent(assistantMsg.id, latestStreamText)
            deps.setStreamingTokenEstimate(Math.ceil(latestStreamText.length / 4))
            lastFlushedStreamText = latestStreamText
            return
          }
          if (streamFlushTimer !== null) return

          streamFlushTimer = setTimeout(() => {
            if (latestStreamText !== lastFlushedStreamText) {
              deps.session.updateMessageContent(assistantMsg.id, latestStreamText)
              deps.setStreamingTokenEstimate(Math.ceil(latestStreamText.length / 4))
              lastFlushedStreamText = latestStreamText
            }
            streamFlushTimer = null
          }, 100)
        },
        onThinking: (thinking) => {
          latestThinking = thinking
          // Flush first delta immediately for instant feedback
          if (lastFlushedThinking === '' && latestThinking !== '') {
            deps.session.updateMessage(assistantMsg.id, {
              metadata: { thinking: latestThinking },
            })
            lastFlushedThinking = latestThinking
            return
          }
          if (thinkingFlushTimer !== null) return
          thinkingFlushTimer = setTimeout(() => {
            if (latestThinking !== lastFlushedThinking) {
              deps.session.updateMessage(assistantMsg.id, {
                metadata: { thinking: latestThinking },
              })
              lastFlushedThinking = latestThinking
            }
            thinkingFlushTimer = null
          }, 150)
        },
        onToolUpdate: (toolCalls) => {
          batch(() => {
            deps.setActiveToolCalls(toolCalls)
            deps.session.updateMessage(assistantMsg.id, { toolCalls })
          })
        },
        onComplete: async (text, tokens, toolCalls) => {
          if (streamFlushTimer !== null) {
            clearTimeout(streamFlushTimer)
            streamFlushTimer = null
          }
          if (thinkingFlushTimer !== null) {
            clearTimeout(thinkingFlushTimer)
            thinkingFlushTimer = null
          }
          // Flush any pending thinking to reactive state
          if (latestThinking && latestThinking !== lastFlushedThinking) {
            deps.session.updateMessage(assistantMsg.id, {
              metadata: { thinking: latestThinking },
            })
          }

          if (text !== lastFlushedStreamText) {
            deps.session.updateMessageContent(assistantMsg.id, text)
            lastFlushedStreamText = text
          }

          deps.setStreamingTokenEstimate(0)
          deps.setActiveToolCalls([])
          const totalTokens = tokens || Math.ceil(text.length / 4)
          const inputTokens = Math.floor(totalTokens * 0.7)
          const outputTokens = Math.ceil(totalTokens * 0.3)
          const cost = estimateCost(targetModel, inputTokens, outputTokens) ?? undefined
          const meta: Record<string, unknown> = { costUSD: cost, model: targetModel }
          if (toolCalls) meta.toolCalls = toolCalls
          if (latestThinking) meta.thinking = latestThinking
          await updateMessage(assistantMsg.id, {
            content: text,
            tokensUsed: tokens,
            metadata: meta,
          })
          deps.session.updateMessage(assistantMsg.id, {
            costUSD: cost,
            model: targetModel,
            toolCalls,
          })
          getCoreBudget()?.addMessage(assistantMsg.id, text)
          syncTrackerStats(deps)
          await maybeCompact(deps)
          void notifyCompletion(
            'Chat complete',
            text.slice(0, 100),
            deps.settings.settings().notifications
          )
          logInfo(deps.LOG_SRC, 'Message complete', {
            model: targetModel,
            tokens: tokens ?? null,
            toolCalls: toolCalls?.length ?? 0,
          })
        },
        onError: (err) => {
          deps.setError(err)
          deps.session.setMessageError(assistantMsg.id, {
            type: err.type,
            message: err.message,
            retryAfter: err.retryAfter,
            timestamp: Date.now(),
          })
          logError(deps.LOG_SRC, 'Message failed', err)
        },
        signal: deps.abortRef.current!.signal,
      },
      deps
    )
  } finally {
    if (streamFlushTimer !== null) {
      clearTimeout(streamFlushTimer)
      streamFlushTimer = null
    }
    if (thinkingFlushTimer !== null) {
      clearTimeout(thinkingFlushTimer)
      thinkingFlushTimer = null
    }
    deps.setIsStreaming(false)
    deps.setStreamingStartedAt(null)
    deps.abortRef.current = null
    if (processQueueFn) void processQueueFn(deps)
  }
}

// ============================================================================
// Regenerate (no new user message)
// ============================================================================

export async function regenerate(deps: ChatDeps): Promise<void> {
  if (deps.isStreaming()) return

  const targetModel = deps.session.selectedModel()
  const sessionId = deps.session.currentSession()?.id
  if (!sessionId) return

  deps.setError(null)
  deps.setIsStreaming(true)
  deps.setStreamingStartedAt(Date.now())
  deps.setActiveToolCalls([])
  deps.abortRef.current = new AbortController()
  logInfo(deps.LOG_SRC, 'Regenerate start', { sessionId })

  try {
    const assistantMsg = await createAssistantMessage(deps, sessionId)

    await streamResponse(
      {
        sessionId,
        model: targetModel,
        messages: await buildApiMessages(deps, assistantMsg.id),
        onContent: (text) => deps.session.updateMessageContent(assistantMsg.id, text),
        onToolUpdate: (toolCalls) => {
          batch(() => {
            deps.setActiveToolCalls(toolCalls)
            deps.session.updateMessage(assistantMsg.id, { toolCalls })
          })
        },
        onComplete: async (text, tokens, toolCalls) => {
          deps.setActiveToolCalls([])
          const meta: Record<string, unknown> = {}
          if (toolCalls) meta.toolCalls = toolCalls
          await updateMessage(assistantMsg.id, {
            content: text,
            tokensUsed: tokens,
            metadata: Object.keys(meta).length > 0 ? meta : undefined,
          })
          syncTrackerStats(deps)
          await maybeCompact(deps)
          void notifyCompletion(
            'Regeneration complete',
            text.slice(0, 100),
            deps.settings.settings().notifications
          )
          logInfo(deps.LOG_SRC, 'Regenerate complete', {
            sessionId,
            tokens: tokens ?? null,
            toolCalls: toolCalls?.length ?? 0,
          })
        },
        onError: (err) => {
          deps.setError(err)
          deps.session.setMessageError(assistantMsg.id, {
            type: err.type,
            message: err.message,
            retryAfter: err.retryAfter,
            timestamp: Date.now(),
          })
          logError(deps.LOG_SRC, 'Regenerate failed', err)
        },
        signal: deps.abortRef.current!.signal,
      },
      deps
    )
  } finally {
    deps.setIsStreaming(false)
    deps.setStreamingStartedAt(null)
    deps.abortRef.current = null
  }
}
