/**
 * Static Extension Loader for Tauri
 *
 * The CLI uses filesystem-based dynamic loading, but the Tauri webview can't
 * use node:fs. Instead, we statically import all built-in extension activate()
 * functions and register them in priority order.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { PluginManifest, PluginPermission } from '../types/plugin'
import { logInfo, logWarn } from './logger'

// ─── Extension Imports (sorted by priority) ─────────────────────────────────

// Priority 10: Core extensions
import { activate as activateAgentModes } from '../../packages/extensions/agent-modes/src/index.js'
import { activate as activateCommander } from '../../packages/extensions/commander/src/index.js'
import { activate as activateContext } from '../../packages/extensions/context/src/index.js'
import { activate as activateDiff } from '../../packages/extensions/diff/src/index.js'
import { activate as activateGit } from '../../packages/extensions/git/src/index.js'
import { activate as activateHooks } from '../../packages/extensions/hooks/src/index.js'
import { activate as activateInstructions } from '../../packages/extensions/instructions/src/index.js'
import { activate as activateLsp } from '../../packages/extensions/lsp/src/index.js'
import { activate as activateMcp } from '../../packages/extensions/mcp/src/index.js'
import { activate as activateMemory } from '../../packages/extensions/memory/src/index.js'
import { activate as activateModels } from '../../packages/extensions/models/src/index.js'
// Priority 0: Security gate
import { activate as activatePermissions } from '../../packages/extensions/permissions/src/index.js'
import { activate as activatePrompts } from '../../packages/extensions/prompts/src/index.js'
// Priority 10: Providers
import { activate as activateAnthropic } from '../../packages/extensions/providers/anthropic/src/index.js'
import { activate as activateCohere } from '../../packages/extensions/providers/cohere/src/index.js'
import { activate as activateCopilot } from '../../packages/extensions/providers/copilot/src/index.js'
import { activate as activateDeepseek } from '../../packages/extensions/providers/deepseek/src/index.js'
import { activate as activateGlm } from '../../packages/extensions/providers/glm/src/index.js'
import { activate as activateGoogle } from '../../packages/extensions/providers/google/src/index.js'
import { activate as activateGroq } from '../../packages/extensions/providers/groq/src/index.js'
import { activate as activateKimi } from '../../packages/extensions/providers/kimi/src/index.js'
import { activate as activateMistral } from '../../packages/extensions/providers/mistral/src/index.js'
import { activate as activateOllama } from '../../packages/extensions/providers/ollama/src/index.js'
import { activate as activateOpenai } from '../../packages/extensions/providers/openai/src/index.js'
import { activate as activateOpenrouter } from '../../packages/extensions/providers/openrouter/src/index.js'
import { activate as activateTogether } from '../../packages/extensions/providers/together/src/index.js'
import { activate as activateXai } from '../../packages/extensions/providers/xai/src/index.js'
import { activate as activateSlashCommands } from '../../packages/extensions/slash-commands/src/index.js'
import { activate as activateToolsExtended } from '../../packages/extensions/tools-extended/src/index.js'
import { activate as activateValidator } from '../../packages/extensions/validator/src/index.js'

// ─── Extension Registry ─────────────────────────────────────────────────────

interface ExtensionEntry {
  name: string
  priority: number
  activate: (api: ExtensionAPI) => Disposable | undefined | Promise<Disposable | undefined>
}

const EXTENSIONS: ExtensionEntry[] = [
  // Priority 0 — security gate (must run first)
  { name: 'permissions', priority: 0, activate: activatePermissions },

  // Priority 10 — core extensions
  { name: 'tools-extended', priority: 10, activate: activateToolsExtended },
  { name: 'hooks', priority: 10, activate: activateHooks },
  { name: 'context', priority: 10, activate: activateContext },
  { name: 'prompts', priority: 10, activate: activatePrompts },
  { name: 'models', priority: 10, activate: activateModels },
  { name: 'diff', priority: 10, activate: activateDiff },
  { name: 'agent-modes', priority: 10, activate: activateAgentModes },
  { name: 'validator', priority: 10, activate: activateValidator },
  { name: 'commander', priority: 10, activate: activateCommander },
  { name: 'slash-commands', priority: 10, activate: activateSlashCommands },
  { name: 'instructions', priority: 10, activate: activateInstructions },
  { name: 'lsp', priority: 10, activate: activateLsp },
  { name: 'git', priority: 10, activate: activateGit },
  { name: 'mcp', priority: 10, activate: activateMcp },
  { name: 'memory', priority: 15, activate: activateMemory },

  // Priority 10 — providers
  { name: 'anthropic', priority: 10, activate: activateAnthropic },
  { name: 'openai', priority: 10, activate: activateOpenai },
  { name: 'openrouter', priority: 10, activate: activateOpenrouter },
  { name: 'google', priority: 10, activate: activateGoogle },
  { name: 'copilot', priority: 10, activate: activateCopilot },
  { name: 'glm', priority: 10, activate: activateGlm },
  { name: 'kimi', priority: 10, activate: activateKimi },
  { name: 'mistral', priority: 10, activate: activateMistral },
  { name: 'groq', priority: 10, activate: activateGroq },
  { name: 'deepseek', priority: 10, activate: activateDeepseek },
  { name: 'xai', priority: 10, activate: activateXai },
  { name: 'cohere', priority: 10, activate: activateCohere },
  { name: 'together', priority: 10, activate: activateTogether },
  { name: 'ollama', priority: 10, activate: activateOllama },
]

export const BUILT_IN_EXTENSION_COUNT = EXTENSIONS.length

// ─── Loader ─────────────────────────────────────────────────────────────────

/**
 * Load and activate all built-in extensions in priority order.
 * Returns a cleanup function that disposes all extensions.
 */
export async function loadAllExtensions(
  createApi: (name: string) => ExtensionAPI
): Promise<() => void> {
  const disposables: Disposable[] = []
  const sorted = [...EXTENSIONS].sort((a, b) => a.priority - b.priority)
  const startedAt = Date.now()
  let failed = 0

  for (const ext of sorted) {
    const extensionStart = Date.now()
    try {
      const api = createApi(ext.name)
      const disposable = await ext.activate(api)
      if (disposable) disposables.push(disposable)
      logInfo('extensions', 'Activated', {
        name: ext.name,
        duration_ms: Date.now() - extensionStart,
      })
    } catch (err) {
      failed += 1
      const error = err instanceof Error ? err.message : String(err)
      logWarn('extensions', 'Failed', {
        name: ext.name,
        error,
      })
      console.warn(`[extension-loader] Failed to activate ${ext.name}:`, err)
    }
  }

  logInfo('extensions', 'Summary', {
    total: sorted.length,
    activated: sorted.length - failed,
    failed,
    duration_ms: Date.now() - startedAt,
  })

  return () => {
    for (const d of disposables) {
      try {
        d.dispose()
      } catch {
        // ignore dispose errors
      }
    }
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
