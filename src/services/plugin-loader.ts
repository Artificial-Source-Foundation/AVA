/**
 * Dynamic Plugin Loader
 *
 * Handles loading community plugins from ~/.ava/plugins/ with
 * permission-based sandboxing. Also provides plugin directory watching
 * and individual plugin reload.
 */

import type { PluginManifest, PluginPermission } from '../types/plugin'

/** Minimal Disposable interface (replaces @ava/core-v2/extensions import) */
interface Disposable {
  dispose(): void
}

/** Minimal ExtensionAPI interface for plugin sandboxing */
interface ExtensionAPI {
  registerTool(tool: { definition?: { name?: string }; [key: string]: unknown }): Disposable
  registerCommand: (...args: unknown[]) => Disposable
  registerAgentMode: (...args: unknown[]) => Disposable
  registerValidator: (...args: unknown[]) => Disposable
  registerContextStrategy: (...args: unknown[]) => Disposable
  registerProvider: (...args: unknown[]) => Disposable
  addToolMiddleware: (...args: unknown[]) => Disposable
  on: (...args: unknown[]) => Disposable
  emit: (...args: unknown[]) => void
  getSettings: (...args: unknown[]) => unknown
  onSettingsChanged: (...args: unknown[]) => Disposable
  getSessionManager: (...args: unknown[]) => unknown
}

// ─── Plugin Sandboxing ──────────────────────────────────────────────────────

/** Tool name patterns blocked by each permission category */
const FS_TOOL_PATTERNS =
  /\b(read_file|write_file|create_file|delete_file|edit|glob|ls|apply_patch|multiedit)\b/i
const SHELL_TOOL_PATTERNS = /\b(bash|shell|terminal|exec)\b/i
const NETWORK_TOOL_PATTERNS = /\b(websearch|webfetch|fetch|http|request)\b/i

/**
 * Wrap an ExtensionAPI to enforce permission-based sandboxing.
 * Tools that don't match the plugin's declared permissions are silently blocked.
 */
function createSandboxedApi(
  api: ExtensionAPI,
  pluginName: string,
  permissions: PluginPermission[]
): ExtensionAPI {
  const permSet = new Set(permissions)

  function isToolBlocked(toolName: string): string | null {
    if (!permSet.has('fs') && FS_TOOL_PATTERNS.test(toolName)) {
      return `Plugin "${pluginName}" lacks 'fs' permission — blocked tool "${toolName}"`
    }
    if (!permSet.has('shell') && SHELL_TOOL_PATTERNS.test(toolName)) {
      return `Plugin "${pluginName}" lacks 'shell' permission — blocked tool "${toolName}"`
    }
    if (!permSet.has('network') && NETWORK_TOOL_PATTERNS.test(toolName)) {
      return `Plugin "${pluginName}" lacks 'network' permission — blocked tool "${toolName}"`
    }
    return null
  }

  return {
    ...api,
    registerTool(tool) {
      const toolName = tool.definition?.name ?? ''
      const reason = isToolBlocked(toolName)
      if (reason) {
        console.warn(`[plugin-sandbox] ${reason}`)
        return { dispose() {} }
      }
      return api.registerTool(tool)
    },
    registerCommand: api.registerCommand.bind(api),
    registerAgentMode: api.registerAgentMode.bind(api),
    registerValidator: api.registerValidator.bind(api),
    registerContextStrategy: api.registerContextStrategy.bind(api),
    registerProvider: api.registerProvider.bind(api),
    addToolMiddleware: api.addToolMiddleware.bind(api),
    on: api.on.bind(api),
    emit: api.emit.bind(api),
    getSettings: api.getSettings.bind(api),
    onSettingsChanged: api.onSettingsChanged.bind(api),
    getSessionManager: api.getSessionManager.bind(api),
  }
}

// ─── Plugin Directory Watcher ────────────────────────────────────────────────

/**
 * Watch a plugin directory for file changes and trigger reload.
 * Uses Tauri FS watch with 500ms debounce.
 * Returns a cleanup function that stops watching.
 */
export function watchPluginDirectory(
  pluginPath: string,
  reloadFn: () => Promise<void>
): () => void {
  let debounceTimer: ReturnType<typeof setTimeout> | null = null
  let stopWatch: (() => void) | null = null

  const debouncedReload = () => {
    if (debounceTimer) clearTimeout(debounceTimer)
    debounceTimer = setTimeout(() => {
      void reloadFn()
    }, 500)
  }

  // Start watching asynchronously
  void (async () => {
    try {
      const { watch } = await import('@tauri-apps/plugin-fs')
      const unwatch = await watch(pluginPath, debouncedReload, { recursive: true })
      stopWatch = () => {
        void unwatch()
      }
    } catch (err) {
      console.warn(`[extension-loader] Failed to watch ${pluginPath}:`, err)
    }
  })()

  return () => {
    if (debounceTimer) clearTimeout(debounceTimer)
    if (stopWatch) stopWatch()
  }
}

// ─── Dynamic Plugin Loader ──────────────────────────────────────────────────

/**
 * Load and activate community plugins from ~/.ava/plugins/.
 * Reads the main JS file, creates a Blob URL, and imports it.
 * Applies permission-based sandboxing from the plugin manifest.
 * Returns a cleanup function and a map of plugin disposables for individual management.
 */
export async function loadInstalledPlugins(
  createApi: (name: string) => ExtensionAPI
): Promise<{ cleanup: () => void; pluginDisposables: Map<string, Disposable> }> {
  const pluginDisposables = new Map<string, Disposable>()

  try {
    const { listInstalledPlugins, readPluginSource, readPluginManifest, loadPluginsState } =
      await import('./plugins-fs')
    const state = await loadPluginsState()
    const installed = await listInstalledPlugins()

    for (const name of installed) {
      const pluginState = state[name]
      if (!pluginState?.enabled) continue

      try {
        const source = await readPluginSource(name)
        if (!source) {
          console.warn(`[extension-loader] No source found for plugin: ${name}`)
          continue
        }

        // Read manifest to get declared permissions
        const manifest: PluginManifest | null = await readPluginManifest(name)
        const permissions: PluginPermission[] = manifest?.permissions ?? []

        // Create Blob URL and dynamically import
        const blob = new Blob([source], { type: 'application/javascript' })
        const blobUrl = URL.createObjectURL(blob)

        try {
          const mod = (await import(/* @vite-ignore */ blobUrl)) as {
            activate?: (
              api: ExtensionAPI
            ) => Disposable | undefined | Promise<Disposable | undefined>
          }

          if (typeof mod.activate === 'function') {
            const baseApi = createApi(`plugin:${name}`)
            const api = createSandboxedApi(baseApi, name, permissions)
            const disposable = await mod.activate(api)
            if (disposable) {
              pluginDisposables.set(name, disposable)
            }
            console.info(
              `[extension-loader] Activated plugin: ${name} (permissions: ${permissions.length > 0 ? permissions.join(', ') : 'none'})`
            )
          } else {
            console.warn(`[extension-loader] Plugin ${name} has no activate() export`)
          }
        } finally {
          URL.revokeObjectURL(blobUrl)
        }
      } catch (err) {
        console.warn(`[extension-loader] Failed to load plugin ${name}:`, err)
      }
    }
  } catch (err) {
    console.warn('[extension-loader] Failed to scan for installed plugins:', err)
  }

  const cleanup = () => {
    for (const [, d] of pluginDisposables) {
      try {
        d.dispose()
      } catch {
        // ignore
      }
    }
    pluginDisposables.clear()
  }

  return { cleanup, pluginDisposables }
}

/**
 * Reload a single plugin by name.
 * Disposes the existing instance, re-reads source, and re-activates.
 */
export async function reloadPlugin(
  name: string,
  pluginDisposables: Map<string, Disposable>,
  createApi: (name: string) => ExtensionAPI
): Promise<void> {
  // Dispose existing
  const existing = pluginDisposables.get(name)
  if (existing) {
    try {
      existing.dispose()
    } catch {
      // ignore dispose errors
    }
    pluginDisposables.delete(name)
  }

  const { readPluginSource, readPluginManifest } = await import('./plugins-fs')
  const source = await readPluginSource(name)
  if (!source) throw new Error(`No source found for plugin: ${name}`)

  const manifest: PluginManifest | null = await readPluginManifest(name)
  const permissions: PluginPermission[] = manifest?.permissions ?? []

  const blob = new Blob([source], { type: 'application/javascript' })
  const blobUrl = URL.createObjectURL(blob)

  try {
    const mod = (await import(/* @vite-ignore */ blobUrl)) as {
      activate?: (api: ExtensionAPI) => Disposable | undefined | Promise<Disposable | undefined>
    }

    if (typeof mod.activate === 'function') {
      const baseApi = createApi(`plugin:${name}`)
      const api = createSandboxedApi(baseApi, name, permissions)
      const disposable = await mod.activate(api)
      if (disposable) pluginDisposables.set(name, disposable)
    }
  } finally {
    URL.revokeObjectURL(blobUrl)
  }
}
