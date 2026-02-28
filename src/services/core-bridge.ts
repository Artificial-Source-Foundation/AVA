/**
 * Core Bridge (v2)
 *
 * Central initialization for core-v2 + extensions.
 * Replaces old @ava/core singletons with the new extension-first stack.
 */

import { getMessageBus, type MessageBus } from '@ava/core-v2/bus'
import { getSettingsManager, type SettingsManager } from '@ava/core-v2/config'
import {
  createExtensionAPI,
  type ToolMiddleware,
  type ToolMiddlewareContext,
  type ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import { getPlatform, setPlatform } from '@ava/core-v2/platform'
import { createSessionManager, type SessionManager } from '@ava/core-v2/session'
import { registerCoreTools } from '@ava/core-v2/tools'
import { createTauriPlatform } from '@ava/platform-tauri'
import { ContextBudget } from '../lib/context-budget'
import { useSandbox } from '../stores/sandbox'
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

  // 7. Register sandbox middleware (intercepts file writes when sandbox mode is on)
  const sandboxMiddleware = createSandboxMiddleware()
  const sandboxApi = createExtensionAPI('sandbox-intercept', _bus!, _sessionMgr!)
  const sandboxDisposable = sandboxApi.addToolMiddleware(sandboxMiddleware)

  _cleanup = () => {
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

// ─── Sandbox Middleware ──────────────────────────────────────────────────────

/** Tools whose file writes should be intercepted in sandbox mode */
const SANDBOX_INTERCEPT_TOOLS = new Set(['write_file', 'create_file', 'edit', 'delete_file'])

/**
 * Creates a ToolMiddleware (priority 3) that intercepts file-modifying tool
 * calls when sandbox mode is enabled. Instead of writing to disk, it captures
 * the changes in the sandbox store for user review.
 */
function createSandboxMiddleware(): ToolMiddleware {
  return {
    name: 'sandbox-intercept',
    priority: 3, // Before approval (5) — sandbox captures even auto-approved writes

    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      const sandbox = useSandbox()
      if (!sandbox.sandboxEnabled()) return undefined
      if (!SANDBOX_INTERCEPT_TOOLS.has(ctx.toolName)) return undefined

      const fs = getPlatform().fs

      if (ctx.toolName === 'write_file' || ctx.toolName === 'create_file') {
        const filePath = ctx.args.path as string
        const newContent = ctx.args.content as string

        // Read original content if file exists
        let originalContent = ''
        let changeType: 'create' | 'modify' = 'create'
        try {
          originalContent = await fs.readFile(filePath)
          changeType = 'modify'
        } catch {
          // File doesn't exist — it's a create
        }

        sandbox.addPendingChange({
          filePath,
          originalContent,
          newContent,
          type: changeType,
        })

        return {
          blocked: true,
          reason: `[Sandbox] ${changeType === 'create' ? 'Created' : 'Modified'} file queued for review: ${filePath}`,
          result: {
            success: true,
            output: `[Sandbox mode] Change to ${filePath} has been captured for review. Use the sandbox review panel to apply or reject.`,
          },
        }
      }

      if (ctx.toolName === 'edit') {
        const filePath = ctx.args.filePath as string
        const oldString = ctx.args.oldString as string
        const newString = ctx.args.newString as string

        // Read current file content
        let originalContent = ''
        try {
          originalContent = await fs.readFile(filePath)
        } catch {
          return {
            blocked: true,
            reason: `[Sandbox] File not found: ${filePath}`,
            result: {
              success: false,
              output: `File not found: ${filePath}`,
              error: `Cannot edit non-existent file: ${filePath}`,
            },
          }
        }

        // Apply the edit to produce new content
        const replaceAll = (ctx.args.replaceAll as boolean) ?? false
        let newContent: string
        if (replaceAll) {
          newContent = originalContent.split(oldString).join(newString)
        } else {
          const idx = originalContent.indexOf(oldString)
          if (idx === -1) {
            return {
              blocked: true,
              reason: `[Sandbox] oldString not found in ${filePath}`,
              result: {
                success: false,
                output: `Could not find the specified text in ${filePath}`,
                error: 'oldString not found in file',
              },
            }
          }
          newContent =
            originalContent.substring(0, idx) +
            newString +
            originalContent.substring(idx + oldString.length)
        }

        sandbox.addPendingChange({
          filePath,
          originalContent,
          newContent,
          type: 'modify',
        })

        return {
          blocked: true,
          reason: `[Sandbox] Edit to ${filePath} queued for review`,
          result: {
            success: true,
            output: `[Sandbox mode] Edit to ${filePath} has been captured for review.`,
          },
        }
      }

      if (ctx.toolName === 'delete_file') {
        const filePath = ctx.args.path as string

        let originalContent = ''
        try {
          originalContent = await fs.readFile(filePath)
        } catch {
          return {
            blocked: true,
            reason: `[Sandbox] File not found: ${filePath}`,
            result: {
              success: false,
              output: `File not found: ${filePath}`,
              error: `Cannot delete non-existent file: ${filePath}`,
            },
          }
        }

        sandbox.addPendingChange({
          filePath,
          originalContent,
          newContent: '',
          type: 'delete',
        })

        return {
          blocked: true,
          reason: `[Sandbox] Deletion of ${filePath} queued for review`,
          result: {
            success: true,
            output: `[Sandbox mode] Deletion of ${filePath} has been captured for review.`,
          },
        }
      }

      return undefined
    },
  }
}
