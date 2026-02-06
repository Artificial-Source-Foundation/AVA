/**
 * Settings Integration
 *
 * Connects settings manager to other modules (agent, context, etc.)
 * Provides reactive settings application and synchronization.
 */

import type { AgentConfig } from '../agent/types.js'
import type { SessionManagerConfig } from '../session/types.js'

/** Placeholder for ContextManager until context module has a manager */
type ContextManager = Record<string, never>

import { getSettingsManager, type SettingsManager } from './manager.js'
import type { AgentSettings, ContextSettings, ProviderSettings, Settings } from './types.js'

// ============================================================================
// Agent Integration
// ============================================================================

/**
 * Create AgentConfig from current settings
 */
export function createAgentConfigFromSettings(
  settings?: Settings,
  overrides?: Partial<AgentConfig>
): AgentConfig {
  const manager = settings ? null : getSettingsManager()
  const providerSettings = settings?.provider ?? manager?.provider
  const agentSettings = settings?.agent ?? manager?.agent

  if (!providerSettings || !agentSettings) {
    throw new Error('Settings not available')
  }

  return {
    maxTimeMinutes: agentSettings.maxTimeMinutes,
    maxTurns: agentSettings.maxTurns,
    maxRetries: agentSettings.maxRetries,
    gracePeriodMs: agentSettings.gracePeriodMs,
    provider: providerSettings.defaultProvider as AgentConfig['provider'],
    model: providerSettings.defaultModel,
    ...overrides,
  }
}

/**
 * Apply settings to an existing AgentConfig
 */
export function applySettingsToAgentConfig(config: AgentConfig, settings: Settings): AgentConfig {
  return {
    ...config,
    maxTimeMinutes: settings.agent.maxTimeMinutes,
    maxTurns: settings.agent.maxTurns,
    maxRetries: settings.agent.maxRetries,
    gracePeriodMs: settings.agent.gracePeriodMs,
    provider: settings.provider.defaultProvider as AgentConfig['provider'],
    model: settings.provider.defaultModel,
  }
}

/**
 * Watch settings changes and update agent config
 * Returns cleanup function
 */
export function watchAgentSettings(
  manager: SettingsManager,
  onUpdate: (config: Partial<AgentConfig>) => void
): () => void {
  return manager.on((event) => {
    if (event.type === 'category_changed') {
      if (event.category === 'agent') {
        const agent = manager.agent
        onUpdate({
          maxTimeMinutes: agent.maxTimeMinutes,
          maxTurns: agent.maxTurns,
          maxRetries: agent.maxRetries,
          gracePeriodMs: agent.gracePeriodMs,
        })
      } else if (event.category === 'provider') {
        const provider = manager.provider
        onUpdate({
          provider: provider.defaultProvider as AgentConfig['provider'],
          model: provider.defaultModel,
        })
      }
    }
  })
}

// ============================================================================
// Context Integration
// ============================================================================

/**
 * Create ContextManager options from settings
 */
export function createContextOptionsFromSettings(settings: ContextSettings): {
  maxTokens: number
  compactionThreshold: number
} {
  return {
    maxTokens: settings.maxTokens,
    compactionThreshold: settings.compactionThreshold / 100, // Convert percent to decimal
  }
}

/**
 * Apply settings to context manager
 */
export function applySettingsToContext(_context: ContextManager, settings: ContextSettings): void {
  // Context manager may not have a direct setLimit method
  // This is a placeholder for the actual implementation
  // The context manager would need to expose methods to update these
  console.log('Applying context settings:', {
    maxTokens: settings.maxTokens,
    threshold: settings.compactionThreshold,
  })
}

/**
 * Watch settings changes and update context
 * Returns cleanup function
 */
export function watchContextSettings(
  manager: SettingsManager,
  context: ContextManager
): () => void {
  return manager.on((event) => {
    if (event.type === 'category_changed' && event.category === 'context') {
      applySettingsToContext(context, manager.context)
    }
  })
}

// ============================================================================
// Session Integration
// ============================================================================

/**
 * Create SessionManagerConfig from settings
 */
export function createSessionConfigFromSettings(settings: ContextSettings): SessionManagerConfig {
  return {
    maxSessions: settings.maxSessions,
    autoSaveInterval: settings.autoSave ? settings.autoSaveInterval : 0,
    compressCheckpoints: true,
  }
}

// ============================================================================
// Provider Integration
// ============================================================================

/**
 * Get LLM client options from settings
 */
export function getLLMClientOptions(settings: ProviderSettings): {
  provider: string
  model: string
  timeout: number
} {
  return {
    provider: settings.defaultProvider,
    model: settings.defaultModel,
    timeout: settings.timeout,
  }
}

/**
 * Get timeout for API requests
 */
export function getRequestTimeout(settings?: ProviderSettings): number {
  const manager = settings ? null : getSettingsManager()
  return settings?.timeout ?? manager?.provider.timeout ?? 120000
}

// ============================================================================
// Validator Integration
// ============================================================================

/**
 * Get enabled validators from settings
 */
export function getEnabledValidators(settings: AgentSettings): string[] {
  if (!settings.validatorsEnabled) {
    return []
  }
  return [...settings.enabledValidators]
}

/**
 * Check if a specific validator is enabled
 */
export function isValidatorEnabled(validator: string, settings?: AgentSettings): boolean {
  const manager = settings ? null : getSettingsManager()
  const agentSettings = settings ?? manager?.agent

  if (!agentSettings?.validatorsEnabled) {
    return false
  }

  return agentSettings.enabledValidators.includes(
    validator as AgentSettings['enabledValidators'][number]
  )
}

// ============================================================================
// Permission Integration
// ============================================================================

/**
 * Check if a path is denied by settings
 */
export function isPathDenied(path: string, settings?: Settings): boolean {
  const manager = settings ? null : getSettingsManager()
  const deniedPaths = settings?.permissions.deniedPaths ?? manager?.permissions.deniedPaths ?? []

  // Normalize path
  const normalizedPath = path.replace(/\\/g, '/')

  for (const denied of deniedPaths) {
    const normalizedDenied = denied.replace(/\\/g, '/')
    if (normalizedPath.startsWith(normalizedDenied) || normalizedPath === normalizedDenied) {
      return true
    }
  }

  return false
}

/**
 * Check if an action requires confirmation
 */
export function requiresConfirmation(
  action: 'delete' | 'execute' | 'write' | 'network',
  settings?: Settings
): boolean {
  const manager = settings ? null : getSettingsManager()
  const confirmations =
    settings?.permissions.requireConfirmation ?? manager?.permissions.requireConfirmation ?? []

  return confirmations.includes(action)
}

// ============================================================================
// Combined Setup
// ============================================================================

/**
 * Initialize all settings integrations
 * Returns cleanup function that removes all watchers
 */
export function initializeSettingsIntegration(options: {
  manager?: SettingsManager
  context?: ContextManager
  onAgentConfigChange?: (config: Partial<AgentConfig>) => void
}): () => void {
  const manager = options.manager ?? getSettingsManager()
  const cleanups: (() => void)[] = []

  // Watch agent settings
  if (options.onAgentConfigChange) {
    cleanups.push(watchAgentSettings(manager, options.onAgentConfigChange))
  }

  // Watch context settings
  if (options.context) {
    cleanups.push(watchContextSettings(manager, options.context))
  }

  // Return combined cleanup
  return () => {
    for (const cleanup of cleanups) {
      cleanup()
    }
  }
}
