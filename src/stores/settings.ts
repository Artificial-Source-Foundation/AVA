/**
 * Settings Store
 * Persistent settings with localStorage backup.
 * Follows the same pattern as layout.ts — module-level signals + export hook.
 */

import type { LLMProvider } from '@estela/core'
import { createSignal } from 'solid-js'
import type { AgentPreset } from '../components/settings/tabs/AgentsTab'
import { defaultAgentPresets } from '../components/settings/tabs/AgentsTab'
import type { LLMProviderConfig } from '../components/settings/tabs/ProvidersTab'
import { defaultProviders } from '../components/settings/tabs/ProvidersTab'
import { STORAGE_KEYS } from '../config/constants'
import { getCoreSettings } from '../services/core-bridge'

// ============================================================================
// Credential Sync — bridges Settings UI → Core credential store
// ============================================================================

const CREDENTIAL_PREFIX = 'estela_cred_'

/** Maps provider IDs to the credential key names that core reads via TauriCredentialStore */
const PROVIDER_KEY_MAP: Record<string, string> = {
  anthropic: 'anthropic-api-key',
  openrouter: 'openrouter-api-key',
  openai: 'openai-api-key',
  google: 'google-api-key',
  copilot: 'copilot-api-key',
  glm: 'glm-api-key',
  kimi: 'kimi-api-key',
  mistral: 'mistral-api-key',
  groq: 'groq-api-key',
  deepseek: 'deepseek-api-key',
  xai: 'xai-api-key',
  cohere: 'cohere-api-key',
  together: 'together-api-key',
  ollama: 'ollama-api-key',
}

/** Write a single provider's API key to the core credential store */
function syncProviderCredentials(providerId: string, apiKey: string | undefined) {
  const credKey = PROVIDER_KEY_MAP[providerId]
  if (!credKey) return
  const storageKey = CREDENTIAL_PREFIX + credKey
  if (apiKey) {
    localStorage.setItem(storageKey, apiKey)
  } else {
    localStorage.removeItem(storageKey)
  }
}

/** Hydrate all provider API keys from settings into the core credential store (call at startup) */
export function syncAllApiKeys() {
  const current = loadSettings()
  for (const provider of current.providers) {
    if (provider.apiKey) {
      syncProviderCredentials(provider.id, provider.apiKey)
    }
  }
}

// ============================================================================
// Core Settings Sync — pushes frontend AppSettings → core SettingsManager
// ============================================================================

/** Push current frontend settings to the core SettingsManager */
export function pushSettingsToCore() {
  const sm = getCoreSettings()
  if (!sm) return
  const s = settings()

  const activeProvider = s.providers.find((p) => p.enabled && p.apiKey)
  sm.set('provider', {
    defaultProvider: (activeProvider?.id ?? 'anthropic') as LLMProvider,
    defaultModel: activeProvider?.defaultModel ?? 'claude-sonnet-4-20250514',
  })

  sm.set('permissions', {
    allowBashExecution: s.permissionMode !== 'ask',
    autoApprovePatterns: s.autoApprovedTools,
  })

  sm.set('context', { maxTokens: 200_000, compactionThreshold: 80 })
  sm.set('memory', { enabled: true })
}

// ============================================================================
// Types
// ============================================================================

export type PermissionMode = 'ask' | 'auto-approve' | 'bypass'

export interface UISettings {
  showBottomPanel: boolean
  showAgentActivity: boolean
  compactMessages: boolean
  showInfoBar: boolean
  showTokenCount: boolean
  showModelInTitleBar: boolean
}

export type AccentColor = 'violet' | 'blue' | 'green' | 'rose' | 'amber' | 'cyan'
export type MonoFont = 'default' | 'jetbrains' | 'fira'
export type BorderRadius = 'sharp' | 'default' | 'rounded' | 'pill'
export type UIDensity = 'compact' | 'default' | 'comfortable'

export interface AppearanceSettings {
  uiScale: number // 0.85 – 1.2, default 1.0 (maps to html font-size: 16px * scale)
  accentColor: AccentColor
  fontMono: MonoFont
  borderRadius: BorderRadius
  density: UIDensity
  reduceMotion: boolean
}

export interface AppSettings {
  onboardingComplete: boolean
  theme: string
  mode: 'light' | 'dark'
  providers: LLMProviderConfig[]
  agents: AgentPreset[]
  autoApprovedTools: string[]
  ui: UISettings
  appearance: AppearanceSettings
  permissionMode: PermissionMode
}

const DEFAULT_UI: UISettings = {
  showBottomPanel: true,
  showAgentActivity: true,
  compactMessages: false,
  showInfoBar: true,
  showTokenCount: true,
  showModelInTitleBar: true,
}

const DEFAULT_APPEARANCE: AppearanceSettings = {
  uiScale: 1.0,
  accentColor: 'violet',
  fontMono: 'default',
  borderRadius: 'default',
  density: 'default',
  reduceMotion: false,
}

const DEFAULT_SETTINGS: AppSettings = {
  onboardingComplete: false,
  theme: 'glass',
  mode: 'dark',
  providers: defaultProviders,
  agents: defaultAgentPresets,
  autoApprovedTools: [],
  ui: { ...DEFAULT_UI },
  appearance: { ...DEFAULT_APPEARANCE },
  permissionMode: 'ask',
}

// ============================================================================
// Persistence
// ============================================================================

function loadSettings(): AppSettings {
  try {
    const raw = localStorage.getItem(STORAGE_KEYS.SETTINGS)
    if (raw) {
      const parsed = JSON.parse(raw) as Partial<AppSettings>
      // Deep-merge sub-objects so new keys get defaults
      return {
        ...DEFAULT_SETTINGS,
        ...parsed,
        ui: { ...DEFAULT_UI, ...(parsed.ui ?? {}) },
        appearance: { ...DEFAULT_APPEARANCE, ...(parsed.appearance ?? {}) },
      }
    }
  } catch {
    /* corrupted data — use defaults */
  }
  return { ...DEFAULT_SETTINGS }
}

function saveSettings(settings: AppSettings) {
  try {
    // Strip icon references before serializing (they're functions)
    const serializable = {
      ...settings,
      providers: settings.providers.map((p) => ({
        id: p.id,
        name: p.name,
        description: p.description,
        enabled: p.enabled,
        apiKey: p.apiKey,
        baseUrl: p.baseUrl,
        defaultModel: p.defaultModel,
        models: p.models,
        status: p.status,
        error: p.error,
      })),
      agents: settings.agents.map((a) => ({
        id: a.id,
        name: a.name,
        description: a.description,
        enabled: a.enabled,
        systemPrompt: a.systemPrompt,
        capabilities: a.capabilities,
        model: a.model,
        isCustom: a.isCustom,
        type: a.type,
      })),
    }
    localStorage.setItem(STORAGE_KEYS.SETTINGS, JSON.stringify(serializable))
    // Keep core SettingsManager in sync
    pushSettingsToCore()
  } catch {
    /* storage full or unavailable */
  }
}

// ============================================================================
// Hydrate providers/agents — restore icon references from defaults
// ============================================================================

function hydrateProviders(saved: LLMProviderConfig[]): LLMProviderConfig[] {
  return saved.map((sp) => {
    const def = defaultProviders.find((d) => d.id === sp.id)
    return def
      ? { ...def, ...sp, icon: def.icon, models: sp.models.length > 0 ? sp.models : def.models }
      : sp
  })
}

function hydrateAgents(saved: AgentPreset[]): AgentPreset[] {
  return saved.map((sa) => {
    const def = defaultAgentPresets.find((d) => d.id === sa.id)
    return def ? { ...def, ...sa, icon: def.icon } : sa
  })
}

// ============================================================================
// Module-level Signals
// ============================================================================

const initial = loadSettings()
initial.providers = hydrateProviders(initial.providers)
initial.agents = hydrateAgents(initial.agents)

const [settings, setSettingsRaw] = createSignal<AppSettings>(initial)

// ============================================================================
// Mutators — update + persist in one call
// ============================================================================

function updateSettings(patch: Partial<AppSettings>) {
  setSettingsRaw((prev) => {
    const next = { ...prev, ...patch }
    saveSettings(next)
    return next
  })
  // Re-apply appearance if mode changed
  if (patch.mode !== undefined) {
    applyAppearance()
  }
}

function updateProvider(id: string, patch: Partial<LLMProviderConfig>) {
  setSettingsRaw((prev) => {
    const next = {
      ...prev,
      providers: prev.providers.map((p) => (p.id === id ? { ...p, ...patch } : p)),
    }
    saveSettings(next)
    return next
  })
  // Sync API key to core credential store so LLM providers can read it
  if (patch.apiKey !== undefined) {
    syncProviderCredentials(id, patch.apiKey)
  }
}

function updateAgent(id: string, patch: Partial<AgentPreset>) {
  setSettingsRaw((prev) => {
    const next = {
      ...prev,
      agents: prev.agents.map((a) => (a.id === id ? { ...a, ...patch } : a)),
    }
    saveSettings(next)
    return next
  })
}

function addAgent(agent: AgentPreset) {
  setSettingsRaw((prev) => {
    const next = { ...prev, agents: [...prev.agents, agent] }
    saveSettings(next)
    return next
  })
}

function removeAgent(id: string) {
  setSettingsRaw((prev) => {
    const next = { ...prev, agents: prev.agents.filter((a) => a.id !== id) }
    saveSettings(next)
    return next
  })
}

function addAutoApprovedTool(toolName: string) {
  setSettingsRaw((prev) => {
    if (prev.autoApprovedTools.includes(toolName)) return prev
    const next = { ...prev, autoApprovedTools: [...prev.autoApprovedTools, toolName] }
    saveSettings(next)
    return next
  })
}

function isToolAutoApproved(toolName: string): boolean {
  return settings().autoApprovedTools.includes(toolName)
}

function removeAutoApprovedTool(toolName: string) {
  setSettingsRaw((prev) => {
    const next = {
      ...prev,
      autoApprovedTools: prev.autoApprovedTools.filter((t) => t !== toolName),
    }
    saveSettings(next)
    return next
  })
}

function cyclePermissionMode() {
  const modes: PermissionMode[] = ['ask', 'auto-approve', 'bypass']
  setSettingsRaw((prev) => {
    const idx = modes.indexOf(prev.permissionMode)
    const next = { ...prev, permissionMode: modes[(idx + 1) % modes.length] }
    saveSettings(next)
    return next
  })
}

function updateUI(patch: Partial<UISettings>) {
  setSettingsRaw((prev) => {
    const next = { ...prev, ui: { ...prev.ui, ...patch } }
    saveSettings(next)
    return next
  })
}

function updateAppearance(patch: Partial<AppearanceSettings>) {
  setSettingsRaw((prev) => {
    const next = { ...prev, appearance: { ...prev.appearance, ...patch } }
    saveSettings(next)
    return next
  })
  // Apply changes to DOM immediately
  applyAppearance()
}

const MONO_FONTS: Record<MonoFont, string> = {
  default: '"Geist Mono", "JetBrains Mono", "Fira Code", "SF Mono", monospace',
  jetbrains: '"JetBrains Mono", "Fira Code", "SF Mono", monospace',
  fira: '"Fira Code", "JetBrains Mono", "SF Mono", monospace',
}

const RADIUS_SCALES: Record<BorderRadius, Record<string, string>> = {
  sharp: {
    '--radius-sm': '0px',
    '--radius-md': '2px',
    '--radius-lg': '3px',
    '--radius-xl': '4px',
    '--radius-2xl': '6px',
  },
  default: {
    '--radius-sm': '3px',
    '--radius-md': '5px',
    '--radius-lg': '6px',
    '--radius-xl': '10px',
    '--radius-2xl': '14px',
  },
  rounded: {
    '--radius-sm': '6px',
    '--radius-md': '8px',
    '--radius-lg': '10px',
    '--radius-xl': '14px',
    '--radius-2xl': '20px',
  },
  pill: {
    '--radius-sm': '9999px',
    '--radius-md': '9999px',
    '--radius-lg': '9999px',
    '--radius-xl': '9999px',
    '--radius-2xl': '9999px',
  },
}

const DENSITY_SCALES: Record<UIDensity, Record<string, string>> = {
  compact: {
    '--density-spacing': '0.75',
    '--density-py': '0.15rem',
    '--density-px': '0.4rem',
    '--density-gap': '0.25rem',
  },
  default: {
    '--density-spacing': '1',
    '--density-py': '0.25rem',
    '--density-px': '0.625rem',
    '--density-gap': '0.5rem',
  },
  comfortable: {
    '--density-spacing': '1.25',
    '--density-py': '0.375rem',
    '--density-px': '0.75rem',
    '--density-gap': '0.75rem',
  },
}

/** Apply appearance settings (mode, accent, scale, font, radius, density, motion) to <html> element */
export function applyAppearance() {
  const s = settings()
  const el = document.documentElement

  // Color mode
  el.dataset.mode = s.mode

  // Accent color (remove attr for default violet)
  if (s.appearance.accentColor === 'violet') {
    delete el.dataset.accent
  } else {
    el.dataset.accent = s.appearance.accentColor
  }

  // UI scale — save scroll ratios, change font-size, force synchronous reflow, restore
  const scrollContainers = document.querySelectorAll('[style*="translateZ"]')
  const scrollState: { el: Element; ratio: number }[] = []
  for (const sc of scrollContainers) {
    if (sc.scrollHeight > sc.clientHeight) {
      scrollState.push({
        el: sc,
        ratio: sc.scrollTop / (sc.scrollHeight - sc.clientHeight),
      })
    }
  }

  el.style.fontSize = `${16 * s.appearance.uiScale}px`

  // Force synchronous reflow so new dimensions are available immediately
  void el.offsetHeight

  // Restore scroll positions in the same frame (no rAF delay = no stutter)
  for (const { el: sc, ratio } of scrollState) {
    sc.scrollTop = ratio * (sc.scrollHeight - sc.clientHeight)
  }

  // Mono font
  el.style.setProperty('--font-mono', MONO_FONTS[s.appearance.fontMono])
  el.style.setProperty('--font-ui-mono', MONO_FONTS[s.appearance.fontMono])

  // Border radius
  const radii = RADIUS_SCALES[s.appearance.borderRadius]
  for (const [prop, val] of Object.entries(radii)) {
    el.style.setProperty(prop, val)
  }

  // UI density
  const density = DENSITY_SCALES[s.appearance.density]
  for (const [prop, val] of Object.entries(density)) {
    el.style.setProperty(prop, val)
  }

  // Reduce motion — toggle data attribute for CSS, override prefers-reduced-motion
  if (s.appearance.reduceMotion) {
    el.dataset.reduceMotion = ''
  } else {
    delete el.dataset.reduceMotion
  }
}

function resetSettings() {
  const fresh = { ...DEFAULT_SETTINGS }
  setSettingsRaw(fresh)
  saveSettings(fresh)
}

// ============================================================================
// Export Hook
// ============================================================================

export function useSettings() {
  return {
    settings,
    updateSettings,
    updateProvider,
    updateAgent,
    addAgent,
    removeAgent,
    addAutoApprovedTool,
    removeAutoApprovedTool,
    isToolAutoApproved,
    updateUI,
    updateAppearance,
    cyclePermissionMode,
    resetSettings,
  }
}
