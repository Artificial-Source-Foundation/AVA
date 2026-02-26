/**
 * Config types — extensible settings system.
 *
 * Core defines only the base settings categories.
 * Extensions register additional categories via `registerCategory()`.
 */

// ─── Core Settings ───────────────────────────────────────────────────────────

export interface CoreSettings {
  provider: ProviderSettings
  agent: AgentSettings
}

export interface ProviderSettings {
  defaultProvider: string
  defaultModel: string
  weakModel?: string
  weakModelProvider?: string
  timeout: number
}

export interface AgentSettings {
  maxTurns: number
  maxTimeMinutes: number
  maxRetries: number
  gracePeriodMs: number
}

export const DEFAULT_PROVIDER_SETTINGS: ProviderSettings = {
  defaultProvider: 'anthropic',
  defaultModel: 'claude-sonnet-4-20250514',
  timeout: 120_000,
}

export const DEFAULT_AGENT_SETTINGS: AgentSettings = {
  maxTurns: 50,
  maxTimeMinutes: 30,
  maxRetries: 3,
  gracePeriodMs: 30_000,
}

// ─── Settings Events ─────────────────────────────────────────────────────────

export type SettingsEvent =
  | { type: 'settings_loaded' }
  | { type: 'settings_saved' }
  | { type: 'settings_reset'; category?: string }
  | { type: 'category_changed'; category: string }
  | { type: 'category_registered'; category: string }

export type SettingsEventListener = (event: SettingsEvent) => void
