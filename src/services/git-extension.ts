/**
 * Git Extension Service
 *
 * Install extensions from Git repos, link local dev directories,
 * update, uninstall, and list git-installed extensions.
 * Uses Tauri FS + fetch APIs.
 */

import { invoke, isTauri } from '@tauri-apps/api/core'
import type { PluginManifest } from '../types/plugin'
import { logInfo, logWarn } from './logger'
import { fetchAndExtractTarball } from './tarball'

// ============================================================================
// Helpers
// ============================================================================

async function getTauriFs() {
  return import('@tauri-apps/plugin-fs')
}

async function getPluginsRootDir(): Promise<string> {
  return invoke<string>('get_global_plugins_dir')
}

async function ensurePluginsDir(): Promise<string> {
  const fs = await getTauriFs()
  const dir = await getPluginsRootDir()
  try {
    await fs.mkdir(dir, { recursive: true })
  } catch {
    // Already exists
  }
  return dir
}

/** Extract owner/repo from a GitHub URL */
function parseGitHubUrl(repoUrl: string): { owner: string; repo: string } | null {
  // Match github.com/owner/repo patterns
  const match = repoUrl.match(/github\.com[/:]([^/]+)\/([^/.]+?)(?:\.git)?$/)
  if (!match) return null
  return { owner: match[1], repo: match[2] }
}

/** Derive a plugin name from a repo URL */
function pluginNameFromUrl(repoUrl: string): string {
  const parsed = parseGitHubUrl(repoUrl)
  if (parsed) return parsed.repo
  // Fallback: last path segment
  const segments = repoUrl.split('/').filter(Boolean)
  const last = segments[segments.length - 1] || 'unknown-plugin'
  return last.replace(/\.git$/, '')
}

// ============================================================================
// Git metadata tracking
// ============================================================================

interface GitExtensionMeta {
  name: string
  source: string
  version: string
  installedAt: number
  sourceType: 'git' | 'local-link'
  localPath?: string
}

const GIT_META_FILE = 'git-extensions.json'

async function loadGitMeta(): Promise<Record<string, GitExtensionMeta>> {
  if (!isTauri()) return {}
  try {
    const fs = await getTauriFs()
    const base = await ensurePluginsDir()
    const text = await fs.readTextFile(`${base}/${GIT_META_FILE}`)
    return JSON.parse(text) as Record<string, GitExtensionMeta>
  } catch {
    return {}
  }
}

async function saveGitMeta(meta: Record<string, GitExtensionMeta>): Promise<void> {
  if (!isTauri()) return
  const fs = await getTauriFs()
  const base = await ensurePluginsDir()
  await fs.writeTextFile(`${base}/${GIT_META_FILE}`, JSON.stringify(meta, null, 2))
}

// ============================================================================
// Public API
// ============================================================================

/**
 * Clone (install) an extension from a GitHub repo URL.
 * Fetches the tarball, extracts to the global XDG plugin dir.
 */
export async function cloneExtension(repoUrl: string): Promise<{ name: string; path: string }> {
  if (!isTauri()) {
    throw new Error('Git extension install requires Tauri runtime')
  }

  const name = pluginNameFromUrl(repoUrl)
  const base = await ensurePluginsDir()
  const pluginPath = `${base}/${name}`

  // Clean existing directory if present
  const fs = await getTauriFs()
  try {
    await fs.remove(pluginPath, { recursive: true })
  } catch {
    // Doesn't exist yet
  }
  await fs.mkdir(pluginPath, { recursive: true })

  // Build tarball URL
  const parsed = parseGitHubUrl(repoUrl)
  const tarballUrl = parsed
    ? `https://api.github.com/repos/${parsed.owner}/${parsed.repo}/tarball`
    : `${repoUrl}/archive/refs/heads/main.tar.gz`

  logInfo('git-extension', `Cloning ${repoUrl} to ${pluginPath}`)
  await fetchAndExtractTarball(tarballUrl, pluginPath, fs)

  // Read manifest to get version
  let version = '0.0.0'
  try {
    const manifestText = await fs.readTextFile(`${pluginPath}/manifest.json`)
    const manifest = JSON.parse(manifestText) as PluginManifest
    version = manifest.version || version
  } catch {
    // Try package.json as fallback
    try {
      const pkgText = await fs.readTextFile(`${pluginPath}/package.json`)
      const pkg = JSON.parse(pkgText) as { version?: string }
      version = pkg.version || version
    } catch {
      // No version info
    }
  }

  // Save git metadata
  const meta = await loadGitMeta()
  meta[name] = {
    name,
    source: repoUrl,
    version,
    installedAt: Date.now(),
    sourceType: 'git',
  }
  await saveGitMeta(meta)

  logInfo('git-extension', `Installed ${name} v${version} from ${repoUrl}`)
  return { name, path: pluginPath }
}

/**
 * Update an extension by re-fetching the latest tarball.
 * Returns true if the version changed.
 */
export async function updateExtension(name: string): Promise<boolean> {
  if (!isTauri()) return false

  const meta = await loadGitMeta()
  const entry = meta[name]
  if (!entry || entry.sourceType !== 'git') {
    throw new Error(`Extension '${name}' is not a git-installed extension`)
  }

  const previousVersion = entry.version
  const result = await cloneExtension(entry.source)

  // Re-read meta to check new version
  const updatedMeta = await loadGitMeta()
  const newVersion = updatedMeta[result.name]?.version || '0.0.0'

  logInfo('git-extension', `Updated ${name}: ${previousVersion} -> ${newVersion}`)
  return newVersion !== previousVersion
}

/**
 * Link a local directory into the global XDG plugin dir via symlink.
 * Useful for plugin developers.
 */
export async function linkLocalExtension(localPath: string): Promise<{ name: string }> {
  if (!isTauri()) {
    throw new Error('Linking local extensions requires Tauri runtime')
  }

  const fs = await getTauriFs()
  const { basename } = await import('@tauri-apps/api/path')

  // Derive name from the directory
  const name = await basename(localPath)
  const base = await ensurePluginsDir()
  const linkPath = `${base}/${name}`

  // Remove existing if present
  try {
    await fs.remove(linkPath, { recursive: true })
  } catch {
    // Doesn't exist
  }

  // Create symlink (Tauri plugin-fs supports symlink via shell fallback)
  const { Command } = await import('@tauri-apps/plugin-shell')
  const cmd = Command.create('symlink', ['-s', localPath, linkPath])
  const output = await cmd.execute()
  if (output.code !== 0) {
    // Fallback: try ln -s
    const lnCmd = Command.create('ln', ['-s', localPath, linkPath])
    const lnOutput = await lnCmd.execute()
    if (lnOutput.code !== 0) {
      throw new Error(`Failed to create symlink: ${lnOutput.stderr}`)
    }
  }

  // Read version from linked directory
  let version = '0.0.0'
  try {
    const manifestText = await fs.readTextFile(`${linkPath}/manifest.json`)
    const manifest = JSON.parse(manifestText) as PluginManifest
    version = manifest.version || version
  } catch {
    // No manifest
  }

  // Save metadata
  const meta = await loadGitMeta()
  meta[name] = {
    name,
    source: localPath,
    version,
    installedAt: Date.now(),
    sourceType: 'local-link',
    localPath,
  }
  await saveGitMeta(meta)

  logInfo('git-extension', `Linked local extension: ${name} -> ${localPath}`)
  return { name }
}

/**
 * Uninstall a git-installed or linked extension.
 */
export async function uninstallGitExtension(name: string): Promise<void> {
  if (!isTauri()) return

  const fs = await getTauriFs()
  const base = await ensurePluginsDir()
  const pluginPath = `${base}/${name}`

  try {
    await fs.remove(pluginPath, { recursive: true })
  } catch (err) {
    logWarn('git-extension', `Failed to remove ${name}`, err)
  }

  // Remove metadata
  const meta = await loadGitMeta()
  delete meta[name]
  await saveGitMeta(meta)

  logInfo('git-extension', `Uninstalled git extension: ${name}`)
}

/**
 * List all git-installed and linked extensions.
 */
export async function listGitExtensions(): Promise<
  { name: string; source: string; version: string; sourceType: 'git' | 'local-link' }[]
> {
  const meta = await loadGitMeta()
  return Object.values(meta).map((entry) => ({
    name: entry.name,
    source: entry.source,
    version: entry.version,
    sourceType: entry.sourceType,
  }))
}
