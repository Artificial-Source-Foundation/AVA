/**
 * Settings Hydration
 * Restores non-serializable references (icons) and merges partial data with defaults.
 * Pure functions — no signals, no side effects.
 */

import { getProviderLogo } from '../../components/icons/provider-logo-map'
import type { AgentPreset } from '../../config/defaults/agent-defaults'
import { defaultAgentPresets } from '../../config/defaults/agent-defaults'
import type { LLMProviderConfig, ProviderModel } from '../../config/defaults/provider-defaults'
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
  const savedIds = new Set(saved.map((s) => s.id))

  const hydrated = saved.map((sp) => {
    const def = defaultProviders.find((d) => d.id === sp.id)
    if (!def) return { ...sp, icon: getProviderLogo(sp.id) }

    // Merge models: persisted models enriched with defaults, plus defaults not in persisted
    const models = mergeModels(sp.models, def.models)
    return { ...def, ...sp, icon: def.icon, models }
  })

  // Add any new default providers not present in saved data
  for (const def of defaultProviders) {
    if (!savedIds.has(def.id)) hydrated.push(def)
  }

  return hydrated
}

/** Merge persisted models with hardcoded defaults so no models are lost */
function mergeModels(saved: ProviderModel[], defaults: ProviderModel[]): ProviderModel[] {
  if (saved.length === 0) return defaults

  const merged = new Map<string, ProviderModel>()

  // Start with saved models, enriched with default pricing/capabilities
  for (const s of saved) {
    const def = defaults.find((d) => d.id === s.id)
    merged.set(s.id, {
      ...s,
      pricing: s.pricing ?? def?.pricing,
      capabilities: s.capabilities?.length ? s.capabilities : def?.capabilities,
    })
  }

  // Add default models not in saved list
  for (const d of defaults) {
    if (!merged.has(d.id)) merged.set(d.id, d)
  }

  return [...merged.values()]
}

/** Restore icon references from defaults for persisted agents */
export function hydrateAgents(saved: AgentPreset[]): AgentPreset[] {
  return saved.map((sa) => {
    const { tier: _legacyTier, ...cleanSaved } = sa as AgentPreset & { tier?: unknown }
    const def = defaultAgentPresets.find((d) => d.id === sa.id)
    return def ? { ...def, ...cleanSaved, icon: def.icon } : cleanSaved
  })
}

/** Deep-merge partial settings with defaults (preserves new keys in sub-objects) */
export function mergeWithDefaults(parsed: Partial<AppSettings>): AppSettings {
  // Migrate legacy "microagents" keys → "skills" (localStorage may have old names)
  const { team: _removedTeam, ...cleanParsed } = parsed as Partial<AppSettings> & {
    team?: unknown
  }
  const legacy = cleanParsed as Record<string, unknown>
  const enabledSkills =
    cleanParsed.enabledSkills ?? (legacy.enabledMicroagents as string[] | undefined) ?? []
  const customSkills =
    cleanParsed.customSkills ??
    (legacy.customMicroagents as AppSettings['customSkills'] | undefined) ??
    []

  // Drop removed generation keys that may still exist in persisted localStorage
  const { delegationEnabled: _deprecatedDelegationEnabled, ...persistedGeneration } =
    (parsed.generation ?? {}) as AppSettings['generation'] & {
      delegationEnabled?: boolean
    }

  // Migrate thinkingEnabled → reasoningEffort (old boolean → new effort level)
  const gen = { ...DEFAULT_GENERATION, ...persistedGeneration }
  if (!('reasoningEffort' in (parsed.generation ?? {}))) {
    gen.reasoningEffort = gen.thinkingEnabled ? 'medium' : 'off'
  }

  return {
    ...DEFAULT_SETTINGS,
    ...cleanParsed,
    enabledSkills,
    customSkills,
    customRules: cleanParsed.customRules ?? [],
    hiddenBuiltInSkills: cleanParsed.hiddenBuiltInSkills ?? [],
    ui: { ...DEFAULT_UI, ...cleanParsed.ui },
    appearance: { ...DEFAULT_APPEARANCE, ...cleanParsed.appearance },
    generation: gen,
    agentLimits: { ...DEFAULT_AGENT_LIMITS, ...cleanParsed.agentLimits },
    behavior: { ...DEFAULT_BEHAVIOR, ...cleanParsed.behavior },
    notifications: { ...DEFAULT_NOTIFICATIONS, ...cleanParsed.notifications },
    git: { ...DEFAULT_GIT, ...cleanParsed.git },
  }
}
