/**
 * Permissions extension.
 * Registers a tool middleware for safety and permission checking.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { createPermissionMiddleware, updateSettings } from './middleware.js'
import type { ToolPermissionRule } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  // Register the permission middleware (pass bus for interactive approval)
  const mwDisposable = api.addToolMiddleware(createPermissionMiddleware(api.bus))

  // Sync settings from the settings manager (may not exist yet)
  try {
    const settings = api.getSettings<Record<string, unknown>>('permissions')

    if (settings) {
      updateSettings({
        yolo: settings.yolo as boolean | undefined,
        autoApproveReads: settings.autoApproveReads as boolean | undefined,
        autoApproveWrites: settings.autoApproveWrites as boolean | undefined,
        autoApproveCommands: settings.autoApproveCommands as boolean | undefined,
        blockedPatterns: settings.blockedPatterns as string[] | undefined,
        trustedPaths: settings.trustedPaths as string[] | undefined,
        toolRules: settings.toolRules as ToolPermissionRule[] | undefined,
        smartApprove: settings.smartApprove as boolean | undefined,
        alwaysApproved: settings.alwaysApproved as string[] | undefined,
        permissionMode: settings.permissionMode as string | undefined,
      })
    }
  } catch {
    // Settings category not registered yet — use defaults
  }

  // Listen for settings changes
  const settingsDisposable = api.onSettingsChanged('permissions', (s) => {
    const ps = s as Record<string, unknown>
    updateSettings({
      yolo: ps.yolo as boolean | undefined,
      autoApproveReads: ps.autoApproveReads as boolean | undefined,
      autoApproveWrites: ps.autoApproveWrites as boolean | undefined,
      autoApproveCommands: ps.autoApproveCommands as boolean | undefined,
      blockedPatterns: ps.blockedPatterns as string[] | undefined,
      trustedPaths: ps.trustedPaths as string[] | undefined,
      toolRules: ps.toolRules as ToolPermissionRule[] | undefined,
      smartApprove: ps.smartApprove as boolean | undefined,
      alwaysApproved: ps.alwaysApproved as string[] | undefined,
      permissionMode: ps.permissionMode as string | undefined,
    })
  })

  return {
    dispose() {
      mwDisposable.dispose()
      settingsDisposable.dispose()
    },
  }
}

export { ARITY_MAP, extractCommandPrefix } from './arity.js'
export type { BashTokens } from './bash-parser.js'
export { parseBashTokens } from './bash-parser.js'
export {
  buildApprovalKey,
  createPermissionMiddleware,
  evaluateToolRules,
  getSettings,
  isInTrustedPath,
  isSafeBashCommand,
  matchesAnyGlob,
  matchesGlob,
  resetSettings,
  updateSettings,
} from './middleware.js'
export type { PermissionMode, PermissionModeConfig } from './modes.js'
export {
  getAllPermissionModes,
  getPermissionMode,
  isToolAutoApproved,
  PERMISSION_MODES,
} from './modes.js'
export type {
  PermissionRequest,
  PermissionResponse,
  PermissionSettings,
  PolicyRule,
  RiskLevel,
  ToolPermissionRule,
} from './types.js'
export { BUILTIN_RULES, classifyRisk, DEFAULT_SETTINGS, SAFE_BASH_PATTERNS } from './types.js'
