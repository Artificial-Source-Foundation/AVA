/**
 * Settings → Registry sync.
 *
 * When commander activates, reads user agent presets from settings
 * and registers them into the agent registry.
 */

import type { ExtensionAPI } from '@ava/core-v2/extensions'
import type { AgentDefinition, AgentTier } from './agent-definition.js'
import { registerAgent } from './registry.js'

interface AgentSettingsPreset {
  id: string
  name: string
  description: string
  enabled?: boolean
  tier?: AgentTier
  systemPrompt?: string
  tools?: string[]
  delegates?: string[]
  model?: string
  provider?: string
  maxTurns?: number
  maxTimeMinutes?: number
  domain?: string
  capabilities?: string[]
  icon?: string
  isCustom?: boolean
}

/** Convert a settings preset to an AgentDefinition. */
export function presetToDefinition(preset: AgentSettingsPreset): AgentDefinition {
  return {
    id: preset.id,
    name: preset.id,
    displayName: preset.name,
    description: preset.description,
    tier: preset.tier ?? 'worker',
    systemPrompt: preset.systemPrompt ?? '',
    tools: preset.tools ?? [],
    delegates: preset.delegates,
    model: preset.model,
    provider: preset.provider,
    maxTurns: preset.maxTurns,
    maxTimeMinutes: preset.maxTimeMinutes,
    domain: preset.domain,
    icon: preset.icon,
    capabilities: preset.capabilities,
    isBuiltIn: !preset.isCustom,
  }
}

/**
 * Read agent presets from settings and register custom ones.
 * Only registers agents with isCustom=true and enabled=true.
 * Built-in agents are already registered by workers.ts.
 */
export function syncSettingsToRegistry(api: ExtensionAPI): void {
  try {
    const config = api.getSettings<{ agents?: AgentSettingsPreset[] }>('agents')
    if (!config?.agents) return

    for (const preset of config.agents) {
      if (!preset.isCustom) continue
      if (preset.enabled === false) continue
      registerAgent(presetToDefinition(preset))
    }
  } catch {
    // Settings category not available — skip
  }
}
