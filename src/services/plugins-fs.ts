/**
 * Plugin Filesystem Service
 *
 * Real plugin lifecycle operations using Tauri FS APIs.
 * Falls back to localStorage for non-Tauri environments (dev/test).
 */

import { isTauri } from '@tauri-apps/api/core'
import type { PluginManifest, PluginState } from '../types/plugin'
import { logInfo, logWarn } from './logger'

const STORAGE_KEY = 'ava_plugins_state'
const PLUGINS_DIR = '.ava/plugins'
const STATE_FILE = `${PLUGINS_DIR}/state.json`

type PluginStateMap = Record<string, PluginState>

// ============================================================================
// Local Storage Fallback (non-Tauri)
// ============================================================================

function readLocalState(): PluginStateMap {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (!raw) return {}
    return JSON.parse(raw) as PluginStateMap
  } catch {
    return {}
  }
}

function writeLocalState(state: PluginStateMap) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(state))
}

// ============================================================================
// Tauri FS Helpers
// ============================================================================

async function getTauriFs() {
  const fs = await import('@tauri-apps/plugin-fs')
  return fs
}

async function getHomeDir(): Promise<string> {
  const { homeDir } = await import('@tauri-apps/api/path')
  return homeDir()
}

async function ensurePluginsDir(): Promise<string> {
  const fs = await getTauriFs()
  const home = await getHomeDir()
  const dir = `${home}${PLUGINS_DIR}`
  try {
    await fs.mkdir(dir, { recursive: true })
  } catch {
    // Already exists
  }
  return dir
}

async function pluginDir(name: string): Promise<string> {
  const base = await ensurePluginsDir()
  return `${base}/${name}`
}

// ============================================================================
// Plugin State Persistence
// ============================================================================

export async function loadPluginsState(): Promise<PluginStateMap> {
  if (!isTauri()) return readLocalState()

  try {
    const fs = await getTauriFs()
    const home = await getHomeDir()
    const path = `${home}${STATE_FILE}`
    const text = await fs.readTextFile(path)
    return JSON.parse(text) as PluginStateMap
  } catch {
    return readLocalState()
  }
}

async function savePluginsState(state: PluginStateMap): Promise<void> {
  if (!isTauri()) {
    writeLocalState(state)
    return
  }

  try {
    const fs = await getTauriFs()
    await ensurePluginsDir()
    const home = await getHomeDir()
    const path = `${home}${STATE_FILE}`
    await fs.writeTextFile(path, JSON.stringify(state, null, 2))
  } catch (err) {
    logWarn('plugins-fs', 'Failed to save state to FS, using localStorage', err)
    writeLocalState(state)
  }
}

// ============================================================================
// Plugin Download & Installation
// ============================================================================

/** Download plugin from URL, extract, and write to ~/.ava/plugins/<name>/ */
export async function downloadPlugin(url: string, name: string): Promise<string> {
  const dir = await pluginDir(name)
  const fs = await getTauriFs()

  try {
    await fs.mkdir(dir, { recursive: true })
  } catch {
    // exists
  }

  // Fetch the plugin bundle
  const response = await fetch(url)
  if (!response.ok) {
    throw new Error(`Failed to download plugin: ${response.status} ${response.statusText}`)
  }

  const contentType = response.headers.get('content-type') || ''

  if (contentType.includes('application/json') || url.endsWith('.json')) {
    // Single JSON manifest+code bundle
    const text = await response.text()
    await fs.writeTextFile(`${dir}/plugin.json`, text)
    logInfo('plugins-fs', `Downloaded plugin ${name} as JSON bundle`)
  } else if (contentType.includes('application/javascript') || url.endsWith('.js')) {
    // Single JS file — create a minimal manifest
    const code = await response.text()
    await fs.writeTextFile(`${dir}/index.js`, code)
    const manifest: PluginManifest = { name, version: '0.0.0', main: 'index.js' }
    await fs.writeTextFile(`${dir}/manifest.json`, JSON.stringify(manifest, null, 2))
    logInfo('plugins-fs', `Downloaded plugin ${name} as JS file`)
  } else {
    // Try as JS regardless (best effort)
    const code = await response.text()
    await fs.writeTextFile(`${dir}/index.js`, code)
    const manifest: PluginManifest = { name, version: '0.0.0', main: 'index.js' }
    await fs.writeTextFile(`${dir}/manifest.json`, JSON.stringify(manifest, null, 2))
    logInfo('plugins-fs', `Downloaded plugin ${name} (unknown type, treating as JS)`)
  }

  return dir
}

/** Remove a plugin's directory */
export async function removePlugin(name: string): Promise<void> {
  if (!isTauri()) return

  try {
    const fs = await getTauriFs()
    const dir = await pluginDir(name)
    await fs.remove(dir, { recursive: true })
    logInfo('plugins-fs', `Removed plugin directory: ${name}`)
  } catch (err) {
    logWarn('plugins-fs', `Failed to remove plugin ${name}`, err)
  }
}

/** Read the manifest.json from a plugin directory */
export async function readPluginManifest(name: string): Promise<PluginManifest | null> {
  if (!isTauri()) return null

  try {
    const fs = await getTauriFs()
    const dir = await pluginDir(name)
    const text = await fs.readTextFile(`${dir}/manifest.json`)
    return JSON.parse(text) as PluginManifest
  } catch {
    return null
  }
}

/** Scan ~/.ava/plugins/ for installed plugins with manifest files */
export async function listInstalledPlugins(): Promise<string[]> {
  if (!isTauri()) return []

  try {
    const fs = await getTauriFs()
    const base = await ensurePluginsDir()
    const entries = await fs.readDir(base)
    const names: string[] = []

    for (const entry of entries) {
      if (!entry.isDirectory) continue
      try {
        await fs.readTextFile(`${base}/${entry.name}/manifest.json`)
        names.push(entry.name)
      } catch {
        // No manifest, skip
      }
    }

    return names
  } catch {
    return []
  }
}

/** Read the main JS source of a plugin */
export async function readPluginSource(name: string): Promise<string | null> {
  if (!isTauri()) return null

  try {
    const fs = await getTauriFs()
    const manifest = await readPluginManifest(name)
    if (!manifest) return null
    const dir = await pluginDir(name)
    return await fs.readTextFile(`${dir}/${manifest.main}`)
  } catch {
    return null
  }
}

// ============================================================================
// Plugin Reload (Hot Reload)
// ============================================================================

/**
 * Reload a plugin by deactivating and re-activating it.
 * Re-reads source from disk, creates a new Blob URL, and re-imports.
 * Returns the new Disposable or null on failure.
 */
export async function reloadPlugin(
  pluginId: string,
  currentDisposable: { dispose(): void } | undefined,
  createApi: (name: string) => {
    registerTool: (...args: unknown[]) => { dispose(): void }
    [key: string]: unknown
  }
): Promise<{ dispose(): void } | null> {
  // 1. Dispose current instance
  if (currentDisposable) {
    try {
      currentDisposable.dispose()
    } catch (err) {
      logWarn('plugins-fs', `Error disposing plugin ${pluginId} during reload`, err)
    }
  }

  // 2. Re-read source from disk
  const source = await readPluginSource(pluginId)
  if (!source) {
    logWarn('plugins-fs', `No source found for plugin ${pluginId} during reload`)
    return null
  }

  // 3. Create Blob URL and re-import
  const blob = new Blob([source], { type: 'application/javascript' })
  const blobUrl = URL.createObjectURL(blob)

  try {
    // Add cache-busting query param to force re-import
    const mod = (await import(/* @vite-ignore */ `${blobUrl}?t=${Date.now()}`)) as {
      activate?: (
        api: unknown
      ) => { dispose(): void } | undefined | Promise<{ dispose(): void } | undefined>
    }

    if (typeof mod.activate === 'function') {
      const api = createApi(`plugin:${pluginId}`)
      const disposable = await mod.activate(api)
      logInfo('plugins-fs', `Reloaded plugin: ${pluginId}`)
      return disposable ?? null
    }

    logWarn('plugins-fs', `Plugin ${pluginId} has no activate() export after reload`)
    return null
  } finally {
    URL.revokeObjectURL(blobUrl)
  }
}

// ============================================================================
// Public API (used by plugin store)
// ============================================================================

export async function installPlugin(pluginId: string, downloadUrl?: string): Promise<PluginState> {
  const state = await loadPluginsState()

  if (isTauri() && downloadUrl) {
    const installPath = await downloadPlugin(downloadUrl, pluginId)
    const manifest = await readPluginManifest(pluginId)
    const next: PluginState = {
      installed: true,
      enabled: true,
      version: manifest?.version,
      installedAt: Date.now(),
      installPath,
    }
    await savePluginsState({ ...state, [pluginId]: next })
    return next
  }

  // Fallback: state-only install (no download URL or non-Tauri)
  const next: PluginState = { installed: true, enabled: true, installedAt: Date.now() }
  const updated = { ...state, [pluginId]: next }
  await savePluginsState(updated)
  return next
}

export async function uninstallPlugin(pluginId: string): Promise<PluginState> {
  const state = await loadPluginsState()

  if (isTauri()) {
    await removePlugin(pluginId)
  }

  const next = { ...state }
  delete next[pluginId]
  await savePluginsState(next)
  return { installed: false, enabled: false }
}

export async function setPluginEnabled(pluginId: string, enabled: boolean): Promise<PluginState> {
  const state = await loadPluginsState()
  const current = state[pluginId]
  if (!current?.installed) {
    throw new Error(`Plugin '${pluginId}' must be installed before enabling/disabling.`)
  }

  const next = { ...current, enabled }
  await savePluginsState({ ...state, [pluginId]: next })
  return next
}
