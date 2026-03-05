import * as tauriCore from '@tauri-apps/api/core'
import * as tauriEvent from '@tauri-apps/api/event'
import { vi } from 'vitest'

type InvokeArgs = unknown
type Handler = (args: InvokeArgs) => unknown | Promise<unknown>
type EventHandler = (event: { payload: unknown }) => void

export interface InvokeCall {
  command: string
  args: unknown
}

export class MockIpc {
  private handlers = new Map<string, Handler>()
  private responses = new Map<string, unknown>()
  private listeners = new Map<string, Set<EventHandler>>()
  private invokeCalls: InvokeCall[] = []

  install(): void {
    vi.spyOn(tauriCore, 'invoke').mockImplementation(async (command, args) => {
      const key = String(command)
      this.invokeCalls.push({ command: key, args })
      const handler = this.handlers.get(key)
      if (handler) return handler(args)
      if (this.responses.has(key)) return this.responses.get(key)
      throw new Error(`No mock response for command: ${key}`)
    })

    vi.spyOn(tauriEvent, 'listen').mockImplementation(async (eventName, handler) => {
      const name = String(eventName)
      const listeners = this.listeners.get(name) ?? new Set<EventHandler>()
      const typedHandler = handler as EventHandler
      listeners.add(typedHandler)
      this.listeners.set(name, listeners)
      return () => {
        const current = this.listeners.get(name)
        current?.delete(typedHandler)
      }
    })
  }

  reset(): void {
    this.handlers.clear()
    this.responses.clear()
    this.listeners.clear()
    this.invokeCalls = []
    vi.restoreAllMocks()
  }

  setResponse(command: string, response: unknown): void {
    this.responses.set(command, response)
  }

  setHandler(command: string, handler: Handler): void {
    this.handlers.set(command, handler)
  }

  getCalls(command?: string): InvokeCall[] {
    if (!command) return this.invokeCalls
    return this.invokeCalls.filter((call) => call.command === command)
  }

  emit(eventName: string, payload: unknown): void {
    const handlers = this.listeners.get(eventName)
    handlers?.forEach((handler) => {
      handler({ payload })
    })
  }
}
