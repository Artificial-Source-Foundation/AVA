import { listen, type UnlistenFn } from '@tauri-apps/api/event'
import { createSignal, onCleanup } from 'solid-js'
import { rustAgent } from '../services/rust-bridge'
import type { AgentEvent, RustSession } from '../types/rust-ipc'

export function useRustAgent() {
  const [isRunning, setIsRunning] = createSignal(false)
  const [events, setEvents] = createSignal<AgentEvent[]>([])
  const [currentTokens, setCurrentTokens] = createSignal('')
  const [error, setError] = createSignal<string | null>(null)
  const [session, setSession] = createSignal<RustSession | null>(null)

  let activeRunId = 0
  let unlisten: UnlistenFn | null = null

  const appendEvent = (event: AgentEvent): void => {
    setEvents((prev) => [...prev, event])
    if (event.type === 'token') setCurrentTokens((prev) => prev + event.content)
    if (event.type === 'complete') {
      setSession(event.session)
      setIsRunning(false)
    }
    if (event.type === 'error') {
      setError(event.message)
      setIsRunning(false)
    }
  }

  const attachListener = async (runId: number): Promise<void> => {
    detachListener()
    unlisten = await listen<AgentEvent>('agent-event', (evt) => {
      if (runId !== activeRunId) return
      appendEvent(evt.payload)
    })
  }

  const detachListener = (): void => {
    if (!unlisten) return
    unlisten()
    unlisten = null
  }

  const resetState = (): void => {
    setEvents([])
    setCurrentTokens('')
    setError(null)
    setSession(null)
  }

  const run = async (goal: string): Promise<RustSession> => {
    const runId = activeRunId + 1
    activeRunId = runId
    resetState()
    setIsRunning(true)
    try {
      const result = await rustAgent.run(goal)
      if (runId !== activeRunId) throw new Error('Run cancelled')
      setSession(result)
      setIsRunning(false)
      return result
    } catch (error_) {
      const message = error_ instanceof Error ? error_.message : String(error_)
      setError(message)
      setIsRunning(false)
      throw new Error(message)
    }
  }

  const stream = async (goal: string): Promise<void> => {
    const runId = activeRunId + 1
    activeRunId = runId
    resetState()
    setIsRunning(true)
    try {
      await attachListener(runId)
      await rustAgent.stream(goal)
      if (runId === activeRunId) setIsRunning(false)
    } catch (error_) {
      const message = error_ instanceof Error ? error_.message : String(error_)
      setError(message)
      setIsRunning(false)
      throw new Error(message)
    } finally {
      detachListener()
    }
  }

  const stop = (): void => {
    activeRunId += 1
    setIsRunning(false)
    detachListener()
  }

  const clearError = (): void => {
    setError(null)
  }

  onCleanup(() => {
    stop()
  })

  return { isRunning, events, currentTokens, error, session, run, stream, stop, clearError }
}
