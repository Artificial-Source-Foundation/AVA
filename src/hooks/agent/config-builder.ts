/**
 * Agent Config Builder — DEPRECATED
 *
 * Agent configuration is now built in the Rust backend.
 * This module is retained only for the ConfigDeps type export.
 */

import type { CompletionNotificationSettings } from '../../services/notifications'

// ============================================================================
// Types
// ============================================================================

/** Dependencies injected from the store layer */
export interface ConfigDeps {
  currentProjectDir: () => string | undefined
  settingsRef: {
    settings: () => {
      agentLimits: { agentMaxTurns: number; agentMaxTimeMinutes: number }
      generation: {
        delegationEnabled: boolean
        reasoningEffort: string
        customInstructions?: string
      }
      behavior: { sessionAutoTitle: boolean }
      permissionMode: string
      notifications: CompletionNotificationSettings
    }
  }
}
