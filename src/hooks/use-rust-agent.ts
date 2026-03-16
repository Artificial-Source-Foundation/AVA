import { invoke } from '@tauri-apps/api/core'
import { listen, type UnlistenFn } from '@tauri-apps/api/event'
import { batch, createSignal, onCleanup } from 'solid-js'
import type { AgentEvent, SubmitGoalResult } from '../types/rust-ipc'
import type { ToolCall } from '../types'

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

  const attachListener = async (): Promise<void> => {
    detachListener()
    unlisten = await listen<AgentEvent>('agent-event', (evt) => {
      const event = evt.payload
      setEvents((prev) => [...prev, event])

      switch (event.type) {
        case 'token':
          setStreamingContent((prev) => prev + event.content)
          break
        case 'thinking':
          setThinkingContent((prev) => prev + event.content)
          break
        case 'tool_call':
          setActiveToolCalls((prev) => [...prev, {
            id: `${event.name}-${Date.now()}`,
            name: event.name,
            args: event.args,
            status: 'running' as const,
            startedAt: Date.now(),
          }])
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
        case 'token_usage':
          setTokenUsage({
            input: event.inputTokens,
            output: event.outputTokens,
            cost: event.costUsd,
          })
          break
        case 'complete':
          batch(() => {
            setIsRunning(false)
          })
          break
        case 'error':
          batch(() => {
            setError(event.message)
            setIsRunning(false)
          })
          break
      }
    })
  }

  const detachListener = (): void => {
    if (unlisten) { unlisten(); unlisten = null }
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

  const run = async (goal: string, opts?: { provider?: string; model?: string; maxTurns?: number }): Promise<SubmitGoalResult | null> => {
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
    } catch { /* ignore */ }
    setIsRunning(false)
    detachListener()
  }

  const clearError = (): void => { setError(null) }

  onCleanup(() => { detachListener() })

  return {
    isRunning, streamingContent, thinkingContent, activeToolCalls,
    error, lastResult, tokenUsage, events,
    run, cancel, clearError,
    // Aliases for compatibility
    stop: cancel,
    isStreaming: isRunning,
    currentTokens: streamingContent,
    session: lastResult,
  }
}
