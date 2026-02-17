const INSTALLED_PLUGINS_KEY = 'ava_plugins_installed'
const LEGACY_INSTALLED_PLUGINS_KEY = 'estela_plugins_installed'

function readInstalledPluginIds(): string[] {
  const raw =
    localStorage.getItem(INSTALLED_PLUGINS_KEY) ||
    localStorage.getItem(LEGACY_INSTALLED_PLUGINS_KEY)
  if (!raw) {
    return []
  }

  try {
    const parsed: unknown = JSON.parse(raw)
    if (!Array.isArray(parsed)) {
      return []
    }

    return parsed.filter((item): item is string => typeof item === 'string')
  } catch {
    return []
  }
}

function writeInstalledPluginIds(ids: string[]): void {
  const uniqueIds = [...new Set(ids)]
  const serialized = JSON.stringify(uniqueIds)
  localStorage.setItem(INSTALLED_PLUGINS_KEY, serialized)
  localStorage.setItem(LEGACY_INSTALLED_PLUGINS_KEY, serialized)
}

export async function listInstalledPlugins(): Promise<string[]> {
  return Promise.resolve(readInstalledPluginIds())
}

export async function installPlugin(pluginId: string): Promise<void> {
  const current = readInstalledPluginIds()
  writeInstalledPluginIds([...current, pluginId])
}

export async function uninstallPlugin(pluginId: string): Promise<void> {
  const current = readInstalledPluginIds()
  writeInstalledPluginIds(current.filter((id) => id !== pluginId))
}
