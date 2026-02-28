/**
 * Permissions extension.
 * Registers a tool middleware for safety and permission checking.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { createPermissionMiddleware, updateSettings } from './middleware.js'

export function activate(api: ExtensionAPI): Disposable {
  // Register the permission middleware (pass bus for interactive approval)
  const mwDisposable = api.addToolMiddleware(createPermissionMiddleware(api.bus))

  // Sync settings from the settings manager (may not exist yet)
  try {
    const settings = api.getSettings<{
      yolo?: boolean
      autoApproveReads?: boolean
      blockedPatterns?: string[]
    }>('permissions')

    if (settings) {
      updateSettings({
        yolo: settings.yolo,
        autoApproveReads: settings.autoApproveReads,
        blockedPatterns: settings.blockedPatterns,
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
      blockedPatterns: ps.blockedPatterns as string[] | undefined,
    })
  })

  return {
    dispose() {
      mwDisposable.dispose()
      settingsDisposable.dispose()
    },
  }
}

export {
  createPermissionMiddleware,
  getSettings,
  resetSettings,
  updateSettings,
} from './middleware.js'
export type {
  PermissionRequest,
  PermissionResponse,
  PermissionSettings,
  PolicyRule,
  RiskLevel,
} from './types.js'
export { BUILTIN_RULES, classifyRisk, DEFAULT_SETTINGS } from './types.js'
