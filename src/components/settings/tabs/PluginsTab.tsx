/**
 * Plugins Tab — Pencil design revamp
 *
 * Single card with header (icon + title + description + Create Plugin button),
 * search bar, status line, and plugin rows with Installed/Install badges.
 */

import { Package, Plus, Search } from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  onCleanup,
  Show,
} from 'solid-js'
import { watchPluginDirectory } from '../../../services/plugin-loader'
import { rustBackend } from '../../../services/rust-bridge'
import { usePlugins } from '../../../stores/plugins'
import { type PluginSortBy, sortPlugins } from '../../../stores/plugins-catalog'
import {
  type PluginMountRegistration,
  type PluginPermission,
  SENSITIVE_PERMISSIONS,
} from '../../../types/plugin'
import { PluginDetailPanel } from '../../plugins'
import { PluginWizard } from '../../plugins/PluginWizard'
import { PublishDialog } from '../../plugins/PublishDialog'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import {
  type DevModeStatus,
  formatSyncTime,
  GitInstallDialog,
  LinkLocalDialog,
  PermissionConfirmDialog,
  PluginDevMode,
} from './plugins-tab'

export const PluginsTab: Component = () => {
  const plugins = usePlugins()
  const [selectedPluginId, setSelectedPluginId] = createSignal<string | null>(null)
  const [devModePlugins, setDevModePlugins] = createSignal<Record<string, boolean>>({})
  const [devModeStatus, setDevModeStatus] = createSignal<Record<string, DevModeStatus>>({})
  const [devModeLogs, setDevModeLogs] = createSignal<Record<string, string[]>>({})
  const [pluginMounts, setPluginMounts] = createSignal<Record<string, PluginMountRegistration[]>>(
    {}
  )
  const watchers = new Map<string, () => void>()

  // Dialog visibility
  const [showGitDialog, setShowGitDialog] = createSignal(false)
  const [showLinkDialog, setShowLinkDialog] = createSignal(false)
  const [showPublish, setShowPublish] = createSignal(false)
  const [showWizard, setShowWizard] = createSignal(false)
  const [sortBy] = createSignal<PluginSortBy>('popular')

  // Permission confirmation
  const [permConfirmPluginId, setPermConfirmPluginId] = createSignal<string | null>(null)
  const permConfirmPlugin = createMemo(() => {
    const id = permConfirmPluginId()
    if (!id) return null
    return plugins.plugins.find((p) => p.id === id) ?? null
  })

  const handleInstallWithPermCheck = (pluginId: string): void => {
    const plugin = plugins.plugins.find((p) => p.id === pluginId)
    const perms = plugin?.permissions ?? []
    const hasSensitive = perms.some((p) => SENSITIVE_PERMISSIONS.includes(p as PluginPermission))
    if (hasSensitive) {
      setPermConfirmPluginId(pluginId)
    } else {
      plugins.clearError(pluginId)
      void plugins.install(pluginId)
    }
  }

  const confirmInstallWithPerms = (): void => {
    const id = permConfirmPluginId()
    if (!id) return
    setPermConfirmPluginId(null)
    plugins.clearError(id)
    void plugins.install(id)
  }

  // Dev-mode helpers
  const appendLog = (pluginId: string, message: string): void => {
    setDevModeLogs((prev) => {
      const current = prev[pluginId] ?? []
      const timestamp = new Date().toLocaleTimeString([], {
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
      })
      return { ...prev, [pluginId]: [...current, `[${timestamp}] ${message}`].slice(-50) }
    })
  }

  const toggleDevMode = (pluginId: string): void => {
    const isOn = devModePlugins()[pluginId]
    if (isOn) {
      const cleanup = watchers.get(pluginId)
      if (cleanup) {
        cleanup()
        watchers.delete(pluginId)
      }
      setDevModePlugins((prev) => ({ ...prev, [pluginId]: false }))
      setDevModeStatus((prev) => ({ ...prev, [pluginId]: 'idle' }))
      appendLog(pluginId, 'Dev mode disabled')
    } else {
      const state = plugins.pluginState()[pluginId]
      if (!state?.installPath) {
        appendLog(pluginId, 'No install path — cannot watch')
        return
      }

      setDevModePlugins((prev) => ({ ...prev, [pluginId]: true }))
      setDevModeStatus((prev) => ({ ...prev, [pluginId]: 'watching' }))
      appendLog(pluginId, `Watching ${state.installPath}`)

      const cleanup = watchPluginDirectory(state.installPath, async () => {
        setDevModeStatus((prev) => ({ ...prev, [pluginId]: 'reloading' }))
        appendLog(pluginId, 'File change detected, reloading...')
        try {
          await plugins.refresh()
          appendLog(pluginId, 'Reload complete')
        } catch (err) {
          appendLog(
            pluginId,
            `Reload failed: ${err instanceof Error ? err.message : 'Unknown error'}`
          )
        } finally {
          setDevModeStatus((prev) => ({ ...prev, [pluginId]: 'watching' }))
        }
      })
      watchers.set(pluginId, cleanup)
    }
  }

  onCleanup(() => {
    for (const cleanup of watchers.values()) cleanup()
    watchers.clear()
  })

  const refreshPluginMounts = async (): Promise<void> => {
    try {
      const mounts = await rustBackend.listPluginMounts()
      const grouped = mounts.reduce<Record<string, PluginMountRegistration[]>>((acc, mount) => {
        if (!acc[mount.plugin]) {
          acc[mount.plugin] = []
        }
        acc[mount.plugin].push(mount)
        return acc
      }, {})
      setPluginMounts(grouped)
    } catch {
      setPluginMounts({})
    }
  }

  createEffect(() => {
    plugins.pluginState()
    void refreshPluginMounts()
  })

  // Derived state
  const selectedPlugin = createMemo(() => {
    const id = selectedPluginId()
    if (!id) return null
    return plugins.plugins.find((plugin) => plugin.id === id) ?? null
  })

  const selectedState = createMemo(() => {
    const id = selectedPluginId()
    if (!id) return null
    return plugins.pluginState()[id] ?? { installed: false, enabled: false }
  })

  const sortedPlugins = createMemo(() => sortPlugins(plugins.filteredPlugins(), sortBy()))
  const selectedMounts = createMemo(() => {
    const id = selectedPluginId()
    if (!id) return []
    return pluginMounts()[id] ?? []
  })

  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: SETTINGS_CARD_GAP }}>
      {/* Page title */}
      <h1
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: '#F5F5F7',
        }}
      >
        Plugins
      </h1>

      {/* Plugins Card */}
      <div
        style={{
          background: '#111114',
          border: '1px solid #ffffff08',
          'border-radius': '12px',
          padding: '20px',
          display: 'flex',
          'flex-direction': 'column',
          gap: '16px',
        }}
      >
        {/* Card header */}
        <div
          style={{
            display: 'flex',
            'align-items': 'center',
            'justify-content': 'space-between',
          }}
        >
          <div style={{ display: 'flex', 'align-items': 'center', gap: '10px' }}>
            <Package size={16} style={{ color: '#C8C8CC' }} />
            <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '14px',
                  'font-weight': '500',
                  color: '#F5F5F7',
                }}
              >
                Plugins
              </span>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  color: '#48484A',
                }}
              >
                Extend AVA with community and custom plugins
              </span>
            </div>
          </div>
          <button
            type="button"
            onClick={() => setShowWizard(true)}
            style={{
              display: 'flex',
              'align-items': 'center',
              gap: '6px',
              padding: '6px 12px',
              background: '#0A84FF',
              'border-radius': '8px',
              border: 'none',
              cursor: 'pointer',
            }}
          >
            <Plus size={12} style={{ color: '#FFFFFF' }} />
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                'font-weight': '500',
                color: '#FFFFFF',
              }}
            >
              Create Plugin
            </span>
          </button>
        </div>

        {/* Search bar */}
        <div
          style={{
            display: 'flex',
            'align-items': 'center',
            gap: '8px',
            padding: '8px 12px',
            background: '#ffffff08',
            border: '1px solid #ffffff0a',
            'border-radius': '8px',
          }}
        >
          <Search size={12} style={{ color: '#48484A', 'flex-shrink': '0' }} />
          <input
            type="text"
            placeholder="Search plugins..."
            value={plugins.search()}
            onInput={(e) => plugins.setSearch(e.currentTarget.value)}
            style={{
              background: 'transparent',
              border: 'none',
              outline: 'none',
              'font-family': 'Geist, sans-serif',
              'font-size': '12px',
              color: '#F5F5F7',
              width: '100%',
            }}
          />
        </div>

        {/* Status line */}
        <span
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '11px',
            color: '#48484A',
          }}
        >
          Status: {plugins.catalogStatus()} &middot; Last sync:{' '}
          {formatSyncTime(plugins.lastCatalogSyncAt())}
        </span>

        <Show when={plugins.catalogError()}>
          <span
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '11px',
              color: '#FF453A',
            }}
          >
            {plugins.catalogError()}
          </span>
        </Show>

        {/* Plugin rows */}
        <Show
          when={sortedPlugins().length > 0}
          fallback={
            <div
              style={{
                display: 'flex',
                'flex-direction': 'column',
                'align-items': 'center',
                'justify-content': 'center',
                padding: '40px 0',
                'text-align': 'center',
              }}
            >
              <Package size={32} style={{ color: '#48484A', 'margin-bottom': '8px' }} />
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '13px',
                  color: '#C8C8CC',
                  'margin-bottom': '4px',
                }}
              >
                No plugins found
              </span>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  color: '#48484A',
                  'max-width': '280px',
                  'margin-bottom': '12px',
                }}
              >
                Plugins add capabilities to AVA. Create your first plugin to get started.
              </span>
              <button
                type="button"
                onClick={() => setShowWizard(true)}
                style={{
                  display: 'flex',
                  'align-items': 'center',
                  gap: '6px',
                  padding: '6px 12px',
                  background: '#0A84FF',
                  'border-radius': '8px',
                  border: 'none',
                  cursor: 'pointer',
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  'font-weight': '500',
                  color: '#FFFFFF',
                }}
              >
                Create your first plugin
              </button>
            </div>
          }
        >
          <For each={sortedPlugins()}>
            {(plugin) => {
              const state = () => plugins.pluginState()[plugin.id]
              const isInstalled = () => state()?.installed ?? false
              return (
                // biome-ignore lint/a11y/useKeyWithClickEvents: plugin card selection
                // biome-ignore lint/a11y/useSemanticElements: card-style row
                <div
                  role="button"
                  tabIndex={0}
                  onClick={() => setSelectedPluginId(plugin.id)}
                  style={{
                    display: 'flex',
                    'align-items': 'center',
                    'justify-content': 'space-between',
                    padding: '10px 14px',
                    background: '#ffffff04',
                    border: `1px solid ${selectedPluginId() === plugin.id ? '#0A84FF40' : '#ffffff0a'}`,
                    'border-radius': '8px',
                    cursor: 'pointer',
                    transition: 'border-color 0.15s',
                  }}
                >
                  <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
                    <span
                      style={{
                        'font-family': 'Geist, sans-serif',
                        'font-size': '13px',
                        'font-weight': '500',
                        color: '#F5F5F7',
                      }}
                    >
                      {plugin.name}
                    </span>
                    <span
                      style={{
                        'font-family': 'Geist, sans-serif',
                        'font-size': '11px',
                        color: '#48484A',
                      }}
                    >
                      {plugin.description}
                    </span>
                  </div>
                  <Show
                    when={isInstalled()}
                    fallback={
                      <button
                        type="button"
                        onClick={(e) => {
                          e.stopPropagation()
                          handleInstallWithPermCheck(plugin.id)
                        }}
                        style={{
                          padding: '4px 10px',
                          background: '#0A84FF',
                          'border-radius': '6px',
                          border: 'none',
                          cursor: 'pointer',
                          'font-family': 'Geist, sans-serif',
                          'font-size': '10px',
                          'font-weight': '500',
                          color: '#FFFFFF',
                        }}
                      >
                        Install
                      </button>
                    }
                  >
                    <span
                      style={{
                        padding: '3px 8px',
                        background: '#34C75920',
                        'border-radius': '6px',
                        'font-family': 'Geist, sans-serif',
                        'font-size': '10px',
                        'font-weight': '500',
                        color: '#34C759',
                      }}
                    >
                      Installed
                    </span>
                  </Show>
                </div>
              )
            }}
          </For>
        </Show>
      </div>

      {/* Plugin Details card */}
      <Show when={selectedPlugin()}>
        <div
          style={{
            background: '#111114',
            border: '1px solid #ffffff08',
            'border-radius': '12px',
            padding: '20px',
            display: 'flex',
            'flex-direction': 'column',
            gap: '16px',
          }}
        >
          <span
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '14px',
              'font-weight': '500',
              color: '#F5F5F7',
            }}
          >
            Plugin Details
          </span>
          <PluginDetailPanel
            plugin={selectedPlugin()}
            state={selectedState()}
            mounts={selectedMounts()}
          />
        </div>
      </Show>

      {/* Developer Mode card */}
      <Show when={selectedPlugin() && selectedState()?.installed}>
        {(_) => {
          const pluginId = () => selectedPluginId()!
          return (
            <div
              style={{
                background: '#111114',
                border: '1px solid #ffffff08',
                'border-radius': '12px',
                padding: '20px',
                display: 'flex',
                'flex-direction': 'column',
                gap: '16px',
              }}
            >
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '14px',
                  'font-weight': '500',
                  color: '#F5F5F7',
                }}
              >
                Developer Mode
              </span>
              <PluginDevMode
                pluginId={pluginId}
                isDevMode={() => devModePlugins()[pluginId()] ?? false}
                status={() => devModeStatus()[pluginId()] ?? 'idle'}
                logs={() => devModeLogs()[pluginId()] ?? []}
                onToggle={toggleDevMode}
              />
            </div>
          )
        }}
      </Show>

      {/* Dialogs */}
      <GitInstallDialog open={showGitDialog} onClose={() => setShowGitDialog(false)} />
      <LinkLocalDialog open={showLinkDialog} onClose={() => setShowLinkDialog(false)} />
      <PermissionConfirmDialog
        plugin={permConfirmPlugin}
        onCancel={() => setPermConfirmPluginId(null)}
        onConfirm={confirmInstallWithPerms}
      />
      <PublishDialog open={showPublish()} onClose={() => setShowPublish(false)} />
      <PluginWizard open={showWizard()} onClose={() => setShowWizard(false)} />
    </div>
  )
}
