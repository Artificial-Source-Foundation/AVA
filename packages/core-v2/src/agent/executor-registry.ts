/**
 * Executor Registry — tracks running child executors for stop/message operations.
 *
 * The delegate tool and task tool register child executors here on creation,
 * and unregister them on completion. This enables the UI to stop individual
 * agents and (future) send follow-up messages.
 */

import type { AgentExecutor } from './loop.js'

export interface RegisteredExecutor {
  executor: AgentExecutor
  abort: AbortController
  parentId: string | null
  name: string
  startedAt: number
}

const registry = new Map<string, RegisteredExecutor>()

/** Register a running executor. */
export function registerExecutor(
  id: string,
  executor: AgentExecutor,
  abort: AbortController,
  parentId: string | null = null,
  name = ''
): void {
  registry.set(id, { executor, abort, parentId, name, startedAt: Date.now() })
}

/** Unregister an executor (on completion). */
export function unregisterExecutor(id: string): void {
  registry.delete(id)
}

/** Get a registered executor by ID. */
export function getExecutor(id: string): RegisteredExecutor | undefined {
  return registry.get(id)
}

/** Abort a running executor by ID. Returns true if found and aborted. */
export function abortExecutor(id: string): boolean {
  const entry = registry.get(id)
  if (!entry) return false
  entry.abort.abort()
  return true
}

/** Get all registered executors. */
export function getAllExecutors(): Map<string, RegisteredExecutor> {
  return new Map(registry)
}

/** Clear the registry (for tests / session reset). */
export function clearExecutorRegistry(): void {
  registry.clear()
}
