/**
 * Plugin Lifecycle Operations
 * Async operations for install, uninstall, toggle, retry, recover, and git/local extensions.
 */

import {
  cloneExtension,
  linkLocalExtension,
  listGitExtensions,
  uninstallGitExtension,
} from '../services/git-extension'
import { installPlugin, setPluginEnabled, uninstallPlugin } from '../services/plugins-fs'
import type { PluginState } from '../types/plugin'
import { PLUGIN_CATALOG } from './plugins-catalog'

export type PluginAction = 'install' | 'uninstall' | 'toggle'

export interface PluginsStoreInternals {
  pluginState: () => Record<string, PluginState>
  pendingActions: () => Record<string, PluginAction | null>
  failedActionsByPlugin: () => Record<string, PluginAction | null>
  setState: (id: string, next: PluginState) => void
  clearState: (id: string) => void
  setPendingAction: (id: string, action: PluginAction | null) => void
  setError: (id: string, message: string) => void
  clearError: (id: string) => void
  setFailedAction: (id: string, action: PluginAction | null) => void
  runLifecycle: <T>(operation: () => Promise<T>) => Promise<T>
}

export async function install(ctx: PluginsStoreInternals, id: string): Promise<void> {
  if (ctx.pendingActions()[id]) return

  const previous = ctx.pluginState()[id]
  ctx.clearError(id)
  ctx.setPendingAction(id, 'install')
  ctx.setState(id, { installed: true, enabled: true })

  const catalogItem = PLUGIN_CATALOG.find((p) => p.id === id)
  const downloadUrl = catalogItem?.downloadUrl

  try {
    const next = await ctx.runLifecycle(() => installPlugin(id, downloadUrl))
    ctx.setState(id, next)
    ctx.setFailedAction(id, null)
  } catch (error) {
    if (previous) {
      ctx.setState(id, previous)
    } else {
      ctx.clearState(id)
    }
    ctx.setFailedAction(id, 'install')
    ctx.setError(id, error instanceof Error ? error.message : 'Failed to install plugin.')
  } finally {
    ctx.setPendingAction(id, null)
  }
}

export async function uninstall(ctx: PluginsStoreInternals, id: string): Promise<void> {
  if (ctx.pendingActions()[id]) return

  const previous = ctx.pluginState()[id] ?? { installed: false, enabled: false }
  if (!previous.installed) {
    ctx.setError(id, 'Plugin is not installed.')
    return
  }

  ctx.clearError(id)
  ctx.setPendingAction(id, 'uninstall')
  ctx.clearState(id)

  try {
    const next = await ctx.runLifecycle(() => uninstallPlugin(id))
    if (next.installed) {
      ctx.setState(id, next)
    } else {
      ctx.clearState(id)
    }
    ctx.setFailedAction(id, null)
  } catch (error) {
    ctx.setState(id, previous)
    ctx.setFailedAction(id, 'uninstall')
    ctx.setError(id, error instanceof Error ? error.message : 'Failed to uninstall plugin.')
  } finally {
    ctx.setPendingAction(id, null)
  }
}

export async function toggleEnabled(ctx: PluginsStoreInternals, id: string): Promise<void> {
  if (ctx.pendingActions()[id]) return

  const current = ctx.pluginState()[id] ?? { installed: false, enabled: false }
  if (!current.installed) {
    ctx.setError(id, 'Plugin must be installed before enabling or disabling.')
    return
  }

  const optimistic = { ...current, enabled: !current.enabled }
  ctx.clearError(id)
  ctx.setPendingAction(id, 'toggle')
  ctx.setState(id, optimistic)

  try {
    const next = await ctx.runLifecycle(() => setPluginEnabled(id, optimistic.enabled))
    ctx.setState(id, next)
    ctx.setFailedAction(id, null)
  } catch (error) {
    ctx.setState(id, current)
    ctx.setFailedAction(id, 'toggle')
    ctx.setError(id, error instanceof Error ? error.message : 'Failed to update plugin state.')
  } finally {
    ctx.setPendingAction(id, null)
  }
}

export async function retry(ctx: PluginsStoreInternals, id: string): Promise<void> {
  const failedAction = ctx.failedActionsByPlugin()[id]
  if (!failedAction || ctx.pendingActions()[id]) return

  if (failedAction === 'install') {
    await install(ctx, id)
    return
  }
  if (failedAction === 'uninstall') {
    await uninstall(ctx, id)
    return
  }
  await toggleEnabled(ctx, id)
}

export async function recover(ctx: PluginsStoreInternals, id: string): Promise<void> {
  const current = ctx.pluginState()[id] ?? { installed: false, enabled: false }
  if (current.installed) {
    await uninstall(ctx, id)
    return
  }
  ctx.clearError(id)
  ctx.setFailedAction(id, null)
}

export async function installFromGit(
  ctx: PluginsStoreInternals,
  repoUrl: string
): Promise<string | undefined> {
  const tempId = `git:${repoUrl}`
  if (ctx.pendingActions()[tempId]) return undefined

  ctx.clearError(tempId)
  ctx.setPendingAction(tempId, 'install')

  try {
    const result = await ctx.runLifecycle(() => cloneExtension(repoUrl))
    const gitPlugins = await listGitExtensions()
    const meta = gitPlugins.find((p) => p.name === result.name)
    ctx.setState(result.name, {
      installed: true,
      enabled: true,
      version: meta?.version,
      installedAt: Date.now(),
      installPath: result.path,
      sourceType: 'git',
      sourceUrl: repoUrl,
    })
    ctx.setFailedAction(tempId, null)
    ctx.setPendingAction(tempId, null)
    return result.name
  } catch (error) {
    ctx.setFailedAction(tempId, 'install')
    ctx.setError(tempId, error instanceof Error ? error.message : 'Failed to install from git.')
    ctx.setPendingAction(tempId, null)
    throw error
  }
}

export async function linkLocal(
  ctx: PluginsStoreInternals,
  localPath: string
): Promise<string | undefined> {
  const tempId = `local:${localPath}`
  if (ctx.pendingActions()[tempId]) return undefined

  ctx.clearError(tempId)
  ctx.setPendingAction(tempId, 'install')

  try {
    const result = await ctx.runLifecycle(() => linkLocalExtension(localPath))
    const gitPlugins = await listGitExtensions()
    const meta = gitPlugins.find((p) => p.name === result.name)
    ctx.setState(result.name, {
      installed: true,
      enabled: true,
      version: meta?.version,
      installedAt: Date.now(),
      installPath: localPath,
      sourceType: 'local-link',
      sourceUrl: localPath,
    })
    ctx.setFailedAction(tempId, null)
    ctx.setPendingAction(tempId, null)
    return result.name
  } catch (error) {
    ctx.setFailedAction(tempId, 'install')
    ctx.setError(tempId, error instanceof Error ? error.message : 'Failed to link local extension.')
    ctx.setPendingAction(tempId, null)
    throw error
  }
}

export async function uninstallGit(ctx: PluginsStoreInternals, name: string): Promise<void> {
  if (ctx.pendingActions()[name]) return

  ctx.clearError(name)
  ctx.setPendingAction(name, 'uninstall')

  try {
    await ctx.runLifecycle(() => uninstallGitExtension(name))
    ctx.clearState(name)
    ctx.setFailedAction(name, null)
  } catch (error) {
    ctx.setFailedAction(name, 'uninstall')
    ctx.setError(name, error instanceof Error ? error.message : 'Failed to uninstall.')
  } finally {
    ctx.setPendingAction(name, null)
  }
}
