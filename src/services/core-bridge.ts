/**
 * Core Bridge — Rust Backend
 * The Rust AgentStack handles all orchestration. This module provides
 * minimal initialization for frontend-only concerns.
 */

import { isTauri } from '@tauri-apps/api/core'
import { ContextBudget } from '../lib/context-budget'
import { log } from '../lib/logger'
import type { ActiveSessionSyncResult, ActiveSessionSyncSnapshot } from '../types/rust-ipc'
import { rustBackend } from './rust-bridge'

let _budget: ContextBudget | null = null
let _cleanup: (() => void) | null = null
let _activeSessionSyncPromise: Promise<ActiveSessionSyncResult> | null = null
let _activeSessionSyncSessionId: string | null = null
let _activeSessionSyncWorkingDirectory: string | null = null
let _activeSessionSyncSnapshot: ActiveSessionSyncSnapshot | null = null
let _activeSessionSyncError: BackendSessionSyncError | null = null
let _activeSessionSyncRequestToken = 0
let _activeSessionRepairToken: number | null = null
const _sessionsNeedingAuthoritativeRecovery = new Set<string>()

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

export class BackendSessionSyncError extends Error {
  readonly code: 'missing-session' | 'sync-failed'
  readonly sessionId: string

  constructor(code: 'missing-session' | 'sync-failed', sessionId: string, message: string) {
    super(message)
    this.name = 'BackendSessionSyncError'
    this.code = code
    this.sessionId = sessionId
  }
}

function classifySessionSyncError(sessionId: string, error: unknown): BackendSessionSyncError {
  const message = error instanceof Error ? error.message : String(error)
  if (message.toLowerCase().includes('not found')) {
    return new BackendSessionSyncError(
      'missing-session',
      sessionId,
      `The backend session for ${sessionId} is unavailable. Open the session again or start a new run before retrying.`
    )
  }
  return new BackendSessionSyncError(
    'sync-failed',
    sessionId,
    `Failed to sync desktop session ${sessionId} with the backend: ${message}`
  )
}

async function syncActiveSession(
  sessionId: string,
  workingDirectory: string | undefined,
  snapshot: ActiveSessionSyncSnapshot | null
): Promise<ActiveSessionSyncResult> {
  if (snapshot) {
    return workingDirectory !== undefined
      ? await rustBackend.setActiveSession(sessionId, workingDirectory, snapshot)
      : await rustBackend.setActiveSession(sessionId, undefined, snapshot)
  }

  return workingDirectory !== undefined
    ? await rustBackend.setActiveSession(sessionId, workingDirectory)
    : await rustBackend.setActiveSession(sessionId)
}

function isCurrentSessionSync(requestToken: number, sessionId: string): boolean {
  return (
    _activeSessionSyncRequestToken === requestToken && _activeSessionSyncSessionId === sessionId
  )
}

function recordSessionSyncResult(
  sessionId: string,
  result: ActiveSessionSyncResult
): ActiveSessionSyncResult {
  if (!result.exists) {
    _activeSessionSyncError = new BackendSessionSyncError(
      'missing-session',
      sessionId,
      `The backend session for ${sessionId} is unavailable. Open the session again or start a new run before retrying.`
    )
    log.warn('session', 'Desktop session sync did not bind to a backend session', {
      sessionId,
      code: _activeSessionSyncError.code,
      error: _activeSessionSyncError.message,
    })
  } else {
    _activeSessionSyncError = null
  }
  return result
}

function recordSessionSyncFailure(sessionId: string, error: unknown): ActiveSessionSyncResult {
  const classified =
    error instanceof BackendSessionSyncError ? error : classifySessionSyncError(sessionId, error)
  _activeSessionSyncError = classified
  log.warn('session', 'Desktop session sync did not bind to a backend session', {
    sessionId,
    code: classified.code,
    error: classified.message,
  })
  return {
    sessionId,
    exists: false,
    messageCount: 0,
  }
}

function scheduleLatestSessionRepair(staleSessionId: string, staleRequestToken: number): void {
  const latestSessionId = _activeSessionSyncSessionId
  const latestRequestToken = _activeSessionSyncRequestToken
  if (
    !latestSessionId ||
    latestSessionId === staleSessionId ||
    latestRequestToken <= staleRequestToken
  ) {
    return
  }
  if (_activeSessionRepairToken === latestRequestToken) {
    return
  }

  _activeSessionRepairToken = latestRequestToken
  log.warn('session', 'Re-applying newer desktop session after stale sync completion', {
    staleSessionId,
    latestSessionId,
  })

  _activeSessionSyncPromise = syncActiveSession(
    latestSessionId,
    _activeSessionSyncWorkingDirectory ?? undefined,
    _activeSessionSyncSnapshot
  )
    .then((result) => {
      if (!isCurrentSessionSync(latestRequestToken, latestSessionId)) {
        scheduleLatestSessionRepair(latestSessionId, latestRequestToken)
        return result
      }
      return recordSessionSyncResult(latestSessionId, result)
    })
    .catch((error) => {
      if (!isCurrentSessionSync(latestRequestToken, latestSessionId)) {
        scheduleLatestSessionRepair(latestSessionId, latestRequestToken)
        return {
          sessionId: latestSessionId,
          exists: false,
          messageCount: 0,
        }
      }
      return recordSessionSyncFailure(latestSessionId, error)
    })
    .finally(() => {
      if (_activeSessionRepairToken === latestRequestToken) {
        _activeSessionRepairToken = null
      }
    })
}

function startActiveSessionSync(
  sessionId: string,
  requestToken: number,
  workingDirectory: string | undefined,
  snapshot: ActiveSessionSyncSnapshot | null
): Promise<ActiveSessionSyncResult> {
  const syncPromise = syncActiveSession(sessionId, workingDirectory, snapshot)
    .then((result) => {
      if (!isCurrentSessionSync(requestToken, sessionId)) {
        scheduleLatestSessionRepair(sessionId, requestToken)
        return result
      }
      return recordSessionSyncResult(sessionId, result)
    })
    .catch((error) => {
      if (!isCurrentSessionSync(requestToken, sessionId)) {
        scheduleLatestSessionRepair(sessionId, requestToken)
        return {
          sessionId,
          exists: false,
          messageCount: 0,
        }
      }
      return recordSessionSyncFailure(sessionId, error)
    })

  _activeSessionSyncPromise = syncPromise
  return syncPromise
}

export async function initCoreBridge(opts: CoreBridgeOptions = {}): Promise<() => void> {
  _budget = new ContextBudget(opts.contextLimit ?? 200_000)

  _cleanup = () => {
    _budget = null
    _activeSessionSyncPromise = null
    _activeSessionSyncSessionId = null
    _activeSessionSyncWorkingDirectory = null
    _activeSessionSyncSnapshot = null
    _activeSessionSyncError = null
    _activeSessionSyncRequestToken = 0
    _activeSessionRepairToken = null
    _sessionsNeedingAuthoritativeRecovery.clear()
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

export function notifySessionOpened(
  sessionId: string,
  workingDirectory?: string,
  snapshot?: ActiveSessionSyncSnapshot
): Promise<ActiveSessionSyncResult> {
  if (!sessionId || !isTauri()) {
    return Promise.resolve({ sessionId, exists: true, messageCount: 0 })
  }

  const effectiveSnapshot =
    snapshot ?? (_activeSessionSyncSessionId === sessionId ? _activeSessionSyncSnapshot : null)
  const requestToken = ++_activeSessionSyncRequestToken
  _activeSessionSyncSessionId = sessionId
  _activeSessionSyncWorkingDirectory = workingDirectory ?? null
  _activeSessionSyncSnapshot = effectiveSnapshot
  _activeSessionSyncError = null
  return startActiveSessionSync(sessionId, requestToken, workingDirectory, effectiveSnapshot)
}

export async function ensureActiveSessionSynced(
  sessionId: string
): Promise<ActiveSessionSyncResult> {
  if (!sessionId || !isTauri()) {
    return { sessionId, exists: true, messageCount: 0 }
  }

  const canReuseCachedSync =
    _activeSessionSyncSessionId === sessionId &&
    _activeSessionSyncPromise !== null &&
    !_activeSessionSyncError

  const result: ActiveSessionSyncResult =
    canReuseCachedSync && _activeSessionSyncPromise
      ? await _activeSessionSyncPromise
      : await notifySessionOpened(
          sessionId,
          _activeSessionSyncSessionId === sessionId
            ? (_activeSessionSyncWorkingDirectory ?? undefined)
            : undefined
        )

  if (_activeSessionSyncSessionId === sessionId && _activeSessionSyncError) {
    throw _activeSessionSyncError
  }

  if (!result.exists) {
    throw new BackendSessionSyncError(
      'missing-session',
      sessionId,
      `The backend session for ${sessionId} is unavailable. Open the session again or start a new run before retrying.`
    )
  }

  return result
}

export function markActiveSessionSynced(sessionId: string, messageCount = 0): void {
  if (!sessionId) return
  _activeSessionSyncRequestToken += 1
  _activeSessionSyncSessionId = sessionId
  _activeSessionSyncWorkingDirectory = null
  _activeSessionSyncSnapshot = null
  _activeSessionSyncError = null
  _activeSessionRepairToken = null
  _activeSessionSyncPromise = Promise.resolve({
    sessionId,
    exists: true,
    messageCount,
  })
}

export function markSessionNeedsAuthoritativeRecovery(sessionId: string): void {
  if (!sessionId) return
  _sessionsNeedingAuthoritativeRecovery.add(sessionId)
}

export function sessionNeedsAuthoritativeRecovery(sessionId: string): boolean {
  if (!sessionId) return false
  return _sessionsNeedingAuthoritativeRecovery.has(sessionId)
}

export function clearSessionNeedsAuthoritativeRecovery(sessionId: string): void {
  if (!sessionId) return
  _sessionsNeedingAuthoritativeRecovery.delete(sessionId)
}
