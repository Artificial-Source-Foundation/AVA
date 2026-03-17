import { isTauri, invoke as tauriInvoke } from '@tauri-apps/api/core'
import { listen, type UnlistenFn } from '@tauri-apps/api/event'
import { batch, createSignal, onCleanup } from 'solid-js'
import { apiInvoke, createEventSocket } from '../lib/api-client'
import { log } from '../lib/logger'
import type { ToolCall } from '../types'
import type { AgentEvent, SubmitGoalResult } from '../types/rust-ipc'

/** Invoke the backend — Tauri IPC or HTTP API depending on runtime. */
function invoke<T>(cmd: string, args?: Record<string, unknown>): Promise<T> {
  if (isTauri()) {
    return args ? tauriInvoke<T>(cmd, args) : tauriInvoke<T>(cmd)
  }
  return apiInvoke<T>(cmd, args)
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

  let unlisten: UnlistenFn | null = null
  let eventSocket: WebSocket | null = null

  /** Shared event handler — used by both Tauri listen() and WebSocket. */
  const handleAgentEvent = (event: AgentEvent): void => {
    setEvents((prev) => [...prev, event])

    switch (event.type) {
      case 'token':
        setStreamingContent((prev) => prev + event.content)
        break
      case 'thinking':
        setThinkingContent((prev) => prev + event.content)
        break
      case 'tool_call':
        setActiveToolCalls((prev) => [
          ...prev,
          {
            id: `${event.name}-${Date.now()}`,
            name: event.name,
            args: event.args,
            status: 'running' as const,
            startedAt: Date.now(),
          },
        ])
        break
      case 'tool_result': {
        setActiveToolCalls((prev) => {
          const updated = [...prev]
          const lastIdx = updated.map((tc) => tc.status).lastIndexOf('running')
          const last = lastIdx >= 0 ? updated[lastIdx] : undefined
          if (last) {
            last.status = event.is_error ? 'error' : 'success'
            last.output = event.content
            last.completedAt = Date.now()
          }
          return updated
        })
        break
      }
      case 'token_usage': {
        // Rust serializes as snake_case (input_tokens), TS types say camelCase
        const tu = event as unknown as Record<string, number>
        setTokenUsage({
          input: tu.input_tokens ?? tu.inputTokens ?? 0,
          output: tu.output_tokens ?? tu.outputTokens ?? 0,
          cost: tu.cost_usd ?? tu.costUsd ?? 0,
        })
        break
      }
      case 'complete':
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
        break
      case 'error':
        log.error('agent', 'Agent event error', { message: event.message })
        batch(() => {
          setError(event.message)
          setIsRunning(false)
        })
        break
    }
  }

  const attachListener = async (): Promise<void> => {
    detachListener()

    if (isTauri()) {
      unlisten = await listen<AgentEvent>('agent-event', (evt) => {
        handleAgentEvent(evt.payload)
      })
    } else {
      // Browser mode — connect via WebSocket
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

  const detachListener = (): void => {
    if (unlisten) {
      unlisten()
      unlisten = null
    }
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
    })
  }

  const run = async (
    goal: string,
    opts?: { provider?: string; model?: string; maxTurns?: number; thinkingLevel?: string }
  ): Promise<SubmitGoalResult | null> => {
    resetState()
    setIsRunning(true)
    try {
      await attachListener()
      const result = await invoke<SubmitGoalResult>('submit_goal', {
        args: {
          goal,
          maxTurns: opts?.maxTurns ?? 0,
          provider: opts?.provider ?? null,
          model: opts?.model ?? null,
          thinkingLevel: opts?.thinkingLevel ?? null,
        },
      })
      setLastResult(result)
      setIsRunning(false)
      return result
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      setError(message)
      setIsRunning(false)
      return null
    } finally {
      detachListener()
    }
  }

  const cancel = async (): Promise<void> => {
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
      setIsRunning(false)
    })
    detachListener()
  }

  const clearError = (): void => {
    setError(null)
  }

  // ── Mid-stream messaging (3-tier) ─────────────────────────────────

  /** Inject a steering message (Tier 1). Agent processes it after current tool. */
  const steer = async (message: string): Promise<void> => {
    if (!isRunning()) return
    try {
      await invoke('steer_agent', { message })
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      setError(msg)
    }
  }

  /** Queue a follow-up message (Tier 2). Runs after agent completes current task. */
  const followUp = async (message: string): Promise<void> => {
    if (!isRunning()) return
    try {
      await invoke('follow_up_agent', { message })
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      setError(msg)
    }
  }

  /** Queue a post-complete message (Tier 3). Runs in grouped pipeline after agent stops. */
  const postComplete = async (message: string, group?: number): Promise<void> => {
    if (!isRunning()) return
    try {
      await invoke('post_complete_agent', { args: { message, group: group ?? 1 } })
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err)
      setError(msg)
    }
  }

  onCleanup(() => {
    detachListener()
  })

  return {
    isRunning,
    streamingContent,
    thinkingContent,
    activeToolCalls,
    error,
    lastResult,
    tokenUsage,
    events,
    run,
    cancel,
    clearError,
    // Mid-stream messaging
    steer,
    followUp,
    postComplete,
    // Aliases for compatibility
    stop: cancel,
    isStreaming: isRunning,
    currentTokens: streamingContent,
    session: lastResult,
  }
}
