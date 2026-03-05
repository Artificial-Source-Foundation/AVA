/**
 * Permissions extension.
 * Registers a tool middleware for safety and permission checking.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { createPermissionMiddleware, updateSettings } from './middleware.js'
import { loadDeclarativePolicies } from './policy/index.js'
import { activate as activateRules } from './rules/index.js'
import { createSandboxMiddleware } from './sandbox-middleware.js'
import type { ToolPermissionRule } from './types.js'

function applySettings(raw: Record<string, unknown>): void {
  updateSettings({
    yolo: raw.yolo as boolean | undefined,
    autoApproveReads: raw.autoApproveReads as boolean | undefined,
    autoApproveWrites: raw.autoApproveWrites as boolean | undefined,
    autoApproveCommands: raw.autoApproveCommands as boolean | undefined,
    blockedPatterns: raw.blockedPatterns as string[] | undefined,
    trustedPaths: raw.trustedPaths as string[] | undefined,
    toolRules: raw.toolRules as ToolPermissionRule[] | undefined,
    smartApprove: raw.smartApprove as boolean | undefined,
    alwaysApproved: raw.alwaysApproved as string[] | undefined,
    permissionMode: raw.permissionMode as string | undefined,
  })
}

async function reloadPolicies(api: ExtensionAPI, cwd: string): Promise<void> {
  const loaded = await loadDeclarativePolicies(api.platform.fs, cwd)
  updateSettings({ declarativePolicyRules: loaded.rules })
  for (const warning of loaded.warnings) {
    api.log.warn(warning)
  }
}

export function activate(api: ExtensionAPI): Disposable {
  const rulesDisposable = activateRules(api)

  // Register the permission middleware (pass bus for interactive approval)
  const sandboxDisposable = api.addToolMiddleware(createSandboxMiddleware())
  const mwDisposable = api.addToolMiddleware(createPermissionMiddleware(api.bus))

  // Sync settings from the settings manager (may not exist yet)
  try {
    const settings = api.getSettings<Record<string, unknown>>('permissions')

    if (settings) {
      applySettings(settings)
    }
  } catch {
    // Settings category not registered yet — use defaults
  }

  // Listen for settings changes
  const settingsDisposable = api.onSettingsChanged('permissions', (s) => {
    applySettings(s as Record<string, unknown>)
  })

  const sessionDisposable = api.on('session:opened', (event) => {
    const payload = event as { workingDirectory?: string }
    const cwd = payload.workingDirectory ?? process.cwd()
    void reloadPolicies(api, cwd)
  })

  return {
    dispose() {
      rulesDisposable.dispose()
      sandboxDisposable.dispose()
      mwDisposable.dispose()
      settingsDisposable.dispose()
      sessionDisposable.dispose()
    },
  }
}

export { ARITY_MAP, extractCommandPrefix } from './arity.js'
export type { BashTokens } from './bash-parser.js'
export { parseBashTokens } from './bash-parser.js'
export {
  buildApprovalKey,
  createDynamicRuleStore,
  isDangerousToGeneralize,
} from './dynamic-rules.js'
export {
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
