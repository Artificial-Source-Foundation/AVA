/**
 * Extensible settings manager.
 *
 * Core defines base categories (provider, agent). Extensions register
 * their own categories via `registerCategory(namespace, defaults)`.
 */

import { createLogger } from '../logger/logger.js'
import {
  DEFAULT_AGENT_SETTINGS,
  DEFAULT_PROVIDER_SETTINGS,
  type SettingsEvent,
  type SettingsEventListener,
} from './types.js'

const log = createLogger('Config')

export class SettingsManager {
  private categories = new Map<string, unknown>()
  private defaults = new Map<string, unknown>()
  private listeners = new Set<SettingsEventListener>()
  private dirty = false

  constructor() {
    // Register core categories
    this.registerCategory('provider', DEFAULT_PROVIDER_SETTINGS)
    this.registerCategory('agent', DEFAULT_AGENT_SETTINGS)
  }

  // ─── Category Registration ───────────────────────────────────────────────

  /** Register a settings category with defaults. Extensions use this. */
  registerCategory(namespace: string, defaults: unknown): void {
    if (!this.defaults.has(namespace)) {
      this.defaults.set(namespace, structuredClone(defaults))
      this.categories.set(namespace, structuredClone(defaults))
      this.emit({ type: 'category_registered', category: namespace })
      log.debug(`Settings category registered: ${namespace}`)
    }
  }

  getRegisteredCategories(): string[] {
    return [...this.categories.keys()]
  }

  // ─── Get / Set ─────────────────────────────────────────────────────────

  get<T>(category: string): T {
    const value = this.categories.get(category)
    if (value === undefined) {
      throw new Error(`Unknown settings category: "${category}"`)
    }
    return value as T
  }

  getAll(): Record<string, unknown> {
    const result: Record<string, unknown> = {}
    for (const [key, value] of this.categories) {
      result[key] = value
    }
    return result
  }

  set(category: string, value: Partial<Record<string, unknown>>): void {
    const current = this.categories.get(category)
    if (current === undefined) {
      throw new Error(`Unknown settings category: "${category}"`)
    }
    const merged = { ...(current as Record<string, unknown>), ...value }
    this.categories.set(category, merged)
    this.dirty = true
    this.emit({ type: 'category_changed', category })
  }

  reset(category: string): void {
    const defaults = this.defaults.get(category)
    if (defaults === undefined) {
      throw new Error(`Unknown settings category: "${category}"`)
    }
    this.categories.set(category, structuredClone(defaults))
    this.dirty = true
    this.emit({ type: 'settings_reset', category })
  }

  resetAll(): void {
    for (const [key, defaults] of this.defaults) {
      this.categories.set(key, structuredClone(defaults))
    }
    this.dirty = true
    this.emit({ type: 'settings_reset' })
  }

  isDirty(): boolean {
    return this.dirty
  }

  markClean(): void {
    this.dirty = false
  }

  // ─── Events ──────────────────────────────────────────────────────────────

  on(listener: SettingsEventListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  private emit(event: SettingsEvent): void {
    for (const listener of this.listeners) {
      listener(event)
    }
  }
}

// ─── Singleton ───────────────────────────────────────────────────────────────

let _instance: SettingsManager | null = null

export function getSettingsManager(): SettingsManager {
  if (!_instance) {
    _instance = new SettingsManager()
  }
  return _instance
}

export function setSettingsManager(manager: SettingsManager | null): void {
  _instance = manager
}

export function resetSettingsManager(): void {
  _instance = null
}
