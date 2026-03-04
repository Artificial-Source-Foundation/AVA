/**
 * Extension loader — discovers and imports extension modules.
 *
 * Scans directories for `ava-extension.json` manifests, resolves
 * the `main` entry point, and returns loaded modules.
 */

import * as path from 'node:path'
import { createLogger } from '../logger/logger.js'
import { getPlatform } from '../platform.js'
import type { ExtensionManifest, ExtensionModule } from './types.js'

const log = createLogger('ExtensionLoader')

export interface LoadedExtension {
  manifest: ExtensionManifest
  module: ExtensionModule
  path: string
}

/**
 * Discover and load extensions from a directory.
 *
 * Each subdirectory should contain an `ava-extension.json` manifest.
 * When `packageRoot` is provided, compiled `.js` files in `<root>/dist/`
 * are used as a fallback when the source `.ts` import fails (compiled CLI).
 */
export async function loadExtensionsFromDirectory(
  dir: string,
  packageRoot?: string
): Promise<LoadedExtension[]> {
  const platform = getPlatform()
  const loaded: LoadedExtension[] = []

  const exists = await platform.fs.exists(dir)
  if (!exists) {
    log.debug(`Extensions directory not found: ${dir}`)
    return loaded
  }

  const entries = await platform.fs.readDirWithTypes(dir)

  for (const entry of entries) {
    if (!entry.isDirectory) continue

    const extDir = path.join(dir, entry.name)
    const manifestPath = path.join(extDir, 'ava-extension.json')

    try {
      if (!(await platform.fs.exists(manifestPath))) continue

      const manifestRaw = await platform.fs.readFile(manifestPath)
      const manifest: ExtensionManifest = JSON.parse(manifestRaw)

      if (!manifest.name || !manifest.main) {
        log.warn(`Invalid manifest in ${extDir}: missing name or main`)
        continue
      }

      const module = await importExtensionModule(extDir, manifest.main, packageRoot)

      if (typeof module.activate !== 'function') {
        log.warn(`Extension ${manifest.name} has no activate() export`)
        continue
      }

      loaded.push({ manifest, module, path: extDir })
      log.debug(`Extension loaded: ${manifest.name} from ${extDir}`)
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      log.error(`Failed to load extension from ${extDir}`, { error: message })
    }
  }

  return loaded
}

/**
 * Import an extension module, falling back to compiled dist output.
 * tsx can import .ts directly; compiled Node needs the .js from dist/.
 */
async function importExtensionModule(
  extDir: string,
  main: string,
  packageRoot?: string
): Promise<ExtensionModule> {
  const sourcePath = path.join(extDir, main)
  try {
    // Dynamic import required: extension paths are discovered at runtime from
    // user-installed plugin directories. Vite cannot statically analyze these.
    // Safe because: only loads extensions from validated manifest paths.
    return (await import(/* @vite-ignore */ sourcePath)) as ExtensionModule
  } catch {
    // Fall back to compiled output in dist/
    if (packageRoot) {
      const relativePath = path.relative(packageRoot, sourcePath)
      const distPath = path.join(packageRoot, 'dist', relativePath.replace(/\.ts$/, '.js'))
      // Dynamic import required: fallback path derived from runtime source path.
      // Safe because: path is computed from known packageRoot and validated source.
      return (await import(/* @vite-ignore */ distPath)) as ExtensionModule
    }
    throw new Error(`Cannot import extension: ${sourcePath} (no dist fallback available)`)
  }
}

/** Loader function type — matches loadExtensionsFromDirectory signature. */
export type ExtensionDirLoader = (dir: string, packageRoot?: string) => Promise<LoadedExtension[]>

/**
 * Load all built-in extensions from the extensions directory.
 *
 * Handles the nested layout: top-level extension dirs + `providers/*` subdirs.
 * Accepts an optional loader for testability.
 */
export async function loadAllBuiltInExtensions(
  extensionsDir: string,
  loader: ExtensionDirLoader = loadExtensionsFromDirectory
): Promise<LoadedExtension[]> {
  const platform = getPlatform()

  const exists = await platform.fs.exists(extensionsDir)
  if (!exists) {
    log.debug(`Built-in extensions directory not found: ${extensionsDir}`)
    return []
  }

  // Load top-level extensions (permissions, tools-extended, hooks, etc.)
  const topLevel = await loader(extensionsDir, extensionsDir)

  // Load provider extensions from providers/* subdirectory
  const providersDir = path.join(extensionsDir, 'providers')
  const providers = await loader(providersDir, extensionsDir)

  const all = [...topLevel, ...providers]

  // Mark all as built-in
  for (const ext of all) {
    ext.manifest.builtIn = true
  }

  log.debug(
    `Loaded ${all.length} built-in extensions (${topLevel.length} core + ${providers.length} providers)`
  )
  return all
}

/**
 * Load a single extension by manifest and module reference.
 * Used for built-in extensions that are bundled, not loaded from disk.
 */
export function loadBuiltInExtension(
  manifest: ExtensionManifest,
  module: ExtensionModule
): LoadedExtension {
  return {
    manifest: { ...manifest, builtIn: true },
    module,
    path: '<built-in>',
  }
}
