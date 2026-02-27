/**
 * Core Bridge (v2)
 *
 * Central initialization for core-v2 + extensions.
 * Replaces old @ava/core singletons with the new extension-first stack.
 */

import { getMessageBus, type MessageBus } from '@ava/core-v2/bus'
import { getSettingsManager, type SettingsManager } from '@ava/core-v2/config'
import { createExtensionAPI } from '@ava/core-v2/extensions'
import { setPlatform } from '@ava/core-v2/platform'
import { createSessionManager, type SessionManager } from '@ava/core-v2/session'
import { registerCoreTools } from '@ava/core-v2/tools'
import { createTauriPlatform } from '@ava/platform-tauri'
import { ContextBudget } from '../lib/context-budget'
import { loadAllExtensions } from './extension-loader'
import { logInfo } from './logger'
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
  _sessionMgr = createSessionManager()
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

  _cleanup = () => {
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
