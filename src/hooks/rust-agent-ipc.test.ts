import { createRoot, createSignal } from 'solid-js'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import {
  clearAllSessionIdMappings,
  registerBackendSessionId,
} from '../services/web-session-identity'
import type { ToolCall } from '../types'
import type { AgentEvent, AgentStatus, SubmitGoalResult } from '../types/rust-ipc'
import { createAgentIpc } from './rust-agent-ipc'

let isTauriRuntime = false
const tauriInvokeMock = vi.fn()
const listenMock = vi.fn()

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => isTauriRuntime,
  invoke: (...args: unknown[]) => tauriInvokeMock(...args),
}))

vi.mock('@tauri-apps/api/event', () => ({
  listen: (...args: unknown[]) => listenMock(...args),
}))

const apiInvokeMock = vi.fn()
const createEventSocketMock = vi.fn()

vi.mock('../lib/api-client', () => ({
  apiInvoke: (...args: unknown[]) => apiInvokeMock(...args),
  createEventSocket: () => createEventSocketMock(),
}))

class FakeWebSocket {
  static readonly CONNECTING = 0
  static readonly OPEN = 1
  static readonly CLOSED = 3

  readyState = FakeWebSocket.CONNECTING
  onmessage: ((event: MessageEvent) => void) | null = null
  onerror: (() => void) | null = null
  onclose: (() => void) | null = null

  private listeners = new Map<string, Set<() => void>>()
  private listenerWaiters = new Map<string, Array<() => void>>()

  addEventListener(type: string, listener: () => void): void {
    const listeners = this.listeners.get(type) ?? new Set<() => void>()
    listeners.add(listener)
    this.listeners.set(type, listeners)
    const waiters = this.listenerWaiters.get(type)
    if (waiters?.length) {
      this.listenerWaiters.delete(type)
      for (const waiter of waiters) {
        waiter()
      }
    }
  }

  waitForListener(type: string): Promise<void> {
    if ((this.listeners.get(type)?.size ?? 0) > 0) {
      return Promise.resolve()
    }

    return new Promise((resolve) => {
      const waiters = this.listenerWaiters.get(type) ?? []
      waiters.push(resolve)
      this.listenerWaiters.set(type, waiters)
    })
  }

  close(): void {
    this.readyState = FakeWebSocket.CLOSED
    this.onclose?.()
    this.emit('close')
  }

  emitOpen(): void {
    this.readyState = FakeWebSocket.OPEN
    this.emit('open')
  }

  emitMessage(event: AgentEvent): void {
    this.onmessage?.({ data: JSON.stringify(event) } as MessageEvent)
  }

  private emit(type: string): void {
    for (const listener of this.listeners.get(type) ?? []) {
      listener()
    }
  }
}

function createDeferred<T>() {
  let resolve!: (value: T) => void
  let reject!: (reason?: unknown) => void
  const promise = new Promise<T>((resolvePromise, rejectPromise) => {
    resolve = resolvePromise
    reject = rejectPromise
  })
  return { promise, resolve, reject }
}

function extractInvokeRunId(callIndex = 0): string {
  const args = tauriInvokeMock.mock.calls[callIndex]?.[1] as
    | { args?: { runId?: string } }
    | undefined
  const runId = args?.args?.runId
  if (!runId) {
    throw new Error(`Missing runId for invoke call ${callIndex}`)
  }
  return runId
}

function createIpcHarness() {
  return createRoot((dispose) => {
    const [isRunning, setIsRunning] = createSignal(false)
    const [error, setError] = createSignal<string | null>(null)
    const [lastResult, setLastResult] = createSignal<SubmitGoalResult | null>(null)
    const [currentRunId, setCurrentRunId] = createSignal<string | null>(null)
    const [trackedSessionId, setTrackedSessionId] = createSignal<string | null>(null)
    const [, setDetachedSessionId] = createSignal<string | null>(null)
    const [activeToolCalls, setActiveToolCalls] = createSignal<ToolCall[]>([])
    const handledEvents: AgentEvent[] = []
    const completion = { resolve: null as ((result: SubmitGoalResult | null) => void) | null }
    const resetState = vi.fn(() => {
      setIsRunning(false)
      setError(null)
      setLastResult(null)
      setCurrentRunId(null)
      setTrackedSessionId(null)
      setDetachedSessionId(null)
      setActiveToolCalls([])
    })

    const ipc = createAgentIpc({
      metrics: {
        chunkCount: 0,
        totalTextLen: 0,
        runStartTime: 0,
        firstTokenLogged: false,
        pendingToolNames: [],
      },
      completion,
      isRunning,
      setIsRunning,
      setError,
      setLastResult,
      setCurrentRunId,
      setTrackedSessionId,
      setDetachedSessionId,
      setActiveToolCalls,
      handleAgentEvent: (event) => {
        handledEvents.push(event)
        if (event.type === 'complete') {
          completion.resolve?.({
            success: true,
            turns: 0,
            sessionId: event.session.id,
          })
          completion.resolve = null
        }
        if (event.type === 'error') {
          completion.resolve?.(null)
          completion.resolve = null
        }
      },
      resetState,
    })

    return {
      dispose,
      ipc,
      isRunning,
      setIsRunning,
      error,
      lastResult,
      currentRunId,
      setCurrentRunId,
      trackedSessionId,
      setTrackedSessionId,
      activeToolCalls,
      handledEvents,
      completion,
    }
  })
}

describe('createAgentIpc', () => {
  beforeEach(() => {
    isTauriRuntime = false
    vi.clearAllMocks()
    vi.useRealTimers()
    clearAllSessionIdMappings()
  })

  it('rehydrates frontend running state from backend status', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-rehydrate',
      })
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-rehydrate',
      })

    const harness = createIpcHarness()
    const rehydratePromise = harness.ipc.rehydrateStatus('session-front')
    await flushPromises()
    socket.emitOpen()
    await rehydratePromise

    expect(apiInvokeMock).toHaveBeenNthCalledWith(1, 'get_agent_status', {
      sessionId: 'session-front',
    })
    expect(apiInvokeMock).toHaveBeenNthCalledWith(2, 'get_agent_status', {
      sessionId: 'session-front',
      runId: 'web-run-rehydrate',
    })
    expect(harness.isRunning()).toBe(true)
    expect(harness.currentRunId()).toBe('web-run-rehydrate')

    harness.dispose()
  })

  it('resolves frontend session aliases before browser status rehydrate calls and restores pending interactive state', async () => {
    registerBackendSessionId('session-front', 'session-back')

    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-rehydrate',
      })
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-rehydrate',
        pendingApproval: {
          type: 'approval_request',
          id: 'approval-aliased',
          tool_call_id: 'tool-1',
          tool_name: 'bash',
          args: { command: 'pwd' },
          risk_level: 'low',
          reason: 'Need confirmation',
          warnings: [],
          run_id: 'web-run-rehydrate',
        },
      })

    const harness = createIpcHarness()
    const rehydratePromise = harness.ipc.rehydrateStatus('session-front')
    await flushPromises()
    socket.emitOpen()
    const rehydrateResult = await rehydratePromise

    expect(apiInvokeMock).toHaveBeenNthCalledWith(1, 'get_agent_status', {
      sessionId: 'session-back',
    })
    expect(apiInvokeMock).toHaveBeenNthCalledWith(2, 'get_agent_status', {
      sessionId: 'session-back',
      runId: 'web-run-rehydrate',
    })
    expect(rehydrateResult).toEqual({
      sessionId: 'session-front',
      running: true,
      runId: 'web-run-rehydrate',
      pendingApproval: expect.objectContaining({
        id: 'approval-aliased',
      }),
      pendingQuestion: null,
      pendingPlan: null,
    })
    expect(harness.handledEvents).toContainEqual(
      expect.objectContaining({
        type: 'approval_request',
        id: 'approval-aliased',
      })
    )

    harness.dispose()
  })

  it('keeps an optimistically restored cached run visible while backend rehydrate validates it', async () => {
    const pendingStatus = createDeferred<AgentStatus>()
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock
      .mockImplementationOnce(() => pendingStatus.promise)
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-cached',
      })

    const harness = createIpcHarness()
    harness.ipc.restoreSessionBinding({
      activeRunId: 'web-run-cached',
      attachedSessionId: 'session-front',
    })
    harness.setIsRunning(true)
    harness.setCurrentRunId('web-run-cached')
    harness.setTrackedSessionId('session-front')

    const rehydratePromise = harness.ipc.rehydrateStatus('session-front')
    await flushPromises()

    expect(harness.isRunning()).toBe(true)
    expect(harness.currentRunId()).toBe('web-run-cached')
    expect(harness.trackedSessionId()).toBe('session-front')

    pendingStatus.resolve({
      running: true,
      provider: 'openai',
      model: 'gpt-5',
      runId: 'web-run-cached',
    })
    await flushPromises()
    socket.emitOpen()
    await rehydratePromise

    expect(harness.isRunning()).toBe(true)
    expect(harness.currentRunId()).toBe('web-run-cached')
    expect(harness.trackedSessionId()).toBe('session-front')

    harness.dispose()
  })

  it('rehydrates pending interactive state from backend status', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-rehydrate',
      })
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-rehydrate',
        pendingApproval: {
          type: 'approval_request',
          id: 'approval-current',
          tool_call_id: 'tool-1',
          tool_name: 'bash',
          args: { command: 'pwd' },
          risk_level: 'low',
          reason: 'Need confirmation',
          warnings: [],
          run_id: 'web-run-rehydrate',
        },
      })

    const harness = createIpcHarness()
    const rehydratePromise = harness.ipc.rehydrateStatus('session-front')
    await flushPromises()
    socket.emitOpen()
    await rehydratePromise

    expect(harness.handledEvents).toContainEqual(
      expect.objectContaining({
        type: 'approval_request',
        id: 'approval-current',
        run_id: 'web-run-rehydrate',
      })
    )

    harness.dispose()
  })

  it('does not rehydrate running state when backend runId is missing', async () => {
    apiInvokeMock.mockResolvedValueOnce({
      running: true,
      provider: 'openai',
      model: 'gpt-5',
      runId: null,
    })

    const harness = createIpcHarness()
    await harness.ipc.rehydrateStatus('session-front')

    expect(harness.isRunning()).toBe(false)
    expect(harness.currentRunId()).toBeNull()
    expect(harness.trackedSessionId()).toBeNull()

    harness.dispose()
  })

  it('keeps browser rehydration scoped to the requested session', async () => {
    apiInvokeMock.mockResolvedValueOnce({
      running: false,
      provider: 'openai',
      model: 'gpt-5',
      runId: null,
    })

    const harness = createIpcHarness()
    await harness.ipc.rehydrateStatus('session-front')

    expect(apiInvokeMock).toHaveBeenCalledWith('get_agent_status', {
      sessionId: 'session-front',
    })
    expect(harness.isRunning()).toBe(false)
    expect(harness.currentRunId()).toBeNull()
    expect(harness.trackedSessionId()).toBeNull()

    harness.dispose()
  })

  it('does not let a stale in-flight rehydrate clobber a freshly started visible run', async () => {
    const pendingStatus = createDeferred<AgentStatus>()
    const submitResult = createDeferred<SubmitGoalResult>()
    const socket = new FakeWebSocket()

    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock
      .mockImplementationOnce(() => pendingStatus.promise)
      .mockImplementationOnce(() => submitResult.promise)

    const harness = createIpcHarness()
    const staleRehydrate = harness.ipc.rehydrateStatus('session-fresh')

    await flushPromises()

    const runPromise = harness.ipc.run('ship visible', { sessionId: 'session-fresh' })
    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    const visibleRunId = (harness.currentRunId() ?? '').trim()
    expect(visibleRunId).not.toBe('')
    expect(harness.trackedSessionId()).toBe('session-fresh')

    pendingStatus.resolve({
      running: false,
      provider: 'openai',
      model: 'gpt-5',
      runId: null,
    })
    await staleRehydrate

    expect(harness.isRunning()).toBe(true)
    expect(harness.currentRunId()).toBe(visibleRunId)
    expect(harness.trackedSessionId()).toBe('session-fresh')

    socket.emitMessage({
      type: 'complete',
      run_id: visibleRunId,
      session: { id: 'session-fresh', messages: [], completed: true },
    })
    submitResult.resolve({ success: true, turns: 1, sessionId: 'session-fresh' })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-fresh',
    })

    harness.dispose()
  })

  it('ignores uncorrelated websocket events while attached to an idle session', async () => {
    apiInvokeMock.mockResolvedValueOnce({
      running: false,
      provider: 'openai',
      model: 'gpt-5',
      runId: null,
    })

    const harness = createIpcHarness()
    await harness.ipc.rehydrateStatus('session-front')

    harness.setIsRunning(false)

    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    const attachPromise = harness.ipc.attachListener()
    socket.emitOpen()
    await attachPromise

    socket.emitMessage({ type: 'progress', message: 'idle progress' })
    socket.emitMessage({ type: 'thinking', content: 'idle thinking' })
    socket.emitMessage({
      type: 'complete',
      session: { id: 'idle-session', messages: [], completed: true },
    })

    expect(harness.handledEvents).toEqual([])

    harness.dispose()
  })

  it('clears browser attachment when the active session becomes null and ignores late events', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-rehydrate',
      })
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-rehydrate',
      })

    const harness = createIpcHarness()
    const rehydratePromise = harness.ipc.rehydrateStatus('session-front')
    await flushPromises()
    socket.emitOpen()
    await rehydratePromise

    await harness.ipc.rehydrateStatus(null)

    expect(apiInvokeMock).toHaveBeenCalledTimes(2)
    expect(harness.isRunning()).toBe(false)
    expect(harness.currentRunId()).toBeNull()

    socket.emitMessage({
      type: 'progress',
      message: 'late correlated event',
      run_id: 'web-run-rehydrate',
    })
    socket.emitMessage({
      type: 'thinking',
      content: 'late uncorrelated event',
    })

    expect(harness.handledEvents).toEqual([])

    harness.dispose()
  })

  it('ignores late websocket events from the old session while switching to a new session', async () => {
    const pendingFreshStatus = createDeferred<AgentStatus>()
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-old',
      })
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-old',
      })
      .mockImplementationOnce(() => pendingFreshStatus.promise)

    const harness = createIpcHarness()
    const oldRehydrate = harness.ipc.rehydrateStatus('session-old')
    await flushPromises()
    socket.emitOpen()
    await oldRehydrate

    const switchPromise = harness.ipc.rehydrateStatus('session-fresh')
    await flushPromises()

    socket.emitMessage({
      type: 'thinking',
      content: 'late old-session thinking',
    })
    socket.emitMessage({
      type: 'progress',
      message: 'late old-session progress',
    })
    socket.emitMessage({
      type: 'progress',
      message: 'late correlated old-session progress',
      run_id: 'web-run-old',
    })

    expect(harness.handledEvents).toEqual([])

    pendingFreshStatus.resolve({
      running: false,
      provider: 'openai',
      model: 'gpt-5',
      runId: null,
    })
    await switchPromise

    expect(harness.isRunning()).toBe(false)
    expect(harness.currentRunId()).toBeNull()

    harness.dispose()
  })

  it('settles an in-flight web run when the active session is cleared', async () => {
    const submitResult = createDeferred<SubmitGoalResult>()
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock.mockImplementationOnce(() => submitResult.promise)

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship web', { sessionId: 'session-front' })

    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    expect((harness.currentRunId() ?? '').trim()).not.toBe('')

    await harness.ipc.rehydrateStatus(null)

    submitResult.resolve({ success: true, turns: 1, sessionId: 'session-web' })

    await expect(runPromise).resolves.toEqual({
      success: false,
      turns: 0,
      sessionId: 'session-front',
      detachedSessionId: 'session-front',
    })
    expect(harness.isRunning()).toBe(false)
    expect(harness.currentRunId()).toBeNull()
    expect(harness.lastResult()).toBeNull()

    harness.dispose()
  })

  it('clears the tracked session binding when a web run completes', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-web' })

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship web', { sessionId: 'session-front' })

    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    const runId = (harness.currentRunId() ?? '').trim()
    expect(runId).not.toBe('')
    expect(harness.trackedSessionId()).toBe('session-front')

    socket.emitMessage({
      type: 'complete',
      run_id: runId,
      session: { id: 'session-web', messages: [], completed: true },
    })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-web',
    })
    expect(harness.currentRunId()).toBeNull()
    expect(harness.trackedSessionId()).toBeNull()

    harness.dispose()
  })

  it('keeps the latest browser rehydration result when an older session rehydrate resolves late', async () => {
    const staleStatus = createDeferred<AgentStatus>()
    const socket = new FakeWebSocket()

    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock
      .mockImplementationOnce(() => staleStatus.promise)
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-fresh',
      })
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-fresh',
      })

    const harness = createIpcHarness()

    const staleRehydrate = harness.ipc.rehydrateStatus('session-old')
    const freshRehydrate = harness.ipc.rehydrateStatus('session-fresh')

    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    await freshRehydrate

    expect(harness.isRunning()).toBe(true)
    expect(harness.currentRunId()).toBe('web-run-fresh')

    staleStatus.resolve({
      running: true,
      provider: 'openai',
      model: 'gpt-5',
      runId: 'web-run-stale',
    })

    await staleRehydrate

    expect(harness.isRunning()).toBe(true)
    expect(harness.currentRunId()).toBe('web-run-fresh')

    expect(apiInvokeMock).toHaveBeenCalledTimes(3)
    expect(apiInvokeMock).toHaveBeenNthCalledWith(1, 'get_agent_status', {
      sessionId: 'session-old',
    })
    expect(apiInvokeMock).toHaveBeenNthCalledWith(2, 'get_agent_status', {
      sessionId: 'session-fresh',
    })
    expect(apiInvokeMock).toHaveBeenNthCalledWith(3, 'get_agent_status', {
      sessionId: 'session-fresh',
      runId: 'web-run-fresh',
    })

    harness.dispose()
  })

  it('accepts only matching correlated websocket events after status rehydration', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-rehydrate',
      })
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-rehydrate',
      })

    const harness = createIpcHarness()
    const rehydratePromise = harness.ipc.rehydrateStatus('session-front')
    await flushPromises()
    socket.emitOpen()
    await rehydratePromise

    socket.emitMessage({
      type: 'progress',
      message: 'stale',
      run_id: 'web-run-old',
    })
    socket.emitMessage({
      type: 'progress',
      message: 'fresh',
      run_id: 'web-run-rehydrate',
    })

    expect(harness.handledEvents).toEqual([
      { type: 'progress', message: 'fresh', run_id: 'web-run-rehydrate' },
    ])

    harness.dispose()
  })

  it('clears stale running state when a rehydrated Tauri run finishes before listener attach completes', async () => {
    isTauriRuntime = true

    listenMock.mockImplementationOnce(async () => () => {})
    tauriInvokeMock
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'tauri-run-rehydrate',
      })
      .mockResolvedValueOnce({
        running: false,
        provider: 'openai',
        model: 'gpt-5',
        runId: null,
      })

    const harness = createIpcHarness()
    await harness.ipc.rehydrateStatus('session-front')

    expect(tauriInvokeMock).toHaveBeenNthCalledWith(1, 'get_agent_status', {
      args: { sessionId: 'session-front' },
    })
    expect(tauriInvokeMock).toHaveBeenNthCalledWith(2, 'get_agent_status', {
      args: { sessionId: 'session-front', runId: 'tauri-run-rehydrate' },
    })
    expect(harness.isRunning()).toBe(false)
    expect(harness.currentRunId()).toBeNull()
    expect(harness.trackedSessionId()).toBeNull()

    harness.dispose()
  })

  it('rolls back optimistic running state when rehydrate reconcile fails after listener attach', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-rehydrate',
      })
      .mockRejectedValueOnce(new Error('reconcile failed'))

    const harness = createIpcHarness()
    const rehydratePromise = harness.ipc.rehydrateStatus('session-front')
    await flushPromises()
    socket.emitOpen()
    await rehydratePromise

    expect(apiInvokeMock).toHaveBeenNthCalledWith(1, 'get_agent_status', {
      sessionId: 'session-front',
    })
    expect(apiInvokeMock).toHaveBeenNthCalledWith(2, 'get_agent_status', {
      sessionId: 'session-front',
      runId: 'web-run-rehydrate',
    })
    expect(harness.isRunning()).toBe(false)
    expect(harness.currentRunId()).toBeNull()

    harness.dispose()
  })

  it('ignores stale websocket closes during reconnect', async () => {
    const first = new FakeWebSocket()
    const second = new FakeWebSocket()
    createEventSocketMock
      .mockReturnValueOnce(first as unknown as WebSocket)
      .mockReturnValueOnce(second as unknown as WebSocket)

    const harness = createIpcHarness()
    const attachFirst = harness.ipc.attachListener()
    first.emitOpen()
    await attachFirst

    harness.setIsRunning(true)
    first.readyState = FakeWebSocket.CLOSED

    const attachSecond = harness.ipc.attachListener()
    second.emitOpen()
    await attachSecond

    expect(harness.error()).toBeNull()
    expect(harness.isRunning()).toBe(true)

    harness.dispose()
  })

  it('routes websocket messages only from the active socket', async () => {
    const first = new FakeWebSocket()
    const second = new FakeWebSocket()
    createEventSocketMock
      .mockReturnValueOnce(first as unknown as WebSocket)
      .mockReturnValueOnce(second as unknown as WebSocket)
    apiInvokeMock
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-active',
      })
      .mockResolvedValueOnce({
        running: true,
        provider: 'openai',
        model: 'gpt-5',
        runId: 'web-run-active',
      })

    const harness = createIpcHarness()
    const attachFirst = harness.ipc.rehydrateStatus('session-front')
    await flushPromises()
    first.emitOpen()
    await attachFirst

    first.readyState = FakeWebSocket.CLOSED
    const attachSecond = harness.ipc.attachListener()
    second.emitOpen()
    await attachSecond

    first.emitMessage({ type: 'progress', message: 'stale', run_id: 'web-run-active' })
    second.emitMessage({ type: 'progress', message: 'fresh', run_id: 'web-run-active' })

    expect(harness.handledEvents).toHaveLength(1)
    expect(harness.handledEvents[0]).toMatchObject({
      type: 'progress',
      message: 'fresh',
      run_id: 'web-run-active',
    })

    harness.dispose()
  })

  it('drops stale correlated websocket interactive events from earlier runs', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-web' })

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship web')

    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    const runId = (harness.currentRunId() ?? '').trim()
    expect(runId).not.toBe('')

    socket.emitMessage({
      type: 'approval_request',
      id: 'approval-stale',
      tool_name: 'bash',
      args: { command: 'rm -rf /tmp/demo' },
      risk_level: 'high',
      reason: 'destructive command',
      warnings: [],
      run_id: 'web-run-old',
    })
    socket.emitMessage({
      type: 'approval_request',
      id: 'approval-fresh',
      tool_name: 'bash',
      args: { command: 'pwd' },
      risk_level: 'low',
      reason: 'read cwd',
      warnings: [],
      run_id: runId,
    })
    socket.emitMessage({
      type: 'interactive_request_cleared',
      request_id: 'approval-fresh',
      request_kind: 'approval',
      timed_out: true,
      run_id: 'web-run-old',
    })
    socket.emitMessage({
      type: 'complete',
      run_id: runId,
      session: { id: 'session-web', messages: [], completed: true },
    })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-web',
    })

    expect(harness.handledEvents).toEqual([
      {
        type: 'approval_request',
        id: 'approval-fresh',
        tool_name: 'bash',
        args: { command: 'pwd' },
        risk_level: 'low',
        reason: 'read cwd',
        warnings: [],
        run_id: runId,
      },
      {
        type: 'complete',
        run_id: runId,
        session: { id: 'session-web', messages: [], completed: true },
      },
    ])

    harness.dispose()
  })

  it('accepts same-session uncorrelated complete events while ignoring stale-session ones', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-web' })

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship web', { sessionId: 'session-front' })

    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    const runId = (harness.currentRunId() ?? '').trim()
    expect(runId).not.toBe('')

    let runSettled = false
    void runPromise.then(() => {
      runSettled = true
    })

    socket.emitMessage({
      type: 'complete',
      session: { id: 'stale-session', messages: [], completed: true },
    })
    await flushPromises()

    expect(runSettled).toBe(false)
    expect(harness.handledEvents).toHaveLength(0)

    socket.emitMessage({
      type: 'complete',
      session: { id: 'session-front', messages: [], completed: true },
    })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-front',
    })

    expect(harness.handledEvents).toEqual([
      {
        type: 'complete',
        session: { id: 'session-front', messages: [], completed: true },
      },
    ])

    harness.dispose()
  })

  it('drops required correlated backend events when run_id is missing', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-web' })

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship web')

    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    const runId = (harness.currentRunId() ?? '').trim()
    expect(runId).not.toBe('')

    socket.emitMessage({
      type: 'plan_step_complete',
      step_id: 'step-stale',
    } as AgentEvent)
    socket.emitMessage({
      type: 'streaming_edit_progress',
      call_id: 'edit-1',
      tool_name: 'apply_patch',
      bytes_received: 128,
    } as AgentEvent)
    socket.emitMessage({
      type: 'subagent_complete',
      call_id: 'task-1',
      session_id: 'child-session',
      description: 'delegate',
      input_tokens: 1,
      output_tokens: 2,
      cost_usd: 0.1,
      resumed: false,
    } as AgentEvent)
    socket.emitMessage({
      type: 'complete',
      run_id: runId,
      session: { id: 'fresh-session', messages: [], completed: true },
    })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'fresh-session',
    })

    expect(harness.handledEvents).toEqual([
      {
        type: 'complete',
        run_id: runId,
        session: { id: 'fresh-session', messages: [], completed: true },
      },
    ])

    harness.dispose()
  })

  it('returns early and clears running state when submit_goal is rejected in web mode before stream events', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock.mockResolvedValueOnce({
      success: false,
      turns: 2,
      sessionId: 'session-web-invalid',
    })

    const harness = createIpcHarness()
    const submitPromise = harness.ipc.run('ship web')

    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    await expect(submitPromise).resolves.toEqual({
      success: false,
      turns: 2,
      sessionId: 'session-web-invalid',
    })

    expect(harness.isRunning()).toBe(false)
    expect(harness.currentRunId()).toBeNull()
    expect(harness.lastResult()).toEqual({
      success: false,
      turns: 2,
      sessionId: 'session-web-invalid',
    })
    expect(harness.handledEvents).toHaveLength(0)

    harness.dispose()
  })

  it('forwards per-run thinking and compaction options through browser submit_goal', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-web' })

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship web', {
      provider: 'openai',
      model: 'gpt-5.4',
      thinkingLevel: 'high',
      sessionId: 'session-front',
      autoCompact: false,
      compactionThreshold: 72,
      compactionProvider: 'anthropic',
      compactionModel: 'claude-sonnet-4.6',
    })

    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    expect(apiInvokeMock).toHaveBeenCalledWith('submit_goal', {
      args: expect.objectContaining({
        goal: 'ship web',
        provider: 'openai',
        model: 'gpt-5.4',
        thinkingLevel: 'high',
        sessionId: 'session-front',
        autoCompact: false,
        compactionThreshold: 72,
        compactionProvider: 'anthropic',
        compactionModel: 'claude-sonnet-4.6',
        runId: expect.any(String),
      }),
    })

    socket.emitMessage({
      type: 'complete',
      run_id: harness.currentRunId() ?? undefined,
      session: { id: 'session-web', messages: [], completed: true },
    })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-web',
    })

    harness.dispose()
  })

  it('resolves frontend session aliases before browser submit_goal invocation', async () => {
    registerBackendSessionId('session-front', 'session-back')

    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-back' })

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship web', {
      sessionId: 'session-front',
    })

    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    expect(apiInvokeMock).toHaveBeenCalledWith('submit_goal', {
      args: expect.objectContaining({
        goal: 'ship web',
        sessionId: 'session-back',
        runId: expect.any(String),
      }),
    })

    socket.emitMessage({
      type: 'complete',
      run_id: harness.currentRunId() ?? undefined,
      session: { id: 'session-back', messages: [], completed: true },
    })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-back',
    })

    harness.dispose()
  })

  it('forwards explicit session ownership through browser replay commands', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValue(socket as unknown as WebSocket)
    const harness = createIpcHarness()

    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-web' })
    const retryPromise = harness.ipc.retryRun('session-front')
    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    expect(apiInvokeMock).toHaveBeenNthCalledWith(1, 'retry_last_message', {
      args: expect.objectContaining({
        sessionId: 'session-front',
        runId: expect.any(String),
      }),
    })

    socket.emitMessage({
      type: 'complete',
      run_id: harness.currentRunId() ?? undefined,
      session: { id: 'session-web', messages: [], completed: true },
    })
    await expect(retryPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-web',
    })

    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-web' })
    const editPromise = harness.ipc.editAndResendRun('user-1', 'retry this', 'session-front')
    await flushPromises()

    expect(apiInvokeMock).toHaveBeenNthCalledWith(2, 'edit_and_resend', {
      args: expect.objectContaining({
        messageId: 'user-1',
        newContent: 'retry this',
        sessionId: 'session-front',
        runId: expect.any(String),
      }),
    })

    socket.emitMessage({
      type: 'complete',
      run_id: harness.currentRunId() ?? undefined,
      session: { id: 'session-web', messages: [], completed: true },
    })
    await expect(editPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-web',
    })

    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-web' })
    const regeneratePromise = harness.ipc.regenerateRun('session-front')
    await flushPromises()

    expect(apiInvokeMock).toHaveBeenNthCalledWith(3, 'regenerate_response', {
      args: expect.objectContaining({
        sessionId: 'session-front',
        runId: expect.any(String),
      }),
    })

    socket.emitMessage({
      type: 'complete',
      run_id: harness.currentRunId() ?? undefined,
      session: { id: 'session-web', messages: [], completed: true },
    })
    await expect(regeneratePromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-web',
    })

    harness.dispose()
  })

  it('resolves frontend session aliases before invoking browser replay commands', async () => {
    registerBackendSessionId('session-front', 'session-back')

    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValue(socket as unknown as WebSocket)
    const harness = createIpcHarness()

    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-back' })
    const retryPromise = harness.ipc.retryRun('session-front')
    await flushPromises()
    socket.emitOpen()
    await flushPromises()

    expect(apiInvokeMock).toHaveBeenNthCalledWith(1, 'retry_last_message', {
      args: expect.objectContaining({
        sessionId: 'session-back',
        runId: expect.any(String),
      }),
    })

    socket.emitMessage({
      type: 'complete',
      run_id: harness.currentRunId() ?? undefined,
      session: { id: 'session-back', messages: [], completed: true },
    })
    await expect(retryPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-back',
    })

    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-back' })
    const editPromise = harness.ipc.editAndResendRun('user-1', 'retry this', 'session-front')
    await flushPromises()

    expect(apiInvokeMock).toHaveBeenNthCalledWith(2, 'edit_and_resend', {
      args: expect.objectContaining({
        messageId: 'user-1',
        newContent: 'retry this',
        sessionId: 'session-back',
        runId: expect.any(String),
      }),
    })

    socket.emitMessage({
      type: 'complete',
      run_id: harness.currentRunId() ?? undefined,
      session: { id: 'session-back', messages: [], completed: true },
    })
    await expect(editPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-back',
    })

    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-back' })
    const regeneratePromise = harness.ipc.regenerateRun('session-front')
    await flushPromises()

    expect(apiInvokeMock).toHaveBeenNthCalledWith(3, 'regenerate_response', {
      args: expect.objectContaining({
        sessionId: 'session-back',
        runId: expect.any(String),
      }),
    })

    socket.emitMessage({
      type: 'complete',
      run_id: harness.currentRunId() ?? undefined,
      session: { id: 'session-back', messages: [], completed: true },
    })
    await expect(regeneratePromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-back',
    })

    harness.dispose()
  })

  it('forwards provider/model through Tauri submit_goal and settles on the matching terminal event', async () => {
    isTauriRuntime = true
    let listener: ((evt: { payload: AgentEvent }) => void) | null = null
    let releaseInvoke: ((value: SubmitGoalResult) => void) | null = null

    listenMock.mockImplementationOnce(
      async (_event: string, cb: (evt: { payload: AgentEvent }) => void) => {
        listener = cb
        return () => {}
      }
    )
    tauriInvokeMock.mockImplementationOnce(
      () =>
        new Promise<SubmitGoalResult>((resolve) => {
          releaseInvoke = resolve
        })
    )

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship it', {
      provider: 'openai',
      model: 'gpt-5.4',
      sessionId: 'session-front',
      images: [{ data: 'base64-image', mediaType: 'image/png' }],
    })

    await waitForInvokeCall(0)
    expect(tauriInvokeMock).toHaveBeenCalledWith('submit_goal', {
      args: expect.objectContaining({
        goal: 'ship it',
        provider: 'openai',
        model: 'gpt-5.4',
        sessionId: 'session-front',
        images: [{ data: 'base64-image', mediaType: 'image/png' }],
        runId: expect.any(String),
      }),
    })

    if (!listener) {
      throw new Error('Tauri listener was not attached')
    }

    const runId = extractInvokeRunId(0)
    const emitTerminalEvent: (evt: { payload: AgentEvent }) => void = listener
    emitTerminalEvent({
      payload: {
        type: 'complete',
        runId,
        session: {
          id: 'session-terminal',
          messages: [],
          completed: true,
        },
      },
    })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-terminal',
    })

    if (!releaseInvoke) {
      throw new Error('Tauri invoke promise was not captured')
    }
    const resolveInvoke: (value: SubmitGoalResult) => void = releaseInvoke
    resolveInvoke({ success: true, turns: 1, sessionId: 'session-terminal' })
    harness.dispose()
  })

  it('ignores uncorrelated Tauri events while a desktop run is active', async () => {
    isTauriRuntime = true
    let listener: ((evt: { payload: AgentEvent }) => void) | null = null
    let releaseInvoke: ((value: SubmitGoalResult) => void) | null = null

    listenMock.mockImplementationOnce(
      async (_event: string, cb: (evt: { payload: AgentEvent }) => void) => {
        listener = cb
        return () => {}
      }
    )
    tauriInvokeMock.mockImplementationOnce(
      () =>
        new Promise<SubmitGoalResult>((resolve) => {
          releaseInvoke = resolve
        })
    )

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship tauri', { sessionId: 'session-front' })

    await waitForInvokeCall(0)

    if (!listener) {
      throw new Error('Tauri listener was not attached')
    }

    const runId = extractInvokeRunId(0)
    const emitEvent = listener as (evt: { payload: AgentEvent }) => void

    emitEvent({
      payload: {
        type: 'progress',
        message: 'uncorrelated',
      },
    })
    emitEvent({
      payload: {
        type: 'complete',
        runId,
        session: {
          id: 'session-terminal',
          messages: [],
          completed: true,
        },
      },
    })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-terminal',
    })
    expect(harness.handledEvents).toEqual([
      {
        type: 'complete',
        runId,
        session: {
          id: 'session-terminal',
          messages: [],
          completed: true,
        },
      },
    ])

    if (!releaseInvoke) {
      throw new Error('Tauri invoke promise was not captured')
    }
    const resolveInvoke: (value: SubmitGoalResult) => void = releaseInvoke
    resolveInvoke({ success: true, turns: 1, sessionId: 'session-terminal' })
    harness.dispose()
  })

  it('settles Tauri runs from terminal events even when invoke resolves early', async () => {
    isTauriRuntime = true
    let listener: ((evt: { payload: AgentEvent }) => void) | null = null
    let releaseInvoke: ((value: SubmitGoalResult) => void) | null = null

    listenMock.mockImplementationOnce(
      async (_event: string, cb: (evt: { payload: AgentEvent }) => void) => {
        listener = cb
        return () => {}
      }
    )
    tauriInvokeMock.mockImplementationOnce(
      () =>
        new Promise<SubmitGoalResult>((resolve) => {
          releaseInvoke = resolve
        })
    )

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship it')

    expect(listener).not.toBeNull()
    await new Promise((resolve) => setTimeout(resolve, 0))
    expect(harness.completion.resolve).not.toBeNull()
    const runId = extractInvokeRunId()

    if (!listener) {
      throw new Error('Tauri listener was not attached')
    }
    const emitTerminalEvent = listener as (evt: { payload: AgentEvent }) => void
    emitTerminalEvent({
      payload: {
        type: 'complete',
        runId,
        session: {
          id: 'session-terminal',
          messages: [],
          completed: true,
        },
      },
    })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-terminal',
    })

    if (releaseInvoke) {
      const resolveInvoke = releaseInvoke as (value: SubmitGoalResult) => void
      resolveInvoke({ success: true, turns: 2, sessionId: 'session-terminal' })
    }
    harness.dispose()
  })

  it('settles Tauri runs on error events before invoke returns', async () => {
    isTauriRuntime = true
    let listener: ((evt: { payload: AgentEvent }) => void) | null = null
    let rejectInvoke: ((reason?: unknown) => void) | null = null

    listenMock.mockImplementationOnce(
      async (_event: string, cb: (evt: { payload: AgentEvent }) => void) => {
        listener = cb
        return () => {}
      }
    )
    tauriInvokeMock.mockImplementationOnce(
      () =>
        new Promise<SubmitGoalResult>((_resolve, reject) => {
          rejectInvoke = reject
        })
    )

    const harness = createIpcHarness()
    const runPromise = harness.ipc.retryRun()

    if (!listener) {
      throw new Error('Tauri listener was not attached')
    }
    await new Promise((resolve) => setTimeout(resolve, 0))
    const runId = extractInvokeRunId()
    const emitTerminalEvent: (evt: { payload: AgentEvent }) => void = listener

    emitTerminalEvent({
      payload: {
        type: 'error',
        message: 'backend session missing',
        runId,
      },
    })

    await expect(runPromise).resolves.toBeNull()

    if (!rejectInvoke) {
      throw new Error('Tauri invoke promise was not captured')
    }
    const rejectCaptured: (reason?: unknown) => void = rejectInvoke
    rejectCaptured(new Error('invoke unwound after terminal error'))
    harness.dispose()
  })

  it('keeps the Tauri listener attached until terminal events arrive', async () => {
    isTauriRuntime = true
    let listener: ((evt: { payload: AgentEvent }) => void) | null = null
    const unlisten = vi.fn()

    listenMock.mockImplementationOnce(
      async (_event: string, cb: (evt: { payload: AgentEvent }) => void) => {
        listener = cb
        return unlisten
      }
    )
    tauriInvokeMock.mockImplementationOnce(() =>
      Promise.resolve({ success: true, turns: 2, sessionId: 'invoke-session' })
    )

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship it')

    await waitForInvokeCall(0)
    const runId = extractInvokeRunId()
    expect(listener).not.toBeNull()
    expect(unlisten).not.toHaveBeenCalled()

    if (!listener) {
      throw new Error('Tauri listener was not attached')
    }
    const emitEvent: (evt: { payload: AgentEvent }) => void = listener
    emitEvent({ payload: { type: 'progress', message: 'wrapping up' } })
    emitEvent({
      payload: {
        type: 'complete',
        runId,
        session: {
          id: 'session-terminal',
          messages: [],
          completed: true,
        },
      },
    })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-terminal',
    })
    expect(harness.handledEvents.map((event) => event.type)).toEqual(['complete'])
    expect(unlisten).toHaveBeenCalledTimes(1)
    harness.dispose()
  })

  it('waits for a terminal Tauri event after accepted invoke responses', async () => {
    isTauriRuntime = true
    let listener: ((evt: { payload: AgentEvent }) => void) | null = null

    listenMock.mockImplementationOnce(
      async (_event: string, cb: (evt: { payload: AgentEvent }) => void) => {
        listener = cb
        return () => {}
      }
    )
    tauriInvokeMock.mockResolvedValueOnce({
      success: true,
      turns: 3,
      sessionId: 'invoke-session',
    })

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship it')

    await Promise.resolve()
    expect(listener).not.toBeNull()

    let settled = false
    void runPromise.then(() => {
      settled = true
    })
    await flushPromises()
    expect(settled).toBe(false)

    if (!listener) {
      throw new Error('Tauri listener was not attached')
    }
    const runId = extractInvokeRunId()
    const emitEvent = listener as (evt: { payload: AgentEvent }) => void
    emitEvent({
      payload: {
        type: 'complete',
        runId,
        session: {
          id: 'session-terminal',
          messages: [],
          completed: true,
        },
      },
    })

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'session-terminal',
    })
    harness.dispose()
  })

  it('ignores late Tauri terminal events from an earlier run after a new listener attaches', async () => {
    isTauriRuntime = true
    const listeners: Array<(evt: { payload: AgentEvent }) => void> = []

    listenMock.mockImplementation(
      async (_event: string, cb: (evt: { payload: AgentEvent }) => void) => {
        listeners.push(cb)
        return () => {}
      }
    )

    const firstInvoke = createDeferred<SubmitGoalResult>()
    const secondInvoke = createDeferred<SubmitGoalResult>()
    tauriInvokeMock
      .mockImplementationOnce(() => firstInvoke.promise)
      .mockImplementationOnce(() => secondInvoke.promise)

    const harness = createIpcHarness()

    const firstRun = harness.ipc.run('first run')
    await waitForInvokeCall(0)
    const firstRunId = extractInvokeRunId(0)

    firstInvoke.resolve({ success: true, turns: 1, sessionId: 'invoke-a' })
    if (!listeners[0]) {
      throw new Error('First Tauri listener was not attached')
    }
    listeners[0]({
      payload: {
        type: 'complete',
        runId: firstRunId,
        session: {
          id: 'invoke-a',
          messages: [],
          completed: true,
        },
      },
    })

    await expect(firstRun).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'invoke-a',
    })
    harness.handledEvents.length = 0

    const secondRun = harness.ipc.run('second run')
    await waitForInvokeCall(1)
    const secondRunId = extractInvokeRunId(1)

    if (!listeners[1]) {
      throw new Error('Second Tauri listener was not attached')
    }

    let secondRunSettled = false
    void secondRun.then(() => {
      secondRunSettled = true
    })

    listeners[1]({
      payload: {
        type: 'complete',
        runId: firstRunId,
        session: {
          id: 'stale-session',
          messages: [],
          completed: true,
        },
      },
    })

    await flushPromises()
    expect(secondRunSettled).toBe(false)

    listeners[1]({
      payload: {
        type: 'complete',
        runId: secondRunId,
        session: {
          id: 'fresh-session',
          messages: [],
          completed: true,
        },
      },
    })

    await expect(secondRun).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'fresh-session',
    })

    secondInvoke.resolve({ success: true, turns: 2, sessionId: 'fresh-session' })
    harness.dispose()
  })

  it('forwards session ownership on deferred queue commands', async () => {
    const harness = createIpcHarness()
    harness.setIsRunning(true)

    await harness.ipc.followUp('queued follow-up', 'session-owned')
    await harness.ipc.postComplete('queued later', 3, 'session-owned')

    expect(apiInvokeMock).toHaveBeenNthCalledWith(1, 'follow_up_agent', {
      args: { message: 'queued follow-up', sessionId: 'session-owned' },
    })
    expect(apiInvokeMock).toHaveBeenNthCalledWith(2, 'post_complete_agent', {
      args: { message: 'queued later', group: 3, sessionId: 'session-owned' },
    })

    harness.dispose()
  })

  it('routes web cancel and steer with active run correlation', async () => {
    const socket = new FakeWebSocket()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock.mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-web' })

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship web')
    await socket.waitForListener('open')
    socket.emitOpen()
    await waitForCondition(() => Boolean(harness.currentRunId()))

    const runId = harness.currentRunId()
    expect(runId).toBeTruthy()

    await harness.ipc.steer('nudge')
    await harness.ipc.cancel()

    expect(apiInvokeMock).toHaveBeenNthCalledWith(2, 'steer_agent', {
      message: 'nudge',
      runId,
    })
    expect(apiInvokeMock).toHaveBeenNthCalledWith(3, 'cancel_agent', {
      runId,
    })

    await expect(runPromise).resolves.toEqual({
      success: false,
      turns: 0,
      sessionId: 'session-web',
    })
    harness.dispose()
  })

  it('drops late websocket events after browser cancel clears run correlation', async () => {
    const socket = new FakeWebSocket()
    const cancelDeferred = createDeferred<void>()
    createEventSocketMock.mockReturnValueOnce(socket as unknown as WebSocket)
    apiInvokeMock
      .mockResolvedValueOnce({ success: true, turns: 1, sessionId: 'session-web' })
      .mockImplementationOnce(() => cancelDeferred.promise)

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship web', { sessionId: 'session-front' })

    await socket.waitForListener('open')
    socket.emitOpen()
    await waitForCondition(() => Boolean(harness.currentRunId()))

    const runId = harness.currentRunId()
    expect(runId).toBeTruthy()

    const cancelPromise = harness.ipc.cancel()
    await waitForCondition(() => !harness.isRunning() && harness.currentRunId() === null)

    socket.emitMessage({
      type: 'progress',
      message: 'late progress after cancel',
      run_id: runId ?? undefined,
    })
    socket.emitMessage({
      type: 'complete',
      run_id: runId ?? undefined,
      session: { id: 'session-front', messages: [], completed: true },
    })

    await expect(runPromise).resolves.toEqual({
      success: false,
      turns: 0,
      sessionId: 'session-web',
    })
    expect(harness.handledEvents).toEqual([])

    cancelDeferred.resolve(undefined)
    await cancelPromise

    harness.dispose()
  })

  it('drops stale Tauri streaming events from earlier runs after a new run starts', async () => {
    isTauriRuntime = true
    const listeners: Array<(evt: { payload: AgentEvent }) => void> = []

    listenMock.mockImplementation(
      async (_event: string, cb: (evt: { payload: AgentEvent }) => void) => {
        listeners.push(cb)
        return () => {}
      }
    )

    const firstInvoke = createDeferred<SubmitGoalResult>()
    const secondInvoke = createDeferred<SubmitGoalResult>()
    tauriInvokeMock
      .mockImplementationOnce(() => firstInvoke.promise)
      .mockImplementationOnce(() => secondInvoke.promise)

    const harness = createIpcHarness()

    const firstRun = harness.ipc.run('first run')
    await waitForInvokeCall(0)
    const firstRunId = extractInvokeRunId(0)

    firstInvoke.resolve({ success: true, turns: 1, sessionId: 'invoke-a' })
    if (!listeners[0]) {
      throw new Error('First Tauri listener was not attached')
    }
    listeners[0]({
      payload: {
        type: 'complete',
        runId: firstRunId,
        session: {
          id: 'invoke-a',
          messages: [],
          completed: true,
        },
      },
    })
    await expect(firstRun).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'invoke-a',
    })
    harness.handledEvents.length = 0

    const secondRun = harness.ipc.run('second run')
    await waitForInvokeCall(1)
    const secondRunId = extractInvokeRunId(1)

    if (!listeners[1]) {
      throw new Error('Second Tauri listener was not attached')
    }

    listeners[1]({
      payload: { type: 'token', runId: firstRunId, content: 'stale token' },
    })
    listeners[1]({
      payload: { type: 'progress', runId: firstRunId, message: 'stale progress' },
    })
    listeners[1]({
      payload: {
        type: 'tool_call',
        runId: firstRunId,
        id: 'stale-call',
        name: 'read',
        args: { path: 'stale.rs' },
      },
    })
    listeners[1]({
      payload: {
        type: 'tool_result',
        runId: firstRunId,
        call_id: 'stale-call',
        content: 'stale result',
        is_error: false,
      },
    })
    listeners[1]({
      payload: { type: 'token', runId: secondRunId, content: 'fresh token' },
    })

    await flushPromises()

    expect(harness.handledEvents).toEqual([
      { type: 'token', runId: secondRunId, content: 'fresh token' },
    ])

    listeners[1]({
      payload: {
        type: 'complete',
        runId: secondRunId,
        session: {
          id: 'fresh-session',
          messages: [],
          completed: true,
        },
      },
    })

    await expect(secondRun).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'fresh-session',
    })

    secondInvoke.resolve({ success: true, turns: 2, sessionId: 'fresh-session' })
    harness.dispose()
  })

  it('threads Tauri control correlation through steer, queue, and cancel commands', async () => {
    isTauriRuntime = true
    let releaseInvoke: ((value: SubmitGoalResult) => void) | null = null

    listenMock.mockImplementationOnce(
      async (_event: string, cb: (evt: { payload: AgentEvent }) => void) => {
        void cb
        return () => {}
      }
    )
    tauriInvokeMock.mockImplementationOnce(
      () =>
        new Promise<SubmitGoalResult>((resolve) => {
          releaseInvoke = resolve
        })
    )
    tauriInvokeMock.mockResolvedValue(undefined)

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship tauri', { sessionId: 'session-front' })

    await waitForInvokeCall(0)
    const runId = extractInvokeRunId(0)

    await harness.ipc.steer('nudge')
    await harness.ipc.followUp('queued follow-up', 'session-front')
    await harness.ipc.postComplete('queued later', 2, 'session-front')
    await harness.ipc.cancel()

    expect(tauriInvokeMock).toHaveBeenNthCalledWith(2, 'steer_agent', {
      args: { message: 'nudge', runId },
    })
    expect(tauriInvokeMock).toHaveBeenNthCalledWith(3, 'follow_up_agent', {
      args: { message: 'queued follow-up', runId, sessionId: 'session-front' },
    })
    expect(tauriInvokeMock).toHaveBeenNthCalledWith(4, 'post_complete_agent', {
      args: { message: 'queued later', group: 2, runId, sessionId: 'session-front' },
    })
    expect(tauriInvokeMock).toHaveBeenNthCalledWith(5, 'cancel_agent', {
      args: { runId },
    })

    await expect(runPromise).resolves.toBeNull()

    if (!releaseInvoke) {
      throw new Error('Tauri invoke promise was not captured')
    }
    const resolveInvoke = releaseInvoke as (value: SubmitGoalResult) => void
    resolveInvoke({ success: true, turns: 1, sessionId: 'session-front' })

    harness.dispose()
  })
})

async function flushPromises(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

async function waitForInvokeCall(callIndex: number): Promise<void> {
  for (let attempt = 0; attempt < 10; attempt += 1) {
    if (tauriInvokeMock.mock.calls[callIndex]) {
      return
    }
    await flushPromises()
  }
  throw new Error(`Timed out waiting for invoke call ${callIndex}`)
}

async function waitForCondition(check: () => boolean): Promise<void> {
  for (let attempt = 0; attempt < 20; attempt += 1) {
    if (check()) {
      return
    }
    await flushPromises()
  }
  throw new Error('Timed out waiting for condition')
}
