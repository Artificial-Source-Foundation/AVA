import { isTauri, invoke as tauriInvoke } from '@tauri-apps/api/core'
import { listen, type UnlistenFn } from '@tauri-apps/api/event'
import { batch, createSignal, onCleanup } from 'solid-js'
import { apiInvoke, createEventSocket } from '../lib/api-client'
import { debugLog } from '../lib/debug-log'
import { log } from '../lib/logger'
import { useLayout } from '../stores/layout'
import type { ToolCall } from '../types'
import type {
  AgentEvent,
  PlanCreatedEvent,
  PlanData,
  SubmitGoalResult,
  TodoItem,
  TodoUpdateEvent,
} from '../types/rust-ipc'

/** Invoke the backend — Tauri IPC or HTTP API depending on runtime. */
function invoke<T>(cmd: string, args?: Record<string, unknown>): Promise<T> {
  if (isTauri()) {
    return args ? tauriInvoke<T>(cmd, args) : tauriInvoke<T>(cmd)
  }
  return apiInvoke<T>(cmd, args)
}

/**
 * A thinking segment: thinking content that occurred before a group of tool calls.
 * Used to reconstruct the interleaved thinking→tools→thinking→response sequence.
 */
export interface ThinkingSegment {
  /** Accumulated thinking text for this segment */
  thinking: string
  /** IDs of tool calls that followed this thinking block (may be empty for final thinking) */
  toolCallIds: string[]
}

export function useRustAgent() {
  const [isRunning, setIsRunning] = createSignal(false)
  const [streamingContent, setStreamingContent] = createSignal('')
  const [thinkingContent, setThinkingContent] = createSignal('')
  const [activeToolCalls, setActiveToolCalls] = createSignal<ToolCall[]>([])
  const [error, setError] = createSignal<string | null>(null)
  const [lastResult, setLastResult] = createSignal<SubmitGoalResult | null>(null)
  const [tokenUsage, setTokenUsage] = createSignal({ input: 0, output: 0, cost: 0 })
  const [events, setEvents] = createSignal<AgentEvent[]>([])
  const [progressMessage, setProgressMessage] = createSignal<string | null>(null)
  const [budgetWarning, setBudgetWarning] = createSignal<{
    thresholdPercent: number
    currentCostUsd: number
    maxBudgetUsd: number
  } | null>(null)
  const [pendingPlan, setPendingPlan] = createSignal<PlanData | null>(null)
  // Interleaved thinking segments: each entry is a block of thinking + the tool calls that followed
  const [thinkingSegments, setThinkingSegments] = createSignal<ThinkingSegment[]>([])
  // Current todo list — updated whenever the agent calls todo_write
  const [todos, setTodos] = createSignal<TodoItem[]>([])

  let unlisten: UnlistenFn | null = null
  let eventSocket: WebSocket | null = null

  // Streaming metrics — reset at the start of each run(), read in handleAgentEvent
  let chunkCount = 0
  let totalTextLen = 0
  let runStartTime = 0
  let firstTokenLogged = false

  // In web mode, the HTTP submit returns immediately while the agent runs async.
  // We use this resolver to make run() wait for the complete/error WebSocket event.
  let completionResolve: ((result: SubmitGoalResult | null) => void) | null = null

  /** Shared event handler — used by both Tauri listen() and WebSocket. */
  const handleAgentEvent = (event: AgentEvent): void => {
    setEvents((prev) => [...prev, event])

    debugLog(
      'event',
      event.type,
      event.type === 'token' ? `(${(event.content as string)?.length ?? 0} chars)` : event
    )

    switch (event.type) {
      case 'token':
        chunkCount++
        totalTextLen += (event.content as string)?.length ?? 0
        if (!firstTokenLogged && runStartTime > 0) {
          log.info('perf', 'First token latency', { ttftMs: Date.now() - runStartTime })
          firstTokenLogged = true
        }
        setStreamingContent((prev) => prev + event.content)
        break
      case 'thinking':
        debugLog('thinking', 'received:', (event.content as string)?.length ?? 0, 'chars')
        setThinkingContent((prev) => prev + event.content)
        // Track for interleaved segments: accumulate into current block or start new one
        setThinkingSegments((prev) => {
          const last = prev[prev.length - 1]
          if (last && last.toolCallIds.length === 0) {
            // Still accumulating into the same thinking block (no tools have been called yet for this block)
            return [
              ...prev.slice(0, -1),
              { thinking: last.thinking + (event.content as string), toolCallIds: [] },
            ]
          }
          // New thinking block (after tool calls or at start)
          return [...prev, { thinking: event.content as string, toolCallIds: [] }]
        })
        break
      case 'tool_call': {
        log.info('tools', 'Tool call started', { tool: event.name })
        debugLog(
          'tools',
          'tool_call:',
          (event as { name?: string }).name,
          (event as { args?: unknown }).args
        )
        const newToolId = `${event.name}-${Date.now()}`
        // Extract file path from args for file-modifying tools
        const args = event.args as Record<string, unknown>
        const filePath = (args.file_path ?? args.filePath ?? args.path ?? args.output_path) as
          | string
          | undefined
        setActiveToolCalls((prev) => [
          ...prev,
          {
            id: newToolId,
            name: event.name,
            args: event.args,
            status: 'running' as const,
            startedAt: Date.now(),
            filePath,
          },
        ])
        // Associate this tool call with the current thinking segment
        setThinkingSegments((prev) => {
          if (prev.length === 0) {
            // Tool called before any thinking — add empty thinking segment
            return [{ thinking: '', toolCallIds: [newToolId] }]
          }
          const last = prev[prev.length - 1]!
          return [
            ...prev.slice(0, -1),
            { thinking: last.thinking, toolCallIds: [...last.toolCallIds, newToolId] },
          ]
        })
        break
      }
      case 'tool_result': {
        debugLog(
          'tools',
          'tool_result:',
          event.is_error ? 'ERROR' : 'OK',
          (event.content as string)?.slice(0, 120)
        )
        setActiveToolCalls((prev) => {
          // Use indexOf (first running) — results arrive in the same order as tool_call events
          const firstIdx = prev.findIndex((tc) => tc.status === 'running')
          if (firstIdx < 0) return prev

          const first = prev[firstIdx]
          const completedAt = Date.now()
          const durationMs = completedAt - first.startedAt
          log.info('tools', 'Tool call completed', {
            tool: first.name,
            success: !event.is_error,
            durationMs,
          })
          log.debug('perf', 'Tool execution time', { tool: first.name, durationMs })

          // Build diff data for file-modifying tools from their args
          let diff: { oldContent: string; newContent: string } | undefined = first.diff
          if (!event.is_error) {
            const toolArgs = first.args as Record<string, unknown>
            if (
              first.name === 'edit' ||
              first.name === 'apply_patch' ||
              first.name === 'multiedit'
            ) {
              // For edit tools: build diff from old_text/new_text args (Rust tool schema)
              // Also handle old_string/new_string variants for compatibility
              const oldStr = (toolArgs.old_text ??
                toolArgs.old_string ??
                toolArgs.old_content ??
                '') as string
              const newStr = (toolArgs.new_text ??
                toolArgs.new_string ??
                toolArgs.new_content ??
                '') as string
              if (oldStr || newStr) {
                diff = { oldContent: oldStr, newContent: newStr }
              }
            } else if (
              first.name === 'write' ||
              first.name === 'write_file' ||
              first.name === 'create_file'
            ) {
              // For write tools: treat as new file creation (empty old content)
              const content = (toolArgs.content ?? toolArgs.new_content ?? '') as string
              if (content) {
                diff = { oldContent: '', newContent: content }
              }
            }
          }

          // Create a NEW object so SolidJS For detects the change and re-renders.
          // Mutating in-place doesn't trigger reactivity because the object reference
          // stays the same — only the array reference changes, but For reconciles
          // by identity and keeps the existing component instance without updating.
          const updated = [...prev]
          updated[firstIdx] = {
            ...first,
            status: event.is_error ? 'error' : 'success',
            output: event.content,
            completedAt,
            diff,
          }
          return updated
        })
        break
      }
      case 'progress':
        log.debug('agent', 'Progress', { message: event.message })
        setProgressMessage(event.message)
        break
      case 'budget_warning': {
        const bw = event as import('../types/rust-ipc').BudgetWarningEvent
        log.warn('agent', 'Budget warning', {
          threshold: bw.thresholdPercent,
          current: bw.currentCostUsd,
          max: bw.maxBudgetUsd,
        })
        setBudgetWarning({
          thresholdPercent: bw.thresholdPercent,
          currentCostUsd: bw.currentCostUsd,
          maxBudgetUsd: bw.maxBudgetUsd,
        })
        break
      }
      case 'token_usage': {
        // Rust serializes as snake_case (input_tokens), TS types say camelCase
        const tu = event as unknown as Record<string, number>
        const input = tu.input_tokens ?? tu.inputTokens ?? 0
        const output = tu.output_tokens ?? tu.outputTokens ?? 0
        const cost = tu.cost_usd ?? tu.costUsd ?? 0
        log.debug('perf', 'Token usage update', { input, output, cost })
        setTokenUsage({ input, output, cost })
        break
      }
      case 'complete':
        debugLog('agent', 'complete event received')
        log.info('streaming', 'Stream completed', {
          chunks: chunkCount,
          totalChars: totalTextLen,
          elapsedMs: Date.now() - runStartTime,
        })
        batch(() => {
          setIsRunning(false)
          // Mark any remaining running tool calls as interrupted
          setActiveToolCalls((prev) => {
            const updated = [...prev]
            for (const tc of updated) {
              if (tc.status === 'running') {
                tc.status = 'success'
                tc.completedAt = Date.now()
              }
            }
            return updated
          })
        })
        // Resolve the web-mode completion promise so run() can finalize
        if (completionResolve) {
          const completeEvent = event as import('../types/rust-ipc').CompleteEvent
          completionResolve({
            success: true,
            turns: 0,
            sessionId: completeEvent.session?.id ?? '',
          })
          completionResolve = null
        }
        break
      case 'error':
        log.error('agent', 'Agent event error', { message: event.message })
        log.error('streaming', 'Stream error', {
          message: event.message,
          chunks: chunkCount,
          elapsedMs: Date.now() - runStartTime,
        })
        batch(() => {
          setError(event.message)
          setIsRunning(false)
        })
        // Resolve the web-mode completion promise on error too
        if (completionResolve) {
          completionResolve(null)
          completionResolve = null
        }
        break

      case 'plan_created': {
        debugLog('plan', 'plan_created event', (event as PlanCreatedEvent).plan?.summary)
        const planEvent = event as PlanCreatedEvent
        setPendingPlan(planEvent.plan)
        break
      }

      case 'todo_update': {
        const todoEvent = event as TodoUpdateEvent
        debugLog('todo', 'todo_update event', todoEvent.todos.length, 'items')
        setTodos(todoEvent.todos)
        // Auto-open the right panel to the Todos tab when todos arrive
        if (todoEvent.todos.length > 0) {
          try {
            const layout = useLayout()
            layout.switchRightPanelTab('todos')
          } catch {
            // Layout context may not be available in all environments
          }
        }
        break
      }

      // Praxis events — pass through to events signal for team bridge consumption
      case 'praxis_worker_started':
      case 'praxis_worker_progress':
      case 'praxis_worker_token':
      case 'praxis_worker_completed':
      case 'praxis_worker_failed':
      case 'praxis_all_complete':
      case 'praxis_summary':
      case 'praxis_phase_started':
      case 'praxis_phase_completed':
      case 'praxis_spec_created':
      case 'praxis_artifact_created':
      case 'praxis_conflict_detected':
        debugLog('team', event.type, (event as { worker_id?: string }).worker_id ?? '')
        // These are already added to events() signal above — the team bridge
        // in useAgent picks them up via the createEffect on rustAgent.events.
        // Additional state updates for Praxis completion:
        if (event.type === 'praxis_all_complete') {
          setIsRunning(false)
          if (completionResolve) {
            completionResolve({ success: true, turns: 0, sessionId: '' })
            completionResolve = null
          }
        }
        break
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
        handleAgentEvent(evt.payload)
      })
    } else if (eventSocket && eventSocket.readyState === WebSocket.OPEN) {
      // Browser: reuse existing open WebSocket connection
      return
    } else {
      // Browser mode — connect via WebSocket (first run or reconnect)
      if (eventSocket) {
        eventSocket.close()
        eventSocket = null
      }
      log.info('ws', 'Connecting to event WebSocket')
      const ws = createEventSocket()
      eventSocket = ws
      ws.onmessage = (evt) => {
        try {
          const event = JSON.parse(evt.data as string) as AgentEvent
          log.debug('ws', 'Message received', { type: event.type })
          handleAgentEvent(event)
        } catch {
          // Ignore malformed messages
        }
      }
      ws.onerror = () => {
        log.error('ws', 'WebSocket connection error')
        batch(() => {
          setError('WebSocket connection error')
          setIsRunning(false)
        })
        if (completionResolve) {
          completionResolve(null)
          completionResolve = null
        }
      }
      ws.onclose = () => {
        log.warn('ws', 'WebSocket disconnected')
        // If the socket closes while the agent is still running, treat it as an error
        if (isRunning()) {
          batch(() => {
            setError('WebSocket connection closed unexpectedly')
            setIsRunning(false)
          })
        }
        if (completionResolve) {
          completionResolve(null)
          completionResolve = null
        }
      }
      // Wait for the connection to open before returning
      await new Promise<void>((resolve, reject) => {
        ws.addEventListener('open', () => resolve(), { once: true })
        ws.addEventListener('error', () => reject(new Error('WebSocket connection failed')), {
          once: true,
        })
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
      eventSocket.close()
      eventSocket = null
    }
  }

  const resetState = (): void => {
    batch(() => {
      setEvents([])
      setStreamingContent('')
      setThinkingContent('')
      setActiveToolCalls([])
      setError(null)
      setLastResult(null)
      setTokenUsage({ input: 0, output: 0, cost: 0 })
      setProgressMessage(null)
      setBudgetWarning(null)
      setPendingPlan(null)
      setThinkingSegments([])
      setTodos([])
    })
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
    runStartTime = Date.now()
    chunkCount = 0
    totalTextLen = 0
    firstTokenLogged = false
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
        // Tauri mode: invoke blocks until the agent finishes
        const result = await invoke<SubmitGoalResult>('submit_goal', submitArgs)
        setLastResult(result)
        setIsRunning(false)
        return result
      }

      // Web mode: the HTTP call returns immediately while the agent runs async.
      // We need to wait for the complete/error event via WebSocket.
      const completionPromise = new Promise<SubmitGoalResult | null>((resolve) => {
        completionResolve = resolve
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
      completionResolve = null
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
    runStartTime = Date.now()
    chunkCount = 0
    totalTextLen = 0
    firstTokenLogged = false
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
        const result = await invoke<SubmitGoalResult>('edit_and_resend', editArgs)
        setLastResult(result)
        setIsRunning(false)
        return result
      }

      // Web mode: HTTP call returns immediately, wait for WebSocket completion
      const completionPromise = new Promise<SubmitGoalResult | null>((resolve) => {
        completionResolve = resolve
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
      completionResolve = null
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
    if (completionResolve) {
      completionResolve(null)
      completionResolve = null
    }
    detachListener()
  }

  const clearError = (): void => {
    setError(null)
  }

  // ── Mid-stream messaging (3-tier) ─────────────────────────────────

  /** Inject a steering message (Tier 1). Agent processes it after current tool. */
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

  /** Queue a follow-up message (Tier 2). Runs after agent completes current task. */
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

  /**
   * Tag the most-recently-started tool call for `toolName` with an approval decision.
   * Called by `useAgent.resolveApproval` right after the user acts on the ApprovalDock.
   */
  const markToolApproval = (toolName: string, decision: 'once' | 'always' | 'denied'): void => {
    setActiveToolCalls((prev) => {
      // Find the last tool call with this name (most recent pending/running/completed)
      const idx = [...prev].reverse().findIndex((tc) => tc.name === toolName)
      if (idx === -1) return prev
      const realIdx = prev.length - 1 - idx
      const updated = [...prev]
      updated[realIdx] = { ...prev[realIdx]!, approvalDecision: decision }
      return updated
    })
  }

  onCleanup(() => {
    destroyListener()
  })

  return {
    isRunning,
    streamingContent,
    thinkingContent,
    activeToolCalls,
    thinkingSegments,
    error,
    lastResult,
    tokenUsage,
    events,
    progressMessage,
    budgetWarning,
    pendingPlan,
    todos,
    run,
    editAndResendRun,
    cancel,
    clearError,
    // Mid-stream messaging
    steer,
    followUp,
    postComplete,
    markToolApproval,
    // Aliases for compatibility
    stop: cancel,
    isStreaming: isRunning,
    currentTokens: streamingContent,
    session: lastResult,
  }
}
