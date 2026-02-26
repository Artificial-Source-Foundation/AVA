/**
 * Pure pub/sub message bus.
 *
 * No policy engine, no tool confirmation — those are extension concerns.
 * Extensions subscribe to events and intercept via middleware.
 */

import type { BusMessage, MessageHandler, Unsubscribe } from './types.js'

export class MessageBus {
  private listeners = new Map<string, Set<MessageHandler>>()
  private correlationHandlers = new Map<string, MessageHandler>()

  /** Subscribe to all messages of a given type. */
  subscribe<T extends BusMessage>(type: string, handler: MessageHandler<T>): Unsubscribe {
    let handlers = this.listeners.get(type)
    if (!handlers) {
      handlers = new Set()
      this.listeners.set(type, handlers)
    }
    const wrapped = handler as MessageHandler
    handlers.add(wrapped)
    return () => {
      handlers!.delete(wrapped)
      if (handlers!.size === 0) this.listeners.delete(type)
    }
  }

  /** Publish a message to all subscribers of its type. */
  publish(message: BusMessage): void {
    const handlers = this.listeners.get(message.type)
    if (handlers) {
      for (const handler of handlers) {
        handler(message)
      }
    }
    // Resolve correlation-based request/response
    if (message.correlationId) {
      const waiting = this.correlationHandlers.get(message.correlationId)
      if (waiting) {
        this.correlationHandlers.delete(message.correlationId)
        waiting(message)
      }
    }
  }

  /** Send a request and wait for a correlated response. */
  async request<TReq extends BusMessage, TRes extends BusMessage>(
    request: Omit<TReq, 'correlationId' | 'timestamp'>,
    responseType: string,
    timeoutMs = 60_000
  ): Promise<TRes> {
    const correlationId = crypto.randomUUID()
    const message = {
      ...request,
      correlationId,
      timestamp: Date.now(),
    } as TReq

    return new Promise<TRes>((resolve, reject) => {
      const timer = setTimeout(() => {
        unsub()
        reject(new Error(`Bus request timed out after ${timeoutMs}ms for ${responseType}`))
      }, timeoutMs)

      // Subscribe to response type and resolve when correlationId matches
      const unsub = this.subscribe(responseType, (msg) => {
        if (msg.correlationId === correlationId) {
          unsub()
          clearTimeout(timer)
          resolve(msg as unknown as TRes)
        }
      })

      this.publish(message)
    })
  }

  hasSubscribers(type: string): boolean {
    const handlers = this.listeners.get(type)
    return handlers !== undefined && handlers.size > 0
  }

  clear(): void {
    this.listeners.clear()
    this.correlationHandlers.clear()
  }
}

// ─── Singleton ───────────────────────────────────────────────────────────────

let _bus: MessageBus | null = null

export function getMessageBus(): MessageBus {
  if (!_bus) {
    _bus = new MessageBus()
  }
  return _bus
}

export function setMessageBus(bus: MessageBus): void {
  _bus = bus
}

export function resetMessageBus(): void {
  _bus?.clear()
  _bus = null
}
