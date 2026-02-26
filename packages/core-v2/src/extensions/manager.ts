/**
 * Extension lifecycle manager.
 *
 * Discovers, loads, activates, and deactivates extensions.
 * Built-in extensions and community extensions use the same pipeline.
 */

import type { MessageBus } from '../bus/message-bus.js'
import { createLogger } from '../logger/logger.js'
import type { SessionManager } from '../session/manager.js'
import { createExtensionAPI } from './api.js'
import type {
  Disposable,
  Extension,
  ExtensionEvent,
  ExtensionEventListener,
  ExtensionManifest,
  ExtensionModule,
} from './types.js'

const log = createLogger('Extensions')

export class ExtensionManager {
  private extensions = new Map<string, Extension>()
  private disposables = new Map<string, Disposable>()
  private listeners = new Set<ExtensionEventListener>()
  private bus: MessageBus
  private sessionManager: SessionManager

  constructor(bus: MessageBus, sessionManager: SessionManager) {
    this.bus = bus
    this.sessionManager = sessionManager
  }

  // ─── Registration ──────────────────────────────────────────────────────

  /**
   * Register an extension from its manifest and module.
   * Does not activate — call `activate()` separately.
   */
  register(manifest: ExtensionManifest, path: string): Extension {
    const extension: Extension = {
      manifest,
      path,
      isActive: false,
    }
    this.extensions.set(manifest.name, extension)
    log.debug(`Extension registered: ${manifest.name}`)
    return extension
  }

  // ─── Activation ────────────────────────────────────────────────────────

  async activate(name: string, module: ExtensionModule): Promise<void> {
    const extension = this.extensions.get(name)
    if (!extension) {
      throw new Error(`Extension not found: "${name}"`)
    }
    if (extension.isActive) {
      log.warn(`Extension already active: ${name}`)
      return
    }

    const api = createExtensionAPI(name, this.bus, this.sessionManager)

    try {
      const disposable = await module.activate(api)
      if (disposable) {
        this.disposables.set(name, disposable)
      }
      extension.isActive = true
      log.info(`Extension activated: ${name}`)
      this.emit({ type: 'activated', name })
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      log.error(`Failed to activate extension: ${name}`, { error: message })
      this.emit({ type: 'error', name, error: message })
      throw err
    }
  }

  async deactivate(name: string): Promise<void> {
    const extension = this.extensions.get(name)
    if (!extension || !extension.isActive) return

    const disposable = this.disposables.get(name)
    if (disposable) {
      disposable.dispose()
      this.disposables.delete(name)
    }

    extension.isActive = false
    log.info(`Extension deactivated: ${name}`)
    this.emit({ type: 'deactivated', name })
  }

  /**
   * Activate all registered extensions that should be enabled by default,
   * sorted by priority.
   */
  async activateAll(modules: Map<string, ExtensionModule>): Promise<void> {
    const sorted = [...this.extensions.entries()]
      .filter(([, ext]) => ext.manifest.enabledByDefault !== false)
      .sort(([, a], [, b]) => (a.manifest.priority ?? 10) - (b.manifest.priority ?? 10))

    for (const [name] of sorted) {
      const module = modules.get(name)
      if (module) {
        await this.activate(name, module)
      } else {
        log.warn(`No module found for extension: ${name}`)
      }
    }

    this.emit({ type: 'loaded', count: this.getActiveExtensions().length })
  }

  // ─── Queries ───────────────────────────────────────────────────────────

  getExtension(name: string): Extension | undefined {
    return this.extensions.get(name)
  }

  getExtensions(): Extension[] {
    return [...this.extensions.values()]
  }

  getActiveExtensions(): Extension[] {
    return [...this.extensions.values()].filter((e) => e.isActive)
  }

  isActive(name: string): boolean {
    return this.extensions.get(name)?.isActive ?? false
  }

  get size(): number {
    return this.extensions.size
  }

  // ─── Events ──────────────────────────────────────────────────────────────

  on(listener: ExtensionEventListener): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  private emit(event: ExtensionEvent): void {
    for (const listener of this.listeners) {
      listener(event)
    }
  }

  // ─── Lifecycle ─────────────────────────────────────────────────────────

  async dispose(): Promise<void> {
    for (const name of this.extensions.keys()) {
      await this.deactivate(name)
    }
    this.extensions.clear()
    this.listeners.clear()
  }

  reset(): void {
    for (const disposable of this.disposables.values()) {
      disposable.dispose()
    }
    this.disposables.clear()
    this.extensions.clear()
    this.listeners.clear()
  }
}
