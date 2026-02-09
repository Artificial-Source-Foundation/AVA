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
import { readSettingsFromFS, writeSettingsToFS } from '../services/settings-fs'

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

  sm.set('context', { maxTokens: s.generation.maxTokens, compactionThreshold: 80 })
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

export type AccentColor = 'violet' | 'blue' | 'green' | 'rose' | 'amber' | 'cyan' | 'custom'
export type MonoFont = 'default' | 'jetbrains' | 'fira'
export type SansFont = 'default' | 'inter' | 'outfit' | 'nunito'
export type BorderRadius = 'sharp' | 'default' | 'rounded' | 'pill'
export type UIDensity = 'compact' | 'default' | 'comfortable'
export type CodeTheme =
  | 'default'
  | 'github-dark'
  | 'monokai'
  | 'nord'
  | 'solarized-dark'
  | 'catppuccin'
export type DarkStyle = 'dark' | 'midnight' | 'charcoal'

export interface AppearanceSettings {
  uiScale: number // 0.85 – 1.2, default 1.0 (maps to html font-size: 16px * scale)
  accentColor: AccentColor
  customAccentColor: string // hex, default '#8b5cf6'
  fontMono: MonoFont
  fontSans: SansFont
  fontLigatures: boolean
  chatFontSize: number // 11–20, default 13 (px, independent of uiScale)
  borderRadius: BorderRadius
  density: UIDensity
  codeTheme: CodeTheme
  darkStyle: DarkStyle
  highContrast: boolean
  reduceMotion: boolean
}

export type SendKey = 'enter' | 'ctrl+enter'

export interface GenerationSettings {
  maxTokens: number // 256–32000, default 4096
  temperature: number // 0.0–2.0, default 0.7
  topP: number // 0.0–1.0, default 1.0
  customInstructions: string // prepended as system message
}

export interface AgentLimitSettings {
  agentMaxTurns: number // 1–100, default 20
  agentMaxTimeMinutes: number // 1–60, default 10
  autoFixLint: boolean // Run linter after file edits, append errors to tool result
}

export interface BehaviorSettings {
  sendKey: SendKey
  sessionAutoTitle: boolean
  autoScroll: boolean
  lineNumbers: boolean
  wordWrap: boolean
}

export interface NotificationSettings {
  notifyOnCompletion: boolean
  soundOnCompletion: boolean
  soundVolume: number // 0–100, default 50
}

export interface AppSettings {
  onboardingComplete: boolean
  theme: string
  mode: 'light' | 'dark' | 'system'
  providers: LLMProviderConfig[]
  agents: AgentPreset[]
  autoApprovedTools: string[]
  ui: UISettings
  appearance: AppearanceSettings
  generation: GenerationSettings
  agentLimits: AgentLimitSettings
  behavior: BehaviorSettings
  notifications: NotificationSettings
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
  customAccentColor: '#8b5cf6',
  fontMono: 'default',
  fontSans: 'default',
  fontLigatures: true,
  chatFontSize: 13,
  borderRadius: 'default',
  density: 'default',
  codeTheme: 'default',
  darkStyle: 'dark',
  highContrast: false,
  reduceMotion: false,
}

const DEFAULT_GENERATION: GenerationSettings = {
  maxTokens: 4096,
  temperature: 0.7,
  topP: 1.0,
  customInstructions: '',
}

const DEFAULT_AGENT_LIMITS: AgentLimitSettings = {
  agentMaxTurns: 20,
  agentMaxTimeMinutes: 10,
  autoFixLint: true,
}

const DEFAULT_BEHAVIOR: BehaviorSettings = {
  sendKey: 'enter',
  sessionAutoTitle: true,
  autoScroll: true,
  lineNumbers: true,
  wordWrap: false,
}

const DEFAULT_NOTIFICATIONS: NotificationSettings = {
  notifyOnCompletion: true,
  soundOnCompletion: false,
  soundVolume: 50,
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
  generation: { ...DEFAULT_GENERATION },
  agentLimits: { ...DEFAULT_AGENT_LIMITS },
  behavior: { ...DEFAULT_BEHAVIOR },
  notifications: { ...DEFAULT_NOTIFICATIONS },
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
        generation: { ...DEFAULT_GENERATION, ...(parsed.generation ?? {}) },
        agentLimits: { ...DEFAULT_AGENT_LIMITS, ...(parsed.agentLimits ?? {}) },
        behavior: { ...DEFAULT_BEHAVIOR, ...(parsed.behavior ?? {}) },
        notifications: { ...DEFAULT_NOTIFICATIONS, ...(parsed.notifications ?? {}) },
      }
    }
  } catch {
    /* corrupted data — use defaults */
  }
  return { ...DEFAULT_SETTINGS }
}

/** Serialize settings for storage (strip non-serializable fields like icon functions) */
function serializeSettings(settings: AppSettings): Record<string, unknown> {
  return {
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
}

function saveSettings(settings: AppSettings) {
  const serializable = serializeSettings(settings)
  try {
    localStorage.setItem(STORAGE_KEYS.SETTINGS, JSON.stringify(serializable))
    // Bridge estela-mode for index.html flash-prevention script
    localStorage.setItem('estela-mode', settings.mode)
  } catch (err) {
    console.warn('[settings] localStorage write failed:', err)
  }
  // Write to Tauri FS backend (async, fire-and-forget)
  writeSettingsToFS(serializable).catch(() => {})
  // Keep core SettingsManager in sync
  pushSettingsToCore()
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

/** Resolve whether we're currently in a dark-like mode (for UI conditionals) */
export function isDarkMode(): boolean {
  const s = settings()
  if (s.mode === 'system') {
    return window.matchMedia('(prefers-color-scheme: dark)').matches
  }
  return s.mode === 'dark'
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

function updateGeneration(patch: Partial<GenerationSettings>) {
  setSettingsRaw((prev) => {
    const next = { ...prev, generation: { ...prev.generation, ...patch } }
    saveSettings(next)
    return next
  })
}

function updateAgentLimits(patch: Partial<AgentLimitSettings>) {
  setSettingsRaw((prev) => {
    const next = { ...prev, agentLimits: { ...prev.agentLimits, ...patch } }
    saveSettings(next)
    return next
  })
}

function updateBehavior(patch: Partial<BehaviorSettings>) {
  setSettingsRaw((prev) => {
    const next = { ...prev, behavior: { ...prev.behavior, ...patch } }
    saveSettings(next)
    return next
  })
}

function updateNotifications(patch: Partial<NotificationSettings>) {
  setSettingsRaw((prev) => {
    const next = { ...prev, notifications: { ...prev.notifications, ...patch } }
    saveSettings(next)
    return next
  })
}

/** Export current settings as a JSON file (triggers download) */
function exportSettings() {
  const data = serializeSettings(settings())
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `estela-settings-${new Date().toISOString().slice(0, 10)}.json`
  a.click()
  URL.revokeObjectURL(url)
}

/** Import settings from a JSON file */
async function importSettings(): Promise<void> {
  return new Promise((resolve) => {
    const input = document.createElement('input')
    input.type = 'file'
    input.accept = '.json'
    input.onchange = async () => {
      const file = input.files?.[0]
      if (!file) {
        resolve()
        return
      }
      try {
        const text = await file.text()
        const parsed = JSON.parse(text) as Partial<AppSettings>
        const merged: AppSettings = {
          ...DEFAULT_SETTINGS,
          ...parsed,
          ui: { ...DEFAULT_UI, ...(parsed.ui ?? {}) },
          appearance: { ...DEFAULT_APPEARANCE, ...(parsed.appearance ?? {}) },
          generation: { ...DEFAULT_GENERATION, ...(parsed.generation ?? {}) },
          agentLimits: { ...DEFAULT_AGENT_LIMITS, ...(parsed.agentLimits ?? {}) },
          behavior: { ...DEFAULT_BEHAVIOR, ...(parsed.behavior ?? {}) },
          notifications: { ...DEFAULT_NOTIFICATIONS, ...(parsed.notifications ?? {}) },
        }
        merged.providers = hydrateProviders(
          (merged.providers ?? defaultProviders) as LLMProviderConfig[]
        )
        merged.agents = hydrateAgents((merged.agents ?? defaultAgentPresets) as AgentPreset[])
        setSettingsRaw(merged)
        saveSettings(merged)
        applyAppearance()
      } catch (err) {
        console.warn('[settings] Import failed:', err)
      }
      resolve()
    }
    input.click()
  })
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
    '--density-py': '0.25rem',
    '--density-px': '0.5rem',
    '--density-gap': '0.25rem',
    '--density-section-py': '0.375rem',
    '--density-section-px': '0.625rem',
  },
  default: {
    '--density-spacing': '1',
    '--density-py': '0.375rem',
    '--density-px': '0.75rem',
    '--density-gap': '0.5rem',
    '--density-section-py': '0.75rem',
    '--density-section-px': '1rem',
  },
  comfortable: {
    '--density-spacing': '1.25',
    '--density-py': '0.5rem',
    '--density-px': '1rem',
    '--density-gap': '0.75rem',
    '--density-section-py': '1rem',
    '--density-section-px': '1.25rem',
  },
}

const SANS_FONTS: Record<SansFont, string> = {
  default: '"Geist", "Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
  inter: '"Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
  outfit: '"Outfit", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
  nunito: '"Nunito Sans", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
}

/** Parse hex color (#rrggbb) to [r, g, b] */
function hexToRgb(hex: string): [number, number, number] {
  const h = hex.replace('#', '')
  return [
    Number.parseInt(h.substring(0, 2), 16),
    Number.parseInt(h.substring(2, 4), 16),
    Number.parseInt(h.substring(4, 6), 16),
  ]
}

/** Lighten/darken an [r,g,b] by a factor (>1 = lighter, <1 = darker) */
function adjustBrightness(rgb: [number, number, number], factor: number): [number, number, number] {
  return [
    Math.min(255, Math.round(rgb[0] * factor)),
    Math.min(255, Math.round(rgb[1] * factor)),
    Math.min(255, Math.round(rgb[2] * factor)),
  ]
}

/** Compute all accent CSS vars from a single hex color */
function hexToAccentVars(hex: string): Record<string, string> {
  const rgb = hexToRgb(hex)
  const lighter = adjustBrightness(rgb, 1.25)
  const darker = adjustBrightness(rgb, 0.78)
  const darkMuted = adjustBrightness(rgb, 0.35)
  return {
    '--accent': hex,
    '--accent-hover': `rgb(${lighter.join(',')})`,
    '--accent-active': `rgb(${darker.join(',')})`,
    '--accent-subtle': `rgba(${rgb.join(',')}, 0.15)`,
    '--accent-muted': `rgb(${darkMuted.join(',')})`,
    '--accent-border': `rgba(${rgb.join(',')}, 0.3)`,
    '--accent-glow': `rgba(${rgb.join(',')}, 0.4)`,
  }
}

/** Accent CSS var names to clean up when switching from custom to preset */
const ACCENT_VAR_NAMES = [
  '--accent',
  '--accent-hover',
  '--accent-active',
  '--accent-subtle',
  '--accent-muted',
  '--accent-border',
  '--accent-glow',
]

/** Resolve the effective data-mode value from settings */
export function resolveMode(s: AppSettings): string {
  if (s.mode === 'system') {
    return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
  }
  // 'light' stays as-is; dark variants come from darkStyle
  if (s.mode === 'dark') {
    return s.appearance.darkStyle // 'dark' | 'midnight' | 'charcoal'
  }
  return s.mode
}

/** Apply appearance settings to <html> element */
export function applyAppearance() {
  const s = settings()
  const el = document.documentElement

  // Color mode — resolve system + dark variants
  const resolved = resolveMode(s)
  el.dataset.mode = resolved

  // Accent color
  if (s.appearance.accentColor === 'custom') {
    // Apply computed custom accent vars inline
    delete el.dataset.accent
    const vars = hexToAccentVars(s.appearance.customAccentColor)
    for (const [prop, val] of Object.entries(vars)) {
      el.style.setProperty(prop, val)
    }
  } else {
    // Remove inline accent vars so CSS selectors take effect
    for (const prop of ACCENT_VAR_NAMES) {
      el.style.removeProperty(prop)
    }
    if (s.appearance.accentColor === 'violet') {
      delete el.dataset.accent
    } else {
      el.dataset.accent = s.appearance.accentColor
    }
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

  // Sans font
  el.style.setProperty('--font-sans', SANS_FONTS[s.appearance.fontSans])

  // Mono font
  el.style.setProperty('--font-mono', MONO_FONTS[s.appearance.fontMono])
  el.style.setProperty('--font-ui-mono', MONO_FONTS[s.appearance.fontMono])

  // Font ligatures
  if (s.appearance.fontLigatures) {
    el.style.setProperty('font-variant-ligatures', 'normal')
    el.style.setProperty('font-feature-settings', '"liga" 1, "calt" 1')
  } else {
    el.style.setProperty('font-variant-ligatures', 'none')
    el.style.setProperty('font-feature-settings', '"liga" 0, "calt" 0')
  }

  // Chat font size (absolute px, independent of uiScale)
  el.style.setProperty('--chat-font-size', `${s.appearance.chatFontSize}px`)

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

  // Code theme
  if (s.appearance.codeTheme === 'default') {
    delete el.dataset.codeTheme
  } else {
    el.dataset.codeTheme = s.appearance.codeTheme
  }

  // High contrast
  if (s.appearance.highContrast) {
    el.dataset.highContrast = ''
  } else {
    delete el.dataset.highContrast
  }

  // Reduce motion — toggle data attribute for CSS, override prefers-reduced-motion
  if (s.appearance.reduceMotion) {
    el.dataset.reduceMotion = ''
  } else {
    delete el.dataset.reduceMotion
  }
}

/** Listen for OS theme changes and re-apply when mode is 'system' */
export function setupSystemThemeListener(): () => void {
  const mq = window.matchMedia('(prefers-color-scheme: dark)')
  const handler = () => {
    if (settings().mode === 'system') {
      applyAppearance()
    }
  }
  mq.addEventListener('change', handler)
  return () => mq.removeEventListener('change', handler)
}

/** Load settings from Tauri FS and merge with current state.
 *  Call once during app startup (after initSettingsFS). */
export async function hydrateSettingsFromFS(): Promise<void> {
  try {
    const fsData = await readSettingsFromFS()
    if (!fsData) return

    const parsed = fsData as Partial<AppSettings>
    const merged: AppSettings = {
      ...DEFAULT_SETTINGS,
      ...parsed,
      ui: { ...DEFAULT_UI, ...(parsed.ui ?? {}) },
      appearance: { ...DEFAULT_APPEARANCE, ...(parsed.appearance ?? {}) },
      generation: { ...DEFAULT_GENERATION, ...(parsed.generation ?? {}) },
      agentLimits: { ...DEFAULT_AGENT_LIMITS, ...(parsed.agentLimits ?? {}) },
      behavior: { ...DEFAULT_BEHAVIOR, ...(parsed.behavior ?? {}) },
      notifications: { ...DEFAULT_NOTIFICATIONS, ...(parsed.notifications ?? {}) },
    }
    merged.providers = hydrateProviders(
      (merged.providers ?? defaultProviders) as LLMProviderConfig[]
    )
    merged.agents = hydrateAgents((merged.agents ?? defaultAgentPresets) as AgentPreset[])

    // Only update if FS data is newer/different than localStorage
    const currentJson = JSON.stringify(serializeSettings(settings()))
    const fsJson = JSON.stringify(serializeSettings(merged))
    if (currentJson !== fsJson) {
      setSettingsRaw(merged)
      // Sync localStorage with FS data
      try {
        localStorage.setItem(STORAGE_KEYS.SETTINGS, fsJson)
        localStorage.setItem('estela-mode', merged.mode)
      } catch {
        /* ignore */
      }
      applyAppearance()
    }
  } catch (err) {
    console.warn('[settings] FS hydration failed:', err)
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
    updateGeneration,
    updateAgentLimits,
    updateBehavior,
    updateNotifications,
    cyclePermissionMode,
    resetSettings,
    exportSettings,
    importSettings,
  }
}
