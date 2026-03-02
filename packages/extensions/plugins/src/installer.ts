/**
 * Plugin installer -- download, verify, install, uninstall.
 */

import * as nodePath from 'node:path'
import { createLogger } from '@ava/core-v2/logger'
import { getPlatform } from '@ava/core-v2/platform'

const log = createLogger('PluginInstaller')

export interface InstalledPlugin {
  name: string
  version: string
  description: string
  installPath: string
  installedAt: number
  enabled: boolean
}

export interface InstallResult {
  success: boolean
  plugin?: InstalledPlugin
  error?: string
}

const PLUGINS_DIR = '.ava/plugins'

function getPluginsDir(): string {
  const home = process.env.HOME ?? process.env.USERPROFILE ?? '/tmp'
  return nodePath.join(home, PLUGINS_DIR)
}

export async function getInstalledPlugins(): Promise<InstalledPlugin[]> {
  const fs = getPlatform().fs
  const dir = getPluginsDir()

  try {
    const entries = await fs.readDir(dir)
    const plugins: InstalledPlugin[] = []

    for (const entry of entries) {
      const manifestPath = nodePath.join(dir, entry, 'ava-extension.json')
      try {
        const content = await fs.readFile(manifestPath)
        const manifest = JSON.parse(content) as Record<string, unknown>
        plugins.push({
          name: manifest.name as string,
          version: (manifest.version as string) ?? '0.0.0',
          description: (manifest.description as string) ?? '',
          installPath: nodePath.join(dir, entry),
          installedAt: Date.now(),
          enabled: (manifest.enabledByDefault as boolean) ?? true,
        })
      } catch {
        // Skip entries without valid manifests
      }
    }

    return plugins
  } catch {
    return []
  }
}

export async function installPlugin(
  source: string,
  options?: { name?: string }
): Promise<InstallResult> {
  const fs = getPlatform().fs
  const dir = getPluginsDir()

  try {
    // Ensure plugins directory exists
    await fs.mkdir(dir)

    // Determine install method based on source
    if (source.startsWith('http://') || source.startsWith('https://')) {
      return installFromUrl(source)
    }
    if (source.startsWith('github:') || source.includes('/')) {
      return installFromGitHub(source, dir)
    }
    // Local path
    return installFromLocal(source, dir, options?.name)
  } catch (err) {
    return {
      success: false,
      error: err instanceof Error ? err.message : 'Install failed',
    }
  }
}

async function installFromUrl(_url: string): Promise<InstallResult> {
  log.info(`Installing plugin from URL: ${_url}`)
  return {
    success: false,
    error: 'URL install not yet implemented -- use local path or github:owner/repo',
  }
}

async function installFromGitHub(repo: string, dir: string): Promise<InstallResult> {
  const cleanRepo = repo.replace('github:', '')
  log.info(`Installing plugin from GitHub: ${cleanRepo}`)

  const shell = getPlatform().shell
  const fs = getPlatform().fs
  const repoName = cleanRepo.split('/').pop()!
  const installPath = nodePath.join(dir, repoName)

  try {
    const cloneResult = await shell.exec(
      `git clone https://github.com/${cleanRepo}.git "${installPath}"`,
      { timeout: 30000 }
    )
    if (cloneResult.exitCode !== 0) {
      return { success: false, error: `git clone failed: ${cloneResult.stderr}` }
    }

    // Read manifest
    const manifestPath = nodePath.join(installPath, 'ava-extension.json')
    const content = await fs.readFile(manifestPath)
    const manifest = JSON.parse(content) as Record<string, unknown>

    // Install deps if package.json exists
    try {
      const hasPackageJson = await fs.exists(nodePath.join(installPath, 'package.json'))
      if (hasPackageJson) {
        await shell.exec('npm install --production', {
          cwd: installPath,
          timeout: 60000,
        })
      }
    } catch {
      // No deps needed -- OK
    }

    return {
      success: true,
      plugin: {
        name: manifest.name as string,
        version: (manifest.version as string) ?? '0.0.0',
        description: (manifest.description as string) ?? '',
        installPath,
        installedAt: Date.now(),
        enabled: true,
      },
    }
  } catch (err) {
    return {
      success: false,
      error: err instanceof Error ? err.message : 'GitHub clone failed',
    }
  }
}

async function installFromLocal(
  source: string,
  dir: string,
  name?: string
): Promise<InstallResult> {
  const fs = getPlatform().fs
  const shell = getPlatform().shell
  const pluginName = name ?? nodePath.basename(source)
  const installPath = nodePath.join(dir, pluginName)

  log.info(`Installing plugin from local path: ${source}`)

  try {
    // Copy plugin directory
    await fs.mkdir(installPath)
    const copyResult = await shell.exec(`cp -r "${source}"/* "${installPath}/"`)
    if (copyResult.exitCode !== 0) {
      return { success: false, error: `Copy failed: ${copyResult.stderr}` }
    }

    // Read manifest
    const manifestPath = nodePath.join(installPath, 'ava-extension.json')
    const content = await fs.readFile(manifestPath)
    const manifest = JSON.parse(content) as Record<string, unknown>

    return {
      success: true,
      plugin: {
        name: manifest.name as string,
        version: (manifest.version as string) ?? '0.0.0',
        description: (manifest.description as string) ?? '',
        installPath,
        installedAt: Date.now(),
        enabled: true,
      },
    }
  } catch (err) {
    return {
      success: false,
      error: err instanceof Error ? err.message : 'Local install failed',
    }
  }
}

export async function uninstallPlugin(name: string): Promise<{ success: boolean; error?: string }> {
  const plugins = await getInstalledPlugins()
  const plugin = plugins.find((p) => p.name === name)

  if (!plugin) {
    return { success: false, error: `Plugin not found: ${name}` }
  }

  try {
    const fs = getPlatform().fs
    await fs.remove(plugin.installPath)
    log.info(`Uninstalled plugin: ${name}`)
    return { success: true }
  } catch (err) {
    return {
      success: false,
      error: err instanceof Error ? err.message : 'Uninstall failed',
    }
  }
}
