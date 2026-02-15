/**
 * Settings Hydration
 * Restores non-serializable references (icons) and merges partial data with defaults.
 * Pure functions — no signals, no side effects.
 */

import type { AgentPreset } from '../../config/defaults/agent-defaults'
import { defaultAgentPresets } from '../../config/defaults/agent-defaults'
import type { LLMProviderConfig } from '../../config/defaults/provider-defaults'
import { defaultProviders } from '../../config/defaults/provider-defaults'
import {
  DEFAULT_AGENT_LIMITS,
  DEFAULT_APPEARANCE,
  DEFAULT_BEHAVIOR,
  DEFAULT_GENERATION,
  DEFAULT_GIT,
  DEFAULT_NOTIFICATIONS,
  DEFAULT_SETTINGS,
  DEFAULT_UI,
} from './settings-defaults'
import type { AppSettings } from './settings-types'

/** Restore icon references from defaults for persisted providers */
export function hydrateProviders(saved: LLMProviderConfig[]): LLMProviderConfig[] {
  return saved.map((sp) => {
    const def = defaultProviders.find((d) => d.id === sp.id)
    return def
      ? { ...def, ...sp, icon: def.icon, models: sp.models.length > 0 ? sp.models : def.models }
      : sp
  })
}

/** Restore icon references from defaults for persisted agents */
export function hydrateAgents(saved: AgentPreset[]): AgentPreset[] {
  return saved.map((sa) => {
    const def = defaultAgentPresets.find((d) => d.id === sa.id)
    return def ? { ...def, ...sa, icon: def.icon } : sa
  })
}

/** Deep-merge partial settings with defaults (preserves new keys in sub-objects) */
export function mergeWithDefaults(parsed: Partial<AppSettings>): AppSettings {
  return {
    ...DEFAULT_SETTINGS,
    ...parsed,
    ui: { ...DEFAULT_UI, ...(parsed.ui ?? {}) },
    appearance: { ...DEFAULT_APPEARANCE, ...(parsed.appearance ?? {}) },
    generation: { ...DEFAULT_GENERATION, ...(parsed.generation ?? {}) },
    agentLimits: { ...DEFAULT_AGENT_LIMITS, ...(parsed.agentLimits ?? {}) },
    behavior: { ...DEFAULT_BEHAVIOR, ...(parsed.behavior ?? {}) },
    notifications: { ...DEFAULT_NOTIFICATIONS, ...(parsed.notifications ?? {}) },
    git: { ...DEFAULT_GIT, ...(parsed.git ?? {}) },
  }
}
