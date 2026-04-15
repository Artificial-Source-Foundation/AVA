import { createRoot, createSignal } from 'solid-js'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import type { ToolCall } from '../types'
import type { AgentEvent, SubmitGoalResult } from '../types/rust-ipc'
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

  addEventListener(type: string, listener: () => void): void {
    const listeners = this.listeners.get(type) ?? new Set<() => void>()
    listeners.add(listener)
    this.listeners.set(type, listeners)
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
    const [activeToolCalls, setActiveToolCalls] = createSignal<ToolCall[]>([])
    const handledEvents: AgentEvent[] = []
    const completion = { resolve: null as ((result: SubmitGoalResult | null) => void) | null }

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
      resetState: vi.fn(),
    })

    return {
      dispose,
      ipc,
      isRunning,
      setIsRunning,
      error,
      lastResult,
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

    const harness = createIpcHarness()
    const attachFirst = harness.ipc.attachListener()
    first.emitOpen()
    await attachFirst

    first.readyState = FakeWebSocket.CLOSED
    const attachSecond = harness.ipc.attachListener()
    second.emitOpen()
    await attachSecond

    first.emitMessage({ type: 'progress', message: 'stale' })
    second.emitMessage({ type: 'progress', message: 'fresh' })

    expect(harness.handledEvents).toHaveLength(1)
    expect(harness.handledEvents[0]).toMatchObject({ type: 'progress', message: 'fresh' })

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
    })

    await waitForInvokeCall(0)
    expect(tauriInvokeMock).toHaveBeenCalledWith('submit_goal', {
      args: expect.objectContaining({
        goal: 'ship it',
        provider: 'openai',
        model: 'gpt-5.4',
        sessionId: 'session-front',
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

  it('bounds EX-002 by settling Tauri runs on terminal events before invoke returns', async () => {
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

  it('keeps the Tauri listener alive briefly when invoke resolves before the terminal event', async () => {
    vi.useFakeTimers()
    isTauriRuntime = true
    let listener: ((evt: { payload: AgentEvent }) => void) | null = null
    const unlisten = vi.fn()

    listenMock.mockImplementationOnce(
      async (_event: string, cb: (evt: { payload: AgentEvent }) => void) => {
        listener = cb
        return unlisten
      }
    )
    tauriInvokeMock.mockImplementationOnce(
      () =>
        new Promise<SubmitGoalResult>((resolve) => {
          setTimeout(() => {
            resolve({
              success: true,
              turns: 2,
              sessionId: 'invoke-session',
            })
          }, 0)
        })
    )

    const harness = createIpcHarness()
    const runPromise = harness.ipc.run('ship it')

    await Promise.resolve()
    await vi.advanceTimersByTimeAsync(0)
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
    expect(harness.handledEvents.map((event) => event.type)).toEqual(['progress', 'complete'])
    expect(unlisten).toHaveBeenCalledTimes(1)
    harness.dispose()
  })

  it('falls back after a bounded Tauri grace period when no terminal event arrives', async () => {
    vi.useFakeTimers()
    isTauriRuntime = true
    let listener: ((evt: { payload: AgentEvent }) => void) | null = null
    const unlisten = vi.fn()

    listenMock.mockImplementationOnce(
      async (_event: string, cb: (evt: { payload: AgentEvent }) => void) => {
        listener = cb
        return unlisten
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
    expect(unlisten).not.toHaveBeenCalled()

    await vi.advanceTimersByTimeAsync(250)

    await expect(runPromise).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'invoke-session',
    })
    expect(harness.lastResult()).toEqual({
      success: true,
      turns: 0,
      sessionId: 'invoke-session',
    })
    expect(harness.handledEvents).toHaveLength(0)
    expect(unlisten).toHaveBeenCalledTimes(1)
    harness.dispose()
  })

  it('ignores late Tauri terminal events from an earlier run after a new listener attaches', async () => {
    vi.useFakeTimers()
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
    await vi.advanceTimersByTimeAsync(250)

    await expect(firstRun).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'invoke-a',
    })

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

  it('drops stale Tauri streaming events from earlier runs after a new run starts', async () => {
    vi.useFakeTimers()
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
    await vi.advanceTimersByTimeAsync(250)
    await expect(firstRun).resolves.toEqual({
      success: true,
      turns: 0,
      sessionId: 'invoke-a',
    })

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
