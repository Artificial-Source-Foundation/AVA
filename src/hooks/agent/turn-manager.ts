/**
 * Turn Manager
 *
 * Manages the lifecycle of agent runs: queuing, starting, cancelling, and
 * steering. Orchestrates session/message creation and delegates actual
 * execution to the streaming module.
 *
 * Higher-level message operations (retry, edit-and-resend, regenerate, undo)
 * are in message-actions.ts.
 */

import type { AgentConfig, AgentEvent, AgentResult } from '@ava/core-v2/agent'
import { generateTitle } from '@ava/core-v2/agent'
import { batch } from 'solid-js'
import { DEFAULTS } from '../../config/constants'
import { getCoreBudget } from '../../services/core-bridge'
import { saveMessage } from '../../services/database'
import { flushLogs, logError, logInfo } from '../../services/logger'
import type { Message } from '../../types'
import { buildConversationHistory } from '../chat/history-builder'
import type { QueuedMessage } from '../chat/types'
import type { ConfigDeps } from './config-builder'
import {
  editAndResend as _editAndResend,
  regenerateResponse as _regenerateResponse,
  retryMessage as _retryMessage,
  undoLastEdit as _undoLastEdit,
  type MessageActionDeps,
} from './message-actions'
import { execute } from './streaming'
import type { AgentRefs, AgentSignals, SessionBridge } from './types'

// ============================================================================
// Message Helpers
// ============================================================================

/** Create and persist a user message, adding it to the session store. */
export async function createUserMessage(
  sessionId: string,
  content: string,
  session: SessionBridge,
  images?: QueuedMessage['images']
): Promise<Message> {
  const msg = await saveMessage({
    sessionId,
    role: 'user',
    content,
    metadata: images?.length ? { images } : undefined,
  })
  session.addMessage(msg)
  return msg
}

/** Create and persist an empty assistant message placeholder. */
export async function createAssistantMessage(
  sessionId: string,
  session: SessionBridge
): Promise<Message> {
  const msg = await saveMessage({ sessionId, role: 'assistant', content: '' })
  session.addMessage(msg)
  return msg
}

// ============================================================================
// Turn Manager
// ============================================================================

export interface TurnManagerDeps {
  signals: AgentSignals
  refs: AgentRefs
  session: SessionBridge
  handleAgentEvent: (event: AgentEvent) => void
  configDeps: ConfigDeps
  teamStore: { clearTeam: () => void }
}

export interface TurnManager {
  run: (goal: string, config?: Partial<AgentConfig>) => Promise<AgentResult | null>
  cancel: () => void
  steer: (
    content: string,
    model?: string,
    images?: Array<{ data: string; mimeType: string; name?: string }>
  ) => void
  processQueue: () => Promise<void>
  clearQueue: () => void
  removeFromQueue: (index: number) => void
  retryMessage: (assistantMessageId: string) => Promise<void>
  editAndResend: (messageId: string, newContent: string) => Promise<void>
  regenerateResponse: (assistantMessageId: string) => Promise<void>
  undoLastEdit: () => Promise<{ success: boolean; message: string }>
}

/**
 * Create the turn manager that drives all agent interaction flows.
 */
export function createTurnManager(deps: TurnManagerDeps): TurnManager {
  const { signals, refs, session, handleAgentEvent, configDeps, teamStore } = deps

  // Shared deps for message-actions functions
  const actionDeps: MessageActionDeps = {
    signals,
    refs,
    session,
    handleAgentEvent,
    configDeps,
  }

  // ====================================================================
  // Queue Processing
  // ====================================================================

  async function processQueue(): Promise<void> {
    const queue = signals.messageQueue()
    if (queue.length === 0) return
    const next = queue[0]!
    signals.setMessageQueue((prev) => prev.slice(1))
    await run(next.content)
  }

  // ====================================================================
  // Public: run() — primary entry point
  // ====================================================================

  async function run(goal: string, config?: Partial<AgentConfig>): Promise<AgentResult | null> {
    if (signals.isRunning()) {
      signals.setMessageQueue((prev) => [...prev, { content: goal }])
      logInfo('Agent', 'Queued message', { queueLength: signals.messageQueue().length + 1 })
      return null
    }

    if (!configDeps.currentProjectDir()) {
      logError('Agent', 'run() blocked — no project open')
      void flushLogs()
      signals.setLastError('Open a project before running agent mode.')
      return null
    }

    batch(() => {
      signals.setIsRunning(true)
      signals.setCurrentThought('')
      signals.setLastError(null)
      signals.setError(null)
      signals.setDoomLoopDetected(false)
      signals.setActiveToolCalls([])
      signals.setStreamingTokenEstimate(0)
      signals.setStreamingStartedAt(Date.now())
    })

    teamStore.clearTeam()
    refs.abortRef.current = new AbortController()

    try {
      let sessionId = session.currentSession()?.id
      if (!sessionId) {
        const newSession = await session.createNewSession()
        sessionId = newSession.id
      }

      // Build structured conversation history BEFORE adding new messages
      const priorMessages = buildConversationHistory(session.messages())

      const userMsg = await createUserMessage(sessionId, goal, session)
      getCoreBudget()?.addMessage(userMsg.id, goal)

      // Auto-title new chats from first user message using AI
      const autoTitleEnabled = configDeps.settingsRef.settings().behavior.sessionAutoTitle
      const currentSession = session.currentSession()
      const defaultName = DEFAULTS.SESSION_NAME
      const sessionName = currentSession?.name?.trim()
      if (autoTitleEnabled && currentSession?.id === sessionId && sessionName === defaultName) {
        void generateTitle(goal).then((title) => {
          if (title) {
            void session.renameSession(sessionId, title)
          }
        })
      }

      const assistantMsg = await createAssistantMessage(sessionId, session)
      const model = config?.model || session.selectedModel()
      session.updateMessage(assistantMsg.id, { model })

      return await execute(
        goal,
        sessionId,
        assistantMsg,
        priorMessages,
        model,
        { signals, refs, session, handleAgentEvent, configDeps },
        config
      )
    } catch (err) {
      if (err instanceof Error && err.name === 'AbortError') {
        logInfo('Agent', 'Run aborted')
        return null
      }
      const errorMsg = err instanceof Error ? err.message : String(err)
      logError('Agent', '═══ AGENT RUN FAILED ═══', {
        error: errorMsg,
        stack: err instanceof Error ? err.stack : undefined,
      })
      void flushLogs()
      batch(() => {
        signals.setLastError(errorMsg)
        signals.setError({ type: 'unknown', message: errorMsg })
      })
      return null
    } finally {
      batch(() => {
        signals.setIsRunning(false)
        signals.setStreamingStartedAt(null)
      })
      refs.abortRef.current = null
      void processQueue()
    }
  }

  // ====================================================================
  // Cancel / Steer / Queue
  // ====================================================================

  function cancel(): void {
    refs.abortRef.current?.abort()
    refs.abortRef.current = null
    refs.executorRef.current = null
    batch(() => {
      signals.setMessageQueue([])
      signals.setActiveToolCalls([])
      signals.setIsRunning(false)
      signals.setStreamingStartedAt(null)
    })
    logInfo('Agent', 'Cancel')
  }

  function steer(
    content: string,
    _model?: string,
    _images?: Array<{ data: string; mimeType: string; name?: string }>
  ): void {
    if (refs.executorRef.current) {
      refs.executorRef.current.steer(content)
      logInfo('Agent', 'Steer via executor', { content: content.slice(0, 80) })
    } else {
      refs.abortRef.current?.abort()
      batch(() => {
        signals.setMessageQueue([{ content }])
        signals.setIsRunning(false)
        signals.setStreamingStartedAt(null)
      })
      logInfo('Agent', 'Steer via queue', { queued: 1 })
    }
  }

  function clearQueue(): void {
    batch(() => signals.setMessageQueue([]))
  }

  function removeFromQueue(index: number): void {
    batch(() => signals.setMessageQueue((prev) => prev.filter((_, i) => i !== index)))
  }

  // ====================================================================
  // Return — delegates message actions to extracted module
  // ====================================================================

  return {
    run,
    cancel,
    steer,
    processQueue,
    clearQueue,
    removeFromQueue,
    retryMessage: (id: string) => _retryMessage(actionDeps, id),
    editAndResend: (id: string, content: string) => _editAndResend(actionDeps, id, content),
    regenerateResponse: (id: string) => _regenerateResponse(actionDeps, id),
    undoLastEdit: () => _undoLastEdit(configDeps),
  }
}
