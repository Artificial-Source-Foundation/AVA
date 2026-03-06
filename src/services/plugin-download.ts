/**
 * Plugin Download & Reload
 *
 * Download plugin bundles from URLs and hot-reload installed plugins.
 * Extracted from plugins-fs.ts for size constraints.
 */

import type { PluginManifest } from '../types/plugin'
import { logInfo, logWarn } from './logger'

// ============================================================================
// Download
// ============================================================================

/** Download plugin from URL, extract, and write to the target directory. */
export async function downloadPlugin(
  url: string,
  dir: string,
  name: string,
  getTauriFs: () => Promise<typeof import('@tauri-apps/plugin-fs')>
): Promise<void> {
  const fs = await getTauriFs()

  try {
    await fs.mkdir(dir, { recursive: true })
  } catch {
    // exists
  }

  const response = await fetch(url)
  if (!response.ok) {
    throw new Error(`Failed to download plugin: ${response.status} ${response.statusText}`)
  }

  const contentType = response.headers.get('content-type') || ''

  if (contentType.includes('application/json') || url.endsWith('.json')) {
    const text = await response.text()
    await fs.writeTextFile(`${dir}/plugin.json`, text)
    logInfo('plugins-fs', `Downloaded plugin ${name} as JSON bundle`)
  } else {
    // JS file or unknown — store as JS with a minimal manifest
    const code = await response.text()
    await fs.writeTextFile(`${dir}/index.js`, code)
    const manifest: PluginManifest = { name, version: '0.0.0', main: 'index.js' }
    await fs.writeTextFile(`${dir}/manifest.json`, JSON.stringify(manifest, null, 2))
    const hint =
      contentType.includes('application/javascript') || url.endsWith('.js')
        ? 'JS file'
        : 'unknown type, treating as JS'
    logInfo('plugins-fs', `Downloaded plugin ${name} (${hint})`)
  }
}

// ============================================================================
// Hot Reload
// ============================================================================

/**
 * Reload a plugin by deactivating and re-activating it.
 * Re-reads source from disk, creates a new Blob URL, and re-imports.
 */
export async function reloadPlugin(
  pluginId: string,
  currentDisposable: { dispose(): void } | undefined,
  createApi: (name: string) => {
    registerTool: (...args: unknown[]) => { dispose(): void }
    [key: string]: unknown
  },
  readSource: (name: string) => Promise<string | null>
): Promise<{ dispose(): void } | null> {
  if (currentDisposable) {
    try {
      currentDisposable.dispose()
    } catch (err) {
      logWarn('plugins-fs', `Error disposing plugin ${pluginId} during reload`, err)
    }
  }

  const source = await readSource(pluginId)
  if (!source) {
    logWarn('plugins-fs', `No source found for plugin ${pluginId} during reload`)
    return null
  }

  const blob = new Blob([source], { type: 'application/javascript' })
  const blobUrl = URL.createObjectURL(blob)

  try {
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
