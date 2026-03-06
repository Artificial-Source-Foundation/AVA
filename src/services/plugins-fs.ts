/**
 * Plugin Filesystem Service
 *
 * Real plugin lifecycle operations using Tauri FS APIs.
 * Falls back to localStorage for non-Tauri environments (dev/test).
 * Download and hot-reload logic lives in plugin-download.ts.
 */

import { isTauri } from '@tauri-apps/api/core'
import type { PluginManifest, PluginState } from '../types/plugin'
import { logInfo, logWarn } from './logger'
import {
  downloadPlugin as downloadPluginImpl,
  reloadPlugin as reloadPluginImpl,
} from './plugin-download'

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

function writeLocalState(state: PluginStateMap): void {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(state))
}

// ============================================================================
// Tauri FS Helpers
// ============================================================================

async function getTauriFs(): Promise<typeof import('@tauri-apps/plugin-fs')> {
  return import('@tauri-apps/plugin-fs')
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
// Plugin FS Operations
// ============================================================================

/** Download plugin from URL to ~/.ava/plugins/<name>/ */
export async function downloadPlugin(url: string, name: string): Promise<string> {
  const dir = await pluginDir(name)
  await downloadPluginImpl(url, dir, name, getTauriFs)
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

/** Hot-reload a plugin (deactivate + re-activate from disk) */
export async function reloadPlugin(
  pluginId: string,
  currentDisposable: { dispose(): void } | undefined,
  createApi: (name: string) => {
    registerTool: (...args: unknown[]) => { dispose(): void }
    [key: string]: unknown
  }
): Promise<{ dispose(): void } | null> {
  return reloadPluginImpl(pluginId, currentDisposable, createApi, readPluginSource)
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
  const next: PluginState = { installed: true, enabled: true, installedAt: Date.now() }
  await savePluginsState({ ...state, [pluginId]: next })
  return next
}

export async function uninstallPlugin(pluginId: string): Promise<PluginState> {
  const state = await loadPluginsState()
  if (isTauri()) await removePlugin(pluginId)
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
