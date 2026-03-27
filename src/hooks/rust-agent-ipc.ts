import { isTauri, invoke as tauriInvoke } from '@tauri-apps/api/core'
import { listen, type UnlistenFn } from '@tauri-apps/api/event'
import type { Accessor, Setter } from 'solid-js'
import { batch } from 'solid-js'
import { apiInvoke, createEventSocket } from '../lib/api-client'
import { log } from '../lib/logger'
import type { ToolCall } from '../types'
import type { AgentEvent, SubmitGoalResult } from '../types/rust-ipc'
import type { CompletionResolver, StreamingMetrics } from './rust-agent-events'

/** Invoke the backend — Tauri IPC or HTTP API depending on runtime. */
function invoke<T>(cmd: string, args?: Record<string, unknown>): Promise<T> {
  if (isTauri()) {
    return args ? tauriInvoke<T>(cmd, args) : tauriInvoke<T>(cmd)
  }
  return apiInvoke<T>(cmd, args)
}

interface IpcDeps {
  metrics: StreamingMetrics
  completion: CompletionResolver
  isRunning: Accessor<boolean>
  setIsRunning: Setter<boolean>
  setError: Setter<string | null>
  setLastResult: Setter<SubmitGoalResult | null>
  setActiveToolCalls: Setter<ToolCall[]>
  handleAgentEvent: (event: AgentEvent) => void
  resetState: () => void
}

export interface AgentIpc {
  run: (
    goal: string,
    opts?: {
      provider?: string
      model?: string
      maxTurns?: number
      thinkingLevel?: string
      sessionId?: string
    }
  ) => Promise<SubmitGoalResult | null>
  editAndResendRun: (messageId: string, newContent: string) => Promise<SubmitGoalResult | null>
  cancel: () => Promise<void>
  steer: (message: string) => Promise<void>
  followUp: (message: string) => Promise<void>
  postComplete: (message: string, group?: number) => Promise<void>
  endRun: () => void
  resetState: () => void
  attachListener: () => Promise<void>
  detachListener: () => void
  destroyListener: () => void
}

/**
 * Create IPC functions for agent communication.
 * Manages Tauri listeners and WebSocket connections.
 */
export function createAgentIpc(deps: IpcDeps): AgentIpc {
  const {
    metrics,
    completion,
    isRunning,
    setIsRunning,
    setError,
    setLastResult,
    setActiveToolCalls,
    handleAgentEvent,
    resetState,
  } = deps

  let unlisten: UnlistenFn | null = null
  let eventSocket: WebSocket | null = null
  let socketGeneration = 0

  const isCurrentSocket = (ws: WebSocket, generation: number): boolean =>
    eventSocket === ws && socketGeneration === generation

  const attachListener = async (): Promise<void> => {
    if (isTauri()) {
      // Tauri: always recreate the listener (cheap, event-based)
      if (unlisten) {
        unlisten()
        unlisten = null
      }
      unlisten = await listen<AgentEvent>('agent-event', (evt) => {
        handleAgentEvent(evt.payload)
      })
    } else if (eventSocket && eventSocket.readyState === WebSocket.OPEN) {
      // Browser: reuse existing open WebSocket connection
      return
    } else {
      // Browser mode — connect via WebSocket (first run or reconnect)
      if (eventSocket) {
        const staleSocket = eventSocket
        eventSocket = null
        socketGeneration += 1
        staleSocket.close()
      }
      log.info('ws', 'Connecting to event WebSocket')
      const ws = createEventSocket()
      const generation = ++socketGeneration
      eventSocket = ws
      ws.onmessage = (evt) => {
        if (!isCurrentSocket(ws, generation)) return
        try {
          const event = JSON.parse(evt.data as string) as AgentEvent
          log.debug('ws', 'Message received', { type: event.type })
          handleAgentEvent(event)
        } catch {
          // Ignore malformed messages
        }
      }
      ws.onerror = () => {
        if (!isCurrentSocket(ws, generation)) return
        log.error('ws', 'WebSocket connection error')
        batch(() => {
          setError('WebSocket connection error')
          setIsRunning(false)
        })
        if (completion.resolve) {
          completion.resolve(null)
          completion.resolve = null
        }
      }
      ws.onclose = () => {
        if (!isCurrentSocket(ws, generation)) return
        log.warn('ws', 'WebSocket disconnected')
        // If the socket closes while the agent is still running, treat it as an error
        if (isRunning()) {
          batch(() => {
            setError('WebSocket connection closed unexpectedly')
            setIsRunning(false)
          })
        }
        if (completion.resolve) {
          completion.resolve(null)
          completion.resolve = null
        }
      }
      // Wait for the connection to open before returning
      await new Promise<void>((resolve, reject) => {
        ws.addEventListener(
          'open',
          () => {
            if (!isCurrentSocket(ws, generation)) return
            resolve()
          },
          { once: true }
        )
        ws.addEventListener(
          'error',
          () => {
            if (!isCurrentSocket(ws, generation)) return
            reject(new Error('WebSocket connection failed'))
          },
          {
            once: true,
          }
        )
      })
    }
  }

  /** Detach Tauri listener only. WebSocket stays alive for reuse across runs. */
  const detachListener = (): void => {
    if (unlisten) {
      unlisten()
      unlisten = null
    }
    // Don't close eventSocket here — it's reused across runs.
    // Only closed on component cleanup (onCleanup).
  }

  /** Full cleanup — close everything including WebSocket. */
  const destroyListener = (): void => {
    detachListener()
    if (eventSocket) {
      socketGeneration += 1
      eventSocket.close()
      eventSocket = null
    }
  }

  /** Reset streaming metrics for a new run. */
  const resetMetrics = (): void => {
    metrics.chunkCount = 0
    metrics.totalTextLen = 0
    metrics.runStartTime = Date.now()
    metrics.firstTokenLogged = false
    metrics.pendingToolNames = []
  }

  const run = async (
    goal: string,
    opts?: {
      provider?: string
      model?: string
      maxTurns?: number
      thinkingLevel?: string
      sessionId?: string
    }
  ): Promise<SubmitGoalResult | null> => {
    resetState()
    setIsRunning(true)
    resetMetrics()
    log.info('streaming', 'Stream started', { model: opts?.model, provider: opts?.provider })
    try {
      await attachListener()
      const submitArgs = {
        args: {
          goal,
          maxTurns: opts?.maxTurns ?? 0,
          provider: opts?.provider ?? null,
          model: opts?.model ?? null,
          thinkingLevel: opts?.thinkingLevel ?? null,
          sessionId: opts?.sessionId ?? null,
        },
      }

      if (isTauri()) {
        // Tauri mode: invoke blocks until the agent finishes.
        // Do NOT set isRunning(false) here — let the caller (useAgent) finalize
        // the message content first, then call endRun() in a batch to avoid
        // a flash where isActiveStreaming=false but the message is still empty.
        const result = await invoke<SubmitGoalResult>('submit_goal', submitArgs)
        setLastResult(result)
        return result
      }

      // Web mode: the HTTP call returns immediately while the agent runs async.
      // We need to wait for the complete/error event via WebSocket.
      const completionPromise = new Promise<SubmitGoalResult | null>((resolve) => {
        completion.resolve = resolve
      })

      // Fire the HTTP request (returns immediately with session ID)
      const submitResult = await invoke<SubmitGoalResult>('submit_goal', submitArgs)

      // Now wait for the WebSocket to deliver complete or error
      const result = await completionPromise

      // Merge session ID from the HTTP response if the WS event didn't provide one
      const finalResult: SubmitGoalResult = result
        ? { ...result, sessionId: result.sessionId || submitResult.sessionId }
        : { success: false, turns: 0, sessionId: submitResult.sessionId }
      setLastResult(finalResult)
      return finalResult
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      log.error('error', 'IPC invoke failed', { command: 'submit_goal', error: message })
      setError(message)
      setIsRunning(false)
      completion.resolve = null
      return null
    } finally {
      detachListener()
    }
  }

  /**
   * Run the agent via the edit-and-resend backend endpoint.
   * Same streaming infrastructure as run(), but calls edit_and_resend instead
   * of submit_goal so the backend properly truncates history at the edited
   * message before starting the new agent run.
   */
  const editAndResendRun = async (
    messageId: string,
    newContent: string
  ): Promise<SubmitGoalResult | null> => {
    resetState()
    setIsRunning(true)
    resetMetrics()
    log.info('streaming', 'Edit-and-resend stream started', { messageId })
    try {
      await attachListener()
      const editArgs = {
        args: {
          messageId,
          newContent,
        },
      }

      if (isTauri()) {
        // Same as run(): don't set isRunning(false) here — let the caller
        // finalize the message first, then call endRun() in a batch.
        const result = await invoke<SubmitGoalResult>('edit_and_resend', editArgs)
        setLastResult(result)
        return result
      }

      // Web mode: HTTP call returns immediately, wait for WebSocket completion
      const completionPromise = new Promise<SubmitGoalResult | null>((resolve) => {
        completion.resolve = resolve
      })

      const submitResult = await invoke<SubmitGoalResult>('edit_and_resend', editArgs)

      const result = await completionPromise

      const finalResult: SubmitGoalResult = result
        ? { ...result, sessionId: result.sessionId || submitResult.sessionId }
        : { success: false, turns: 0, sessionId: submitResult.sessionId }
      setLastResult(finalResult)
      return finalResult
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      log.error('error', 'IPC invoke failed', { command: 'edit_and_resend', error: message })
      setError(message)
      setIsRunning(false)
      completion.resolve = null
      return null
    } finally {
      detachListener()
    }
  }

  const cancel = async (): Promise<void> => {
    log.info('agent', 'Agent cancel requested')
    try {
      await invoke('cancel_agent')
    } catch {
      /* ignore */
    }
    batch(() => {
      // Mark any running tool calls as interrupted
      setActiveToolCalls((prev) => {
        const updated = [...prev]
        for (const tc of updated) {
          if (tc.status === 'running') {
            tc.status = 'error'
            tc.output = '[interrupted]'
            tc.completedAt = Date.now()
          }
        }
        return updated
      })
      // Set the error so that useAgent's cancel detection can identify this as a
      // user-initiated cancellation and preserve partial streaming content.
      setError('Agent run cancelled by user')
      setIsRunning(false)
    })
    // Resolve the web-mode completion promise so run() can return
    if (completion.resolve) {
      completion.resolve(null)
      completion.resolve = null
    }
    detachListener()
  }

  /**
   * Explicitly mark the run as finished. Used by useAgent.ts in Tauri mode
   * to control the exact moment isRunning goes false — after the message
   * content has been finalized, preventing a flash of empty content.
   */
  const endRun = (): void => {
    setIsRunning(false)
  }

  // ── Mid-stream messaging (3-tier) ─────────────────────────────────

  /** Inject an interrupt message. Agent stops at next tool boundary and processes it. */
  const steer = async (message: string): Promise<void> => {
    if (!isRunning()) {
      log.warn('agent', 'Steer rejected: agent not running')
      return
    }
    log.info('agent', 'Steering message injected', { length: message.length })
    try {
      await invoke('steer_agent', { message })
      log.info('agent', 'Steering message delivered successfully')
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Steering message failed', { error: msg })
      setError(msg)
    }
  }

  /** Queue a message for next turn. Agent finishes current turn, then processes this. */
  const followUp = async (message: string): Promise<void> => {
    if (!isRunning()) {
      log.warn('agent', 'Cannot queue follow-up: agent is not running')
      return
    }
    try {
      await invoke('follow_up_agent', { message })
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Failed to queue follow-up', { error: msg })
      setError(msg)
      throw err
    }
  }

  /** Queue a post-complete message (Tier 3). Runs in grouped pipeline after agent stops. */
  const postComplete = async (message: string, group?: number): Promise<void> => {
    if (!isRunning()) {
      log.warn('agent', 'Cannot queue post-complete: agent is not running')
      return
    }
    try {
      await invoke('post_complete_agent', { args: { message, group: group ?? 1 } })
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Failed to queue post-complete', { error: msg })
      setError(msg)
      throw err
    }
  }

  return {
    run,
    editAndResendRun,
    cancel,
    steer,
    followUp,
    postComplete,
    endRun,
    resetState,
    attachListener,
    detachListener,
    destroyListener,
  }
}
