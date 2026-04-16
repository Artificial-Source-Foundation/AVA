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
  setCurrentRunId: Setter<string | null>
  setActiveToolCalls: Setter<ToolCall[]>
  handleAgentEvent: (event: AgentEvent) => void
  resetState: () => void
}

const TAURI_TERMINAL_GRACE_MS = 75

interface TauriTerminalTracker {
  runId: string
  terminalSeen: boolean
  terminalPromise: Promise<void>
  resolveTerminal: (() => void) | null
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
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
      autoCompact?: boolean
      compactionThreshold?: number
      compactionProvider?: string
      compactionModel?: string
    }
  ) => Promise<SubmitGoalResult | null>
  editAndResendRun: (
    messageId: string,
    newContent: string,
    sessionId?: string
  ) => Promise<SubmitGoalResult | null>
  retryRun: (sessionId?: string) => Promise<SubmitGoalResult | null>
  regenerateRun: (sessionId?: string) => Promise<SubmitGoalResult | null>
  cancel: () => Promise<void>
  steer: (message: string) => Promise<void>
  followUp: (message: string, sessionId?: string) => Promise<void>
  postComplete: (message: string, group?: number, sessionId?: string) => Promise<void>
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
    setCurrentRunId,
    setActiveToolCalls,
    handleAgentEvent,
    resetState,
  } = deps

  let unlisten: UnlistenFn | null = null
  let eventSocket: WebSocket | null = null
  let socketGeneration = 0
  let tauriRunSequence = 0
  let activeTauriRun: TauriTerminalTracker | null = null
  let activeRunId: string | null = null

  const isCurrentSocket = (ws: WebSocket, generation: number): boolean =>
    eventSocket === ws && socketGeneration === generation

  const nextTauriRunId = (): string => {
    tauriRunSequence += 1
    const uniqueSuffix =
      typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function'
        ? crypto.randomUUID()
        : `${Date.now()}-${tauriRunSequence}`
    return `desktop-run-${uniqueSuffix}`
  }

  const getTauriEventRunId = (event: AgentEvent): string | null => {
    const terminalEvent = event as { run_id?: string; runId?: string }
    return terminalEvent.run_id ?? terminalEvent.runId ?? null
  }

  const resetTauriTerminalTracking = (runId: string): TauriTerminalTracker => {
    let resolveTerminalPromise: (() => void) | null = null
    const tracker: TauriTerminalTracker = {
      runId,
      terminalSeen: false,
      terminalPromise: new Promise<void>((resolve) => {
        resolveTerminalPromise = resolve
      }),
      resolveTerminal: null,
    }
    tracker.resolveTerminal = () => {
      tracker.terminalSeen = true
      resolveTerminalPromise?.()
      resolveTerminalPromise = null
      tracker.resolveTerminal = null
    }
    activeRunId = runId
    setCurrentRunId(runId)
    activeTauriRun = tracker
    return tracker
  }

  const clearTauriTerminalTracking = (tracker?: TauriTerminalTracker | null): void => {
    if (tracker && activeTauriRun !== tracker) {
      return
    }
    if (tracker) {
      tracker.resolveTerminal = null
    }
    activeTauriRun = null
    activeRunId = null
    setCurrentRunId(null)
  }

  const shouldHandleCorrelatedEvent = (event: AgentEvent): boolean => {
    const activeRun = activeTauriRun
    const eventRunId = getTauriEventRunId(event)
    const isTerminalEvent = event.type === 'complete' || event.type === 'error'
    const requiresRunCorrelation =
      event.type === 'plan_step_complete' ||
      event.type === 'subagent_complete' ||
      event.type === 'streaming_edit_progress'

    if (!eventRunId) {
      if (requiresRunCorrelation) {
        log.warn('agent', 'Ignoring malformed correlated event missing run_id', {
          eventType: event.type,
        })
        return false
      }

      if (!isTerminalEvent) {
        return true
      }

      if (!isTauri() && !activeRunId) {
        return true
      }

      log.warn('agent', 'Ignoring uncorrelated terminal event', {
        eventType: event.type,
        activeRunId: activeRunId ?? activeRun?.runId ?? null,
      })
      return false
    }

    if (!activeRunId || eventRunId !== activeRunId) {
      log.warn('agent', 'Ignoring stale correlated run event', {
        eventType: event.type,
        eventRunId,
        activeRunId,
      })
      return false
    }

    if (isTerminalEvent) {
      activeRun?.resolveTerminal?.()
    }

    return true
  }

  const withTauriRunId = (
    args: Record<string, unknown> | undefined,
    runId: string | null
  ): Record<string, unknown> | undefined => {
    if (!runId) {
      return args
    }

    if (!args) {
      return { args: { runId } }
    }

    const nestedArgs = args.args
    if (nestedArgs && typeof nestedArgs === 'object' && !Array.isArray(nestedArgs)) {
      return {
        ...args,
        args: {
          ...(nestedArgs as Record<string, unknown>),
          runId,
        },
      }
    }

    return {
      ...args,
      runId,
    }
  }

  const attachListener = async (): Promise<void> => {
    if (isTauri()) {
      // Tauri: always recreate the listener (cheap, event-based)
      if (unlisten) {
        unlisten()
        unlisten = null
      }
      unlisten = await listen<AgentEvent>('agent-event', (evt) => {
        if (!shouldHandleCorrelatedEvent(evt.payload)) {
          return
        }
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
          if (!shouldHandleCorrelatedEvent(event)) {
            return
          }
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
    clearTauriTerminalTracking()
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

  const createCompletionPromise = (): {
    promise: Promise<SubmitGoalResult | null>
    resolve: (result: SubmitGoalResult | null) => void
  } => {
    let resolvePromise!: (result: SubmitGoalResult | null) => void
    const promise = new Promise<SubmitGoalResult | null>((resolve) => {
      resolvePromise = resolve
      completion.resolve = resolve
    })
    return { promise, resolve: resolvePromise }
  }

  const normalizeTauriSubmitResult = (result: SubmitGoalResult | null): SubmitGoalResult | null => {
    if (!result) {
      return null
    }
    return {
      success: result.success,
      turns: 0,
      sessionId: result.sessionId,
    }
  }

  const invokeStreamingCommand = async (
    command: string,
    args?: Record<string, unknown>
  ): Promise<SubmitGoalResult | null> => {
    const completionBinding = createCompletionPromise()
    const completionPromise = completionBinding.promise
    const runId = nextTauriRunId()
    activeRunId = runId
    setCurrentRunId(runId)
    const terminalTracker = isTauri() ? resetTauriTerminalTracking(runId) : null
    const invokePromise = invoke<SubmitGoalResult>(command, withTauriRunId(args, runId))

    if (isTauri()) {
      const guardedInvokePromise = invokePromise.catch((err) => {
        if (completion.resolve !== completionBinding.resolve) {
          log.warn('agent', 'Backend invoke rejected after terminal event', {
            command,
            error: err instanceof Error ? err.message : String(err),
          })
          return null
        }
        throw err
      })

      const winner = await Promise.race([
        completionPromise.then((result) => ({ source: 'terminal' as const, result })),
        guardedInvokePromise.then((result) => ({ source: 'invoke' as const, result })),
      ])

      let finalResult = winner.result
      if (winner.source === 'invoke' && terminalTracker && !terminalTracker.terminalSeen) {
        await Promise.race([terminalTracker.terminalPromise, delay(TAURI_TERMINAL_GRACE_MS)])
        if (terminalTracker.terminalSeen) {
          finalResult = await completionPromise
        }
      }

      const normalizedResult = normalizeTauriSubmitResult(finalResult)
      if (completion.resolve === completionBinding.resolve) {
        completion.resolve = null
      }
      clearTauriTerminalTracking(terminalTracker)
      if (normalizedResult) {
        setLastResult(normalizedResult)
      }
      return normalizedResult
    }

    const submitResult = await invokePromise
    if (!submitResult.success) {
      activeRunId = null
      setCurrentRunId(null)
      if (completion.resolve === completionBinding.resolve) {
        completion.resolve = null
      }
      setIsRunning(false)
      setLastResult(submitResult)
      return submitResult
    }

    const result = await completionPromise
    const finalResult: SubmitGoalResult = result
      ? { ...result, sessionId: result.sessionId || submitResult.sessionId }
      : { success: false, turns: 0, sessionId: submitResult.sessionId }
    activeRunId = null
    setCurrentRunId(null)
    setLastResult(finalResult)
    return finalResult
  }

  const run = async (
    goal: string,
    opts?: {
      provider?: string
      model?: string
      maxTurns?: number
      thinkingLevel?: string
      sessionId?: string
      autoCompact?: boolean
      compactionThreshold?: number
      compactionProvider?: string
      compactionModel?: string
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
          autoCompact: opts?.autoCompact ?? null,
          compactionThreshold: opts?.compactionThreshold ?? null,
          compactionProvider: opts?.compactionProvider ?? null,
          compactionModel: opts?.compactionModel ?? null,
        },
      }

      return await invokeStreamingCommand('submit_goal', submitArgs)
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
    newContent: string,
    sessionId?: string
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
          sessionId: sessionId ?? null,
        },
      }

      return await invokeStreamingCommand('edit_and_resend', editArgs)
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

  /**
   * Retry the last failed message via the backend retry endpoint.
   * Uses the same streaming infrastructure as run() so events are properly
   * captured and the caller can settle the response into a placeholder message.
   */
  const retryRun = async (sessionId?: string): Promise<SubmitGoalResult | null> => {
    resetState()
    setIsRunning(true)
    resetMetrics()
    log.info('streaming', 'Retry stream started')
    try {
      await attachListener()

      return await invokeStreamingCommand(
        'retry_last_message',
        sessionId ? { args: { sessionId } } : undefined
      )
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      log.error('error', 'IPC invoke failed', { command: 'retry_last_message', error: message })
      setError(message)
      setIsRunning(false)
      completion.resolve = null
      return null
    } finally {
      detachListener()
    }
  }

  /**
   * Regenerate the last assistant response via the backend regenerate endpoint.
   * Same streaming infrastructure as retryRun().
   */
  const regenerateRun = async (sessionId?: string): Promise<SubmitGoalResult | null> => {
    resetState()
    setIsRunning(true)
    resetMetrics()
    log.info('streaming', 'Regenerate stream started')
    try {
      await attachListener()

      return await invokeStreamingCommand(
        'regenerate_response',
        sessionId ? { args: { sessionId } } : undefined
      )
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      log.error('error', 'IPC invoke failed', { command: 'regenerate_response', error: message })
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
    // Resolve any in-flight terminal completion promise so run() can return
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
  const followUp = async (message: string, sessionId?: string): Promise<void> => {
    if (!isRunning()) {
      log.warn('agent', 'Cannot queue follow-up: agent is not running')
      throw new Error('Agent is not running')
    }
    try {
      await invoke('follow_up_agent', { args: { message, sessionId: sessionId ?? null } })
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      log.error('agent', 'Failed to queue follow-up', { error: msg })
      setError(msg)
      throw err
    }
  }

  /** Queue a post-complete message (Tier 3). Runs in grouped pipeline after agent stops. */
  const postComplete = async (
    message: string,
    group?: number,
    sessionId?: string
  ): Promise<void> => {
    if (!isRunning()) {
      log.warn('agent', 'Cannot queue post-complete: agent is not running')
      throw new Error('Agent is not running')
    }
    try {
      await invoke('post_complete_agent', {
        args: { message, group: group ?? 1, sessionId: sessionId ?? null },
      })
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
    retryRun,
    regenerateRun,
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
