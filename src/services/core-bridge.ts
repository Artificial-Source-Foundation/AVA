/**
 * Core Bridge — Rust Backend
 * The Rust AgentStack handles all orchestration. This module provides
 * minimal initialization for frontend-only concerns.
 */

import { ContextBudget } from '../lib/context-budget'

let _budget: ContextBudget | null = null
let _cleanup: (() => void) | null = null

export function getCoreBudget(): ContextBudget | null {
  return _budget
}

// Stubs for code that still references these
export function getCoreSettings(): null {
  return null
}
export function getCoreBus(): null {
  return null
}
export function getCoreSessionManager(): null {
  return null
}

export interface CoreBridgeOptions {
  contextLimit?: number
}

export async function initCoreBridge(opts: CoreBridgeOptions = {}): Promise<() => void> {
  _budget = new ContextBudget(opts.contextLimit ?? 200_000)

  _cleanup = () => {
    _budget = null
  }
  return _cleanup
}

export function notifySessionOpened(_sessionId: string, _workingDirectory: string): void {
  // No-op — Rust backend handles session lifecycle
}
