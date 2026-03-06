/**
 * Core Bridge (v2)
 *
 * Central initialization for core-v2 + extensions.
 * Replaces old @ava/core singletons with the new extension-first stack.
 */

import { getMessageBus, type MessageBus } from '@ava/core-v2/bus'
import { getSettingsManager, type SettingsManager } from '@ava/core-v2/config'
import { createExtensionAPI, emitEvent, onEvent } from '@ava/core-v2/extensions'
import { setPlatform } from '@ava/core-v2/platform'
import { createSessionManager, type SessionManager } from '@ava/core-v2/session'
import { registerCoreTools } from '@ava/core-v2/tools'
import { createTauriPlatform } from '@ava/platform-tauri'
import { ContextBudget } from '../lib/context-budget'
import { updateSession as dbUpdateSession } from './database'
import { DesktopSessionStorage } from './desktop-session-storage'
import { loadAllExtensions } from './extension-loader'
import { logInfo } from './logger'
import { createSandboxMiddleware } from './sandbox-middleware'
import { startSettingsSync } from './settings-sync'
import { createApprovalMiddleware, setAutoApprovalChecker } from './tool-approval-bridge'

// ─── Singleton State ────────────────────────────────────────────────────────

let _settings: SettingsManager | null = null
let _bus: MessageBus | null = null
let _sessionMgr: SessionManager | null = null
let _budget: ContextBudget | null = null
let _cleanup: (() => void) | null = null

// ─── Getters ────────────────────────────────────────────────────────────────

export function getCoreSettings(): SettingsManager | null {
  return _settings
}

export function getCoreBus(): MessageBus | null {
  return _bus
}

export function getCoreSessionManager(): SessionManager | null {
  return _sessionMgr
}

export function getCoreBudget(): ContextBudget | null {
  return _budget
}

// ─── Initialization ─────────────────────────────────────────────────────────

export interface CoreBridgeOptions {
  contextLimit?: number
  autoApprovalChecker?: (name: string) => boolean
}

/**
 * Initialize core-v2 platform, extensions, and approval middleware.
 * Returns a cleanup function.
 */
export async function initCoreBridge(opts: CoreBridgeOptions = {}): Promise<() => void> {
  logInfo('core', 'Init core bridge (v2)', {
    contextLimit: opts.contextLimit ?? 200_000,
  })

  // 1. Platform — Tauri implementations for fs, shell, credentials, database
  setPlatform(createTauriPlatform('ava.db'))

  // 2. Singletons
  _settings = getSettingsManager()
  _bus = getMessageBus()
  _sessionMgr = createSessionManager({ storage: new DesktopSessionStorage() })
  _budget = new ContextBudget(opts.contextLimit ?? 200_000)

  // 3. Register core tools (read, write, edit, bash, glob, grep)
  registerCoreTools()

  // 4. Set up auto-approval checker for the middleware
  if (opts.autoApprovalChecker) {
    setAutoApprovalChecker(opts.autoApprovalChecker)
  }

  // 5. Load and activate all built-in extensions
  const disposeExtensions = await loadAllExtensions((name) =>
    createExtensionAPI(name, _bus!, _sessionMgr!)
  )

  // 6. Register the desktop approval middleware (after extensions, so it can
  //    run alongside the permissions middleware at priority 0)
  const approvalMiddleware = createApprovalMiddleware()
  const api = createExtensionAPI('desktop-approval', _bus!, _sessionMgr!)
  const approvalDisposable = api.addToolMiddleware(approvalMiddleware)

  // 7. Register sandbox middleware (intercepts file writes when sandbox mode is on)
  const sandboxMiddleware = createSandboxMiddleware()
  const sandboxApi = createExtensionAPI('sandbox-intercept', _bus!, _sessionMgr!)
  const sandboxDisposable = sandboxApi.addToolMiddleware(sandboxMiddleware)

  // 8. Start bidirectional settings sync (core → frontend events)
  const disposeSettingsSync = startSettingsSync()

  // 9. Sync context budget from agent events
  const contextSubs = [
    onEvent('context:compacting', (data) => {
      const { estimatedTokens } = data as { estimatedTokens: number }
      if (_budget) _budget.setUsed(estimatedTokens)
    }),
    onEvent('agent:finish', () => {
      // Trigger reactive re-read of budget stats
      if (_budget) {
        window.dispatchEvent(new CustomEvent('ava:budget-updated'))
      }
    }),
  ]

  // 10. Bridge session busy/idle status to desktop DB and dispatch frontend event
  const sessionStatusSub = onEvent('session:status', (data) => {
    const { sessionId, status } = data as { sessionId: string; status: string }
    if (status === 'busy') {
      void dbUpdateSession(sessionId, { busySince: Date.now() })
    } else if (status === 'idle') {
      void dbUpdateSession(sessionId, { busySince: null })
    }
    // Dispatch DOM event so frontend session store can update busySessionIds
    if (typeof window !== 'undefined') {
      window.dispatchEvent(new CustomEvent('ava:session-status', { detail: { sessionId, status } }))
    }
  })

  // 11. Wire instructions:loaded → prompt builder (same as CLI)
  const instructionsSub = onEvent('instructions:loaded', (data) => {
    const { merged, count } = data as { merged: string; count: number }
    if (merged) {
      void import('../../packages/extensions/prompts/src/builder.js').then((pm) => {
        pm.addPromptSection({
          name: 'project-instructions',
          content: `# Project Instructions\n\n${merged}`,
          priority: 5,
        })
        logInfo('core', `Loaded ${count} project instruction file(s)`)
      })
    }
  })

  _cleanup = () => {
    instructionsSub.dispose()
    sessionStatusSub.dispose()
    for (const sub of contextSubs) sub.dispose()
    disposeSettingsSync()
    sandboxDisposable.dispose()
    approvalDisposable.dispose()
    disposeExtensions()
    _bus = null
    _sessionMgr = null
    _budget = null
    _settings = null
    logInfo('core', 'Core bridge disposed')
  }

  return _cleanup
}

// ─── Session Lifecycle ──────────────────────────────────────────────────

/**
 * Notify core-v2 extensions that a session is open.
 * Triggers instructions loading (CLAUDE.md), codebase scanning, skills, etc.
 * Must be called when creating or switching sessions — mirrors what the CLI does.
 */
export function notifySessionOpened(sessionId: string, workingDirectory: string): void {
  emitEvent('session:opened', { sessionId, workingDirectory })
}
