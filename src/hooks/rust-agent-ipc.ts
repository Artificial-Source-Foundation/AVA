/**
 * rust-agent-ipc.ts — Frontend Runtime/Session Correlation Owner
 *
 * This module is the canonical owner of frontend runtime and session correlation:
 * - activeRunId / attachedSessionId binding
 * - Tauri listener lifecycle and WebSocket generation tracking
 * - Run-scoped event correlation (shouldHandleCorrelatedEvent)
 * - Session binding capture/restore for rehydration
 *
 * Settlement logic (message finalization, backend sync) is coordinated through
 * the agent-settlement service, which consumes this correlation layer but does
 * not duplicate its ownership responsibilities.
 *
 * @see src/services/agent-settlement.ts for settlement coordination
 */

import { isTauri, invoke as tauriInvoke } from '@tauri-apps/api/core'
import { listen, type UnlistenFn } from '@tauri-apps/api/event'
import type { Accessor, Setter } from 'solid-js'
import { batch } from 'solid-js'
import { apiInvoke, createEventSocket } from '../lib/api-client'
import { log } from '../lib/logger'
import { resolveBackendSessionId } from '../services/web-session-identity'
import type { ToolCall } from '../types'
import type {
  AgentEvent,
  AgentStatus,
  SubmitGoalResult,
  ToolIntrospectionImageContext,
} from '../types/rust-ipc'
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
  setTrackedSessionId: Setter<string | null>
  setDetachedSessionId?: Setter<string | null>
  setActiveToolCalls: Setter<ToolCall[]>
  handleAgentEvent: (event: AgentEvent) => void
  resetState: () => void
}

interface TauriTerminalTracker {
  runId: string
}

/**
 * Agent IPC interface — Runtime correlation and transport abstraction.
 *
 * Consumers that need settlement coordination (message finalization, backend
 * sync) should use this IPC layer together with the agent-settlement service:
 *   import { createAgentIpc } from './rust-agent-ipc'
 *   import { createRunGuard, settleSuccessAssistantMessage } from '../services/agent-settlement'
 *
 * The IPC layer owns run/session correlation; the settlement service owns
 * message lifecycle and backend synchronization patterns.
 */
export interface AgentIpc {
  run: (
    goal: string,
    opts?: {
      provider?: string
      model?: string
      maxTurns?: number
      thinkingLevel?: string
      sessionId?: string
      images?: ToolIntrospectionImageContext[]
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
  rehydrateStatus: (sessionId?: string | null) => Promise<RehydrateResult>
  captureSessionBinding: () => { activeRunId: string | null; attachedSessionId: string | null }
  restoreSessionBinding: (binding: {
    activeRunId: string | null
    attachedSessionId: string | null
  }) => void
}

export interface RehydrateResult {
  sessionId: string | null
  running: boolean
  runId: string | null
  pendingApproval: AgentStatus['pendingApproval'] | null
  pendingQuestion: AgentStatus['pendingQuestion'] | null
  pendingPlan: AgentStatus['pendingPlan'] | null
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
    setTrackedSessionId,
    setDetachedSessionId,
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
  let attachedSessionId: string | null = null
  let rehydrateGeneration = 0

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
    const tracker: TauriTerminalTracker = {
      runId,
    }
    activeRunId = runId
    setCurrentRunId(runId)
    activeTauriRun = tracker
    return tracker
  }

  const clearTauriTerminalTracking = (tracker?: TauriTerminalTracker | null): void => {
    if (tracker) {
      const activeTrackerRunId = activeTauriRun?.runId ?? null
      if (activeTauriRun !== tracker && activeTrackerRunId !== tracker.runId) {
        return
      }
    }
    if (tracker) {
      // No-op: tracker is currently run-id only.
    }
    activeTauriRun = null
    activeRunId = null
    setCurrentRunId(null)
  }

  const shouldHandleCorrelatedEvent = (event: AgentEvent): boolean => {
    const eventRunId = getTauriEventRunId(event)
    const isTerminalEvent = event.type === 'complete' || event.type === 'error'
    const requiresRunCorrelation =
      event.type === 'plan_step_complete' ||
      event.type === 'subagent_complete' ||
      event.type === 'streaming_edit_progress'

    if (!attachedSessionId && !activeRunId && !isRunning()) {
      return false
    }

    if (!eventRunId) {
      if (requiresRunCorrelation) {
        log.warn('agent', 'Ignoring malformed correlated event missing run_id', {
          eventType: event.type,
        })
        return false
      }

      if (
        !isTauri() &&
        event.type === 'complete' &&
        activeRunId &&
        attachedSessionId &&
        event.session.id === attachedSessionId
      ) {
        return true
      }

      if (!isTerminalEvent) {
        log.warn('agent', 'Ignoring uncorrelated non-terminal event', {
          eventType: event.type,
          activeRunId,
        })
        return false
      }

      if (!activeRunId) {
        log.warn('agent', 'Ignoring uncorrelated terminal event without active run', {
          eventType: event.type,
          attachedSessionId,
        })
        return false
      }

      log.warn('agent', 'Ignoring uncorrelated terminal event', {
        eventType: event.type,
        activeRunId,
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

  const resolveBrowserSessionId = (sessionId?: string | null): string | null => {
    if (!sessionId || isTauri()) {
      return sessionId ?? null
    }

    return resolveBackendSessionId(sessionId)
  }

  const withWebRunCorrelation = (
    payload?: Record<string, unknown>,
    sessionId?: string | null
  ): Record<string, unknown> | undefined => {
    const runId = activeRunId
    const correlatedSessionId = resolveBrowserSessionId(sessionId)
    if (!runId && !correlatedSessionId) {
      return payload
    }

    const correlation: Record<string, unknown> = {}
    if (runId) {
      correlation.runId = runId
    }
    if (correlatedSessionId) {
      correlation.sessionId = correlatedSessionId
    }

    const base = payload ?? {}
    if (isTauri()) {
      const nestedArgs = base.args
      if (nestedArgs && typeof nestedArgs === 'object' && !Array.isArray(nestedArgs)) {
        return {
          ...base,
          args: {
            ...(nestedArgs as Record<string, unknown>),
            ...correlation,
          },
        }
      }

      return {
        args: {
          ...base,
          ...correlation,
        },
      }
    }

    const nestedArgs = base.args
    if (nestedArgs && typeof nestedArgs === 'object' && !Array.isArray(nestedArgs)) {
      return {
        ...base,
        args: {
          ...(nestedArgs as Record<string, unknown>),
          ...correlation,
        },
      }
    }

    return {
      ...base,
      ...correlation,
    }
  }

  const resolveBrowserReplaySessionId = (sessionId?: string): string | undefined => {
    return resolveBrowserSessionId(sessionId) ?? undefined
  }

  const statusCorrelation = (
    sessionId?: string | null,
    runId?: string | null
  ): Record<string, unknown> | undefined => {
    const correlation: Record<string, unknown> = {}
    const correlatedSessionId = resolveBrowserSessionId(sessionId)
    if (correlatedSessionId) {
      correlation.sessionId = correlatedSessionId
    }
    if (runId) {
      correlation.runId = runId
    }

    if (Object.keys(correlation).length === 0) {
      return undefined
    }

    return isTauri() ? { args: correlation } : correlation
  }

  const replayPendingInteractiveState = (status: AgentStatus): void => {
    if (status.pendingApproval) {
      handleAgentEvent(status.pendingApproval)
    }
    if (status.pendingQuestion) {
      handleAgentEvent(status.pendingQuestion)
    }
    if (status.pendingPlan) {
      handleAgentEvent(status.pendingPlan)
    }
  }

  const emptyRehydrateResult = (sessionId: string | null): RehydrateResult => ({
    sessionId,
    running: false,
    runId: null,
    pendingApproval: null,
    pendingQuestion: null,
    pendingPlan: null,
  })

  const rehydrateResultFromStatus = (
    sessionId: string | null,
    status: AgentStatus,
    runId: string | null
  ): RehydrateResult => ({
    sessionId,
    running: Boolean(status.running && runId),
    runId,
    pendingApproval: status.pendingApproval ?? null,
    pendingQuestion: status.pendingQuestion ?? null,
    pendingPlan: status.pendingPlan ?? null,
  })

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
    attachedSessionId = null
    if (eventSocket) {
      socketGeneration += 1
      eventSocket.close()
      eventSocket = null
    }
  }

  const captureSessionBinding = (): {
    activeRunId: string | null
    attachedSessionId: string | null
  } => ({
    activeRunId,
    attachedSessionId,
  })

  const clearRunningCorrelationState = (): void => {
    activeRunId = null
    attachedSessionId = null
    batch(() => {
      setIsRunning(false)
      setCurrentRunId(null)
      setTrackedSessionId(null)
    })
  }

  const restoreSessionBinding = (binding: {
    activeRunId: string | null
    attachedSessionId: string | null
  }): void => {
    activeTauriRun = null
    activeRunId = binding.activeRunId ?? null
    attachedSessionId = binding.attachedSessionId ?? null
  }

  /** Reset streaming metrics for a new run. */
  const resetMetrics = (): void => {
    metrics.chunkCount = 0
    metrics.totalTextLen = 0
    metrics.runStartTime = Date.now()
    metrics.firstTokenLogged = false
    metrics.pendingToolNames = []
  }

  const settleDetachedCompletion = (sessionId?: string | null): void => {
    setDetachedSessionId?.(sessionId ?? null)
    if (!completion.resolve) {
      return
    }
    completion.resolve({
      success: false,
      turns: 0,
      sessionId: sessionId ?? '',
      detachedSessionId: sessionId ?? null,
    })
    completion.resolve = null
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

  const invalidateInFlightRehydrates = (): void => {
    rehydrateGeneration += 1
  }

  const normalizeTauriSubmitResult = (result: SubmitGoalResult | null): SubmitGoalResult | null => {
    if (!result) {
      return null
    }
    return {
      success: result.success,
      turns: 0,
      sessionId: result.sessionId,
      ...(result.detachedSessionId != null ? { detachedSessionId: result.detachedSessionId } : {}),
    }
  }

  const invokeStreamingCommand = async (
    command: string,
    args?: Record<string, unknown>
  ): Promise<SubmitGoalResult | null> => {
    const completionBinding = createCompletionPromise()
    const completionPromise = completionBinding.promise
    const runId = nextTauriRunId()
    setDetachedSessionId?.(null)
    activeRunId = runId
    setCurrentRunId(runId)
    const terminalTracker = isTauri() ? resetTauriTerminalTracking(runId) : null
    const invokePromise = invoke<SubmitGoalResult>(command, withTauriRunId(args, runId))
    const runStillBound = (): boolean =>
      activeRunId === runId || completion.resolve === completionBinding.resolve

    if (isTauri()) {
      const acceptedPromise = invokePromise
        .then((result) => ({ kind: 'accepted' as const, result }))
        .catch((err) => {
          if (completion.resolve !== completionBinding.resolve) {
            log.warn('agent', 'Backend invoke rejected after terminal event', {
              command,
              error: err instanceof Error ? err.message : String(err),
            })
            return { kind: 'accepted' as const, result: null }
          }
          return { kind: 'invoke-error' as const, error: err }
        })

      const terminalPromise = completionPromise.then((result) => ({
        kind: 'terminal' as const,
        result,
      }))

      const firstSettled = await Promise.race([terminalPromise, acceptedPromise])

      if (firstSettled.kind === 'invoke-error') {
        throw firstSettled.error
      }

      const acceptedResult = firstSettled.kind === 'accepted' ? firstSettled.result : null
      const finalResult =
        firstSettled.kind === 'terminal' ? firstSettled.result : await completionPromise

      const normalizedResult = normalizeTauriSubmitResult(finalResult)
      const normalizedAcceptedResult = normalizeTauriSubmitResult(acceptedResult)
      const mergedResult = normalizedResult
        ? {
            ...normalizedResult,
            sessionId: normalizedResult.sessionId || normalizedAcceptedResult?.sessionId || '',
          }
        : null
      const shouldCommitResult = runStillBound()
      if (completion.resolve === completionBinding.resolve) {
        completion.resolve = null
      }
      clearTauriTerminalTracking(terminalTracker)
      if (shouldCommitResult) {
        setTrackedSessionId(null)
      }
      if (mergedResult && shouldCommitResult) {
        setLastResult(mergedResult)
      }
      return mergedResult
    }

    const submitResult = await invokePromise.catch((err) => {
      if (!runStillBound()) {
        return null
      }
      throw err
    })
    if (!submitResult) {
      return null
    }
    if (!submitResult.success) {
      const shouldCommitResult = runStillBound()
      activeRunId = null
      setCurrentRunId(null)
      if (completion.resolve === completionBinding.resolve) {
        completion.resolve = null
      }
      if (shouldCommitResult) {
        setTrackedSessionId(null)
        setIsRunning(false)
        setLastResult(submitResult)
      }
      return submitResult
    }

    const result = await completionPromise
    const finalResult: SubmitGoalResult = result
      ? { ...result, sessionId: result.sessionId || submitResult.sessionId }
      : { success: false, turns: 0, sessionId: submitResult.sessionId }
    const shouldCommitResult = runStillBound()
    activeRunId = null
    setCurrentRunId(null)
    if (shouldCommitResult) {
      setTrackedSessionId(null)
      setLastResult(finalResult)
    }
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
      images?: ToolIntrospectionImageContext[]
      autoCompact?: boolean
      compactionThreshold?: number
      compactionProvider?: string
      compactionModel?: string
    }
  ): Promise<SubmitGoalResult | null> => {
    resetState()
    invalidateInFlightRehydrates()
    attachedSessionId = opts?.sessionId ?? null
    setTrackedSessionId(attachedSessionId)
    const requestSessionId = resolveBrowserSessionId(opts?.sessionId ?? null)
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
          sessionId: requestSessionId,
          images: opts?.images ?? [],
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
      setTrackedSessionId(null)
      completion.resolve = null
      return null
    } finally {
      detachListener()
    }
  }

  const rehydrateStatus = async (sessionId?: string | null): Promise<RehydrateResult> => {
    const requestGeneration = ++rehydrateGeneration
    const targetSessionId = sessionId ?? null
    const requestSessionId = resolveBrowserSessionId(targetSessionId)
    let optimisticStateApplied = false
    const previousRunId = activeRunId

    if (!targetSessionId) {
      settleDetachedCompletion(attachedSessionId)
      detachListener()
      clearRunningCorrelationState()
      return emptyRehydrateResult(null)
    }

    if (attachedSessionId && targetSessionId && attachedSessionId !== targetSessionId) {
      settleDetachedCompletion(attachedSessionId)
      detachListener()
      clearRunningCorrelationState()
    }

    try {
      const status = await invoke<AgentStatus>(
        'get_agent_status',
        statusCorrelation(requestSessionId, null)
      )

      if (requestGeneration !== rehydrateGeneration) {
        return emptyRehydrateResult(targetSessionId)
      }

      if (!status.running) {
        detachListener()
        clearRunningCorrelationState()
        resetState()
        return emptyRehydrateResult(targetSessionId)
      }

      const runId =
        typeof status.runId === 'string' && status.runId.trim().length > 0 ? status.runId : null

      if (!runId) {
        detachListener()
        clearRunningCorrelationState()
        resetState()
        log.info('agent', 'Rehydrate ignored invalid backend run status', {
          runId: status.runId,
        })
        return emptyRehydrateResult(targetSessionId)
      }

      if (requestGeneration !== rehydrateGeneration) {
        return emptyRehydrateResult(targetSessionId)
      }

      if (previousRunId !== runId) {
        resetState()
      }

      activeRunId = runId
      attachedSessionId = targetSessionId
      batch(() => {
        setIsRunning(true)
        setCurrentRunId(runId)
        setTrackedSessionId(targetSessionId)
      })
      optimisticStateApplied = true

      await attachListener()

      if (requestGeneration !== rehydrateGeneration) {
        return emptyRehydrateResult(targetSessionId)
      }

      const reconciledStatus = await invoke<AgentStatus>(
        'get_agent_status',
        statusCorrelation(requestSessionId, runId)
      )

      if (requestGeneration !== rehydrateGeneration) {
        return emptyRehydrateResult(targetSessionId)
      }

      const reconciledRunId =
        typeof reconciledStatus.runId === 'string' && reconciledStatus.runId.trim().length > 0
          ? reconciledStatus.runId
          : null

      if (!reconciledStatus.running || !reconciledRunId) {
        detachListener()
        clearRunningCorrelationState()
        resetState()
        log.info('agent', 'Cleared stale rehydrated run after listener attach')
        return emptyRehydrateResult(targetSessionId)
      }
      if (runId !== reconciledRunId) {
        resetState()
      }
      activeRunId = reconciledRunId
      attachedSessionId = targetSessionId
      batch(() => {
        setCurrentRunId(reconciledRunId)
        setTrackedSessionId(targetSessionId)
      })
      replayPendingInteractiveState(reconciledStatus)
      log.info('agent', 'Rehydrated active backend run state', {
        runId: reconciledRunId,
      })
      return rehydrateResultFromStatus(targetSessionId, reconciledStatus, reconciledRunId)
    } catch (err) {
      if (optimisticStateApplied && requestGeneration === rehydrateGeneration) {
        detachListener()
        clearRunningCorrelationState()
        resetState()
      }

      log.warn('agent', 'Failed to rehydrate agent status', {
        error: err instanceof Error ? err.message : String(err),
      })
      return emptyRehydrateResult(targetSessionId)
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
    const requestSessionId = resolveBrowserReplaySessionId(sessionId)
    resetState()
    invalidateInFlightRehydrates()
    attachedSessionId = sessionId ?? null
    setTrackedSessionId(attachedSessionId)
    setIsRunning(true)
    resetMetrics()
    log.info('streaming', 'Edit-and-resend stream started', { messageId })
    try {
      await attachListener()
      const editArgs = {
        args: {
          messageId,
          newContent,
          sessionId: requestSessionId ?? null,
        },
      }

      return await invokeStreamingCommand('edit_and_resend', editArgs)
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      log.error('error', 'IPC invoke failed', { command: 'edit_and_resend', error: message })
      setError(message)
      setIsRunning(false)
      setTrackedSessionId(null)
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
    const requestSessionId = resolveBrowserReplaySessionId(sessionId)
    resetState()
    invalidateInFlightRehydrates()
    attachedSessionId = sessionId ?? null
    setTrackedSessionId(attachedSessionId)
    setIsRunning(true)
    resetMetrics()
    log.info('streaming', 'Retry stream started')
    try {
      await attachListener()

      return await invokeStreamingCommand(
        'retry_last_message',
        requestSessionId ? { args: { sessionId: requestSessionId } } : undefined
      )
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      log.error('error', 'IPC invoke failed', { command: 'retry_last_message', error: message })
      setError(message)
      setIsRunning(false)
      setTrackedSessionId(null)
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
    const requestSessionId = resolveBrowserReplaySessionId(sessionId)
    resetState()
    invalidateInFlightRehydrates()
    attachedSessionId = sessionId ?? null
    setTrackedSessionId(attachedSessionId)
    setIsRunning(true)
    resetMetrics()
    log.info('streaming', 'Regenerate stream started')
    try {
      await attachListener()

      return await invokeStreamingCommand(
        'regenerate_response',
        requestSessionId ? { args: { sessionId: requestSessionId } } : undefined
      )
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      log.error('error', 'IPC invoke failed', { command: 'regenerate_response', error: message })
      setError(message)
      setIsRunning(false)
      setTrackedSessionId(null)
      completion.resolve = null
      return null
    } finally {
      detachListener()
    }
  }

  const cancel = async (): Promise<void> => {
    log.info('agent', 'Agent cancel requested')
    const cancelArgs = withWebRunCorrelation(undefined)
    clearRunningCorrelationState()
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
    })
    // Resolve any in-flight terminal completion promise so run() can return.
    if (completion.resolve) {
      completion.resolve(null)
      completion.resolve = null
    }
    detachListener()
    try {
      await invoke('cancel_agent', cancelArgs)
    } catch {
      /* ignore */
    }
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
      await invoke('steer_agent', withWebRunCorrelation({ message }))
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
      await invoke(
        'follow_up_agent',
        withWebRunCorrelation(
          { args: { message, sessionId: sessionId ?? null } },
          sessionId ?? null
        )
      )
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
      await invoke(
        'post_complete_agent',
        withWebRunCorrelation(
          {
            args: { message, group: group ?? 1, sessionId: sessionId ?? null },
          },
          sessionId ?? null
        )
      )
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
    rehydrateStatus,
    captureSessionBinding,
    restoreSessionBinding,
  }
}
