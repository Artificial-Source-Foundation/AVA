import { createRoot, createSignal } from 'solid-js'
import { describe, expect, it, vi } from 'vitest'
import type { ToolCall } from '../types'
import type { AgentEvent, SubmitGoalResult } from '../types/rust-ipc'
import { createAgentIpc } from './rust-agent-ipc'

vi.mock('@tauri-apps/api/core', () => ({
  isTauri: () => false,
  invoke: vi.fn(),
}))

vi.mock('@tauri-apps/api/event', () => ({
  listen: vi.fn(),
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

function createIpcHarness() {
  return createRoot((dispose) => {
    const [isRunning, setIsRunning] = createSignal(false)
    const [error, setError] = createSignal<string | null>(null)
    const [lastResult, setLastResult] = createSignal<SubmitGoalResult | null>(null)
    const [activeToolCalls, setActiveToolCalls] = createSignal<ToolCall[]>([])
    const handledEvents: AgentEvent[] = []

    const ipc = createAgentIpc({
      metrics: {
        chunkCount: 0,
        totalTextLen: 0,
        runStartTime: 0,
        firstTokenLogged: false,
        pendingToolNames: [],
      },
      completion: { resolve: null },
      isRunning,
      setIsRunning,
      setError,
      setLastResult,
      setActiveToolCalls,
      handleAgentEvent: (event) => handledEvents.push(event),
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
    }
  })
}

describe('createAgentIpc', () => {
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
})
