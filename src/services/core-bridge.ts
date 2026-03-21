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

/**
 * Update the context budget's limit to match the selected model's context window.
 * Called whenever the active model changes so the status bar percentage is accurate.
 */
export function updateCoreBudgetLimit(contextWindow: number): void {
  if (_budget && contextWindow > 0) {
    _budget.setLimit(contextWindow)
    // Trigger reactive re-compute in session-state contextUsage memo
    window.dispatchEvent(
      new CustomEvent('ava:core-settings-changed', { detail: { category: 'context' } })
    )
  }
}

export function notifySessionOpened(_sessionId: string, _workingDirectory: string): void {
  // No-op — Rust backend handles session lifecycle
}
