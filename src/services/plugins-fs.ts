import { invoke, isTauri } from '@tauri-apps/api/core'
import type { PluginState } from '../types/plugin'
import { logWarn } from './logger'

const STORAGE_KEY = 'ava_plugins_state'

type PluginStateMap = Record<string, PluginState>

function assertPluginInstalled(pluginId: string, state: PluginStateMap): PluginState {
  const current = state[pluginId] ?? { installed: false, enabled: false }
  if (!current.installed) {
    throw new Error(`Plugin '${pluginId}' must be installed before enabling/disabling.`)
  }
  return current
}

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

export async function loadPluginsState(): Promise<PluginStateMap> {
  if (!isTauri()) {
    return readLocalState()
  }

  try {
    const result = await invoke<PluginStateMap>('get_plugins_state')
    return result ?? {}
  } catch (err) {
    logWarn(
      'plugins-fs',
      'Failed to load plugin state from tauri, falling back to localStorage',
      err
    )
    return readLocalState()
  }
}

export async function installPlugin(pluginId: string): Promise<PluginState> {
  if (!isTauri()) {
    const state = readLocalState()
    const next = { installed: true, enabled: true }
    writeLocalState({ ...state, [pluginId]: next })
    return next
  }

  return invoke<PluginState>('install_plugin', { pluginId })
}

export async function uninstallPlugin(pluginId: string): Promise<PluginState> {
  if (!isTauri()) {
    const state = readLocalState()
    assertPluginInstalled(pluginId, state)
    const next = { ...state }
    delete next[pluginId]
    writeLocalState(next)
    return { installed: false, enabled: false }
  }

  return invoke<PluginState>('uninstall_plugin', { pluginId })
}

export async function setPluginEnabled(pluginId: string, enabled: boolean): Promise<PluginState> {
  if (!isTauri()) {
    const state = readLocalState()
    assertPluginInstalled(pluginId, state)
    const next = { installed: true, enabled }
    writeLocalState({ ...state, [pluginId]: next })
    return next
  }

  return invoke<PluginState>('set_plugin_enabled', { pluginId, enabled })
}
