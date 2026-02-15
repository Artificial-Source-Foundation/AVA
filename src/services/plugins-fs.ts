import { invoke, isTauri } from '@tauri-apps/api/core'
import type { PluginState } from '../stores/plugins'
import { logWarn } from './logger'

const STORAGE_KEY = 'ava_plugins_state'

type PluginStateMap = Record<string, PluginState>

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

export async function savePluginsState(state: PluginStateMap): Promise<void> {
  if (!isTauri()) {
    writeLocalState(state)
    return
  }

  try {
    await invoke('set_plugins_state', { state })
  } catch (err) {
    logWarn('plugins-fs', 'Failed to persist plugin state in tauri, writing local fallback', err)
    writeLocalState(state)
  }
}
