/**
 * Plugin Install Dialogs
 *
 * Modal dialogs for installing plugins from Git, linking local directories,
 * and confirming sensitive permission grants.
 */

import { AlertTriangle, FolderSymlink, GitBranch, Shield } from 'lucide-solid'
import { type Accessor, type Component, createSignal, For, Show } from 'solid-js'
import { usePlugins } from '../../../../stores/plugins'
import {
  PLUGIN_PERMISSION_META,
  type PluginCatalogItem,
  type PluginPermission,
} from '../../../../types/plugin'
import { permissionColor } from './plugin-utils'

// ---------------------------------------------------------------------------
// Git Install Dialog
// ---------------------------------------------------------------------------

export interface GitInstallDialogProps {
  open: Accessor<boolean>
  onClose: () => void
}

export const GitInstallDialog: Component<GitInstallDialogProps> = (props) => {
  const plugins = usePlugins()
  const [gitUrl, setGitUrl] = createSignal('')
  const [gitInstalling, setGitInstalling] = createSignal(false)
  const [gitError, setGitError] = createSignal<string | null>(null)

  const handleGitInstall = async (): Promise<void> => {
    const url = gitUrl().trim()
    if (!url) return

    setGitInstalling(true)
    setGitError(null)

    try {
      await plugins.installFromGit(url)
      setGitUrl('')
      props.onClose()
    } catch (err) {
      setGitError(err instanceof Error ? err.message : 'Failed to install from git.')
    } finally {
      setGitInstalling(false)
    }
  }

  return (
    <Show when={props.open()}>
      <div
        class="fixed inset-0 z-50 flex items-center justify-center"
        style={{ background: 'var(--modal-overlay)' }}
      >
        <div
          class="p-6 max-w-md w-full space-y-4"
          style={{
            background: 'var(--modal-surface)',
            border: '1px solid var(--modal-border)',
            'border-radius': 'var(--modal-radius-sm)',
            'box-shadow': 'var(--modal-shadow)',
          }}
        >
          <div class="flex items-center gap-2">
            <GitBranch class="w-4 h-4 text-[var(--accent)]" />
            <h3 class="text-sm font-semibold text-[var(--text-primary)]">Install from Git</h3>
          </div>
          <p class="text-xs text-[var(--text-secondary)]">
            Enter a GitHub repository URL to install an extension.
          </p>
          <input
            type="text"
            placeholder="https://github.com/owner/repo"
            value={gitUrl()}
            onInput={(e) => setGitUrl(e.currentTarget.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') void handleGitInstall()
              if (e.key === 'Escape') props.onClose()
            }}
            class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
            autofocus
          />
          <Show when={gitError()}>
            <p class="text-[var(--settings-text-badge)] text-[var(--error)]">{gitError()}</p>
          </Show>
          <div class="flex gap-2 justify-end">
            <button
              type="button"
              onClick={() => props.onClose()}
              class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
            >
              Cancel
            </button>
            <button
              type="button"
              onClick={() => void handleGitInstall()}
              disabled={!gitUrl().trim() || gitInstalling()}
              class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:bg-[var(--accent-hover)] transition-colors disabled:opacity-50"
            >
              {gitInstalling() ? 'Installing...' : 'Install'}
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}

// ---------------------------------------------------------------------------
// Link Local Dialog
// ---------------------------------------------------------------------------

export interface LinkLocalDialogProps {
  open: Accessor<boolean>
  onClose: () => void
}

export const LinkLocalDialog: Component<LinkLocalDialogProps> = (props) => {
  const plugins = usePlugins()
  const [linkPath, setLinkPath] = createSignal('')
  const [linkInstalling, setLinkInstalling] = createSignal(false)
  const [linkError, setLinkError] = createSignal<string | null>(null)

  const handleLinkLocal = async (): Promise<void> => {
    const path = linkPath().trim()
    if (!path) return

    setLinkInstalling(true)
    setLinkError(null)

    try {
      await plugins.linkLocal(path)
      setLinkPath('')
      props.onClose()
    } catch (err) {
      setLinkError(err instanceof Error ? err.message : 'Failed to link local extension.')
    } finally {
      setLinkInstalling(false)
    }
  }

  return (
    <Show when={props.open()}>
      <div
        class="fixed inset-0 z-50 flex items-center justify-center"
        style={{ background: 'var(--modal-overlay)' }}
      >
        <div
          class="p-6 max-w-md w-full space-y-4"
          style={{
            background: 'var(--modal-surface)',
            border: '1px solid var(--modal-border)',
            'border-radius': 'var(--modal-radius-sm)',
            'box-shadow': 'var(--modal-shadow)',
          }}
        >
          <div class="flex items-center gap-2">
            <FolderSymlink class="w-4 h-4 text-[var(--accent)]" />
            <h3 class="text-sm font-semibold text-[var(--text-primary)]">Link Local Extension</h3>
          </div>
          <p class="text-xs text-[var(--text-secondary)]">
            Enter the absolute path to your local plugin directory. A symlink will be created.
          </p>
          <input
            type="text"
            placeholder="/path/to/my-plugin"
            value={linkPath()}
            onInput={(e) => setLinkPath(e.currentTarget.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') void handleLinkLocal()
              if (e.key === 'Escape') props.onClose()
            }}
            class="w-full px-3 py-2 text-sm rounded-[var(--radius-md)] bg-[var(--surface-sunken)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
            autofocus
          />
          <Show when={linkError()}>
            <p class="text-[var(--settings-text-badge)] text-[var(--error)]">{linkError()}</p>
          </Show>
          <div class="flex gap-2 justify-end">
            <button
              type="button"
              onClick={() => props.onClose()}
              class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
            >
              Cancel
            </button>
            <button
              type="button"
              onClick={() => void handleLinkLocal()}
              disabled={!linkPath().trim() || linkInstalling()}
              class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:bg-[var(--accent-hover)] transition-colors disabled:opacity-50"
            >
              {linkInstalling() ? 'Linking...' : 'Link'}
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}

// ---------------------------------------------------------------------------
// Permission Confirmation Dialog
// ---------------------------------------------------------------------------

export interface PermissionConfirmDialogProps {
  plugin: Accessor<PluginCatalogItem | null>
  onCancel: () => void
  onConfirm: () => void
}

export const PermissionConfirmDialog: Component<PermissionConfirmDialogProps> = (props) => {
  return (
    <Show when={props.plugin()}>
      {(plugin) => (
        <div
          class="fixed inset-0 z-50 flex items-center justify-center"
          style={{ background: 'var(--modal-overlay)' }}
        >
          <div
            class="p-6 max-w-md w-full space-y-4"
            style={{
              background: 'var(--modal-surface)',
              border: '1px solid var(--modal-border)',
              'border-radius': 'var(--modal-radius-sm)',
              'box-shadow': 'var(--modal-shadow)',
            }}
          >
            <div class="flex items-center gap-2">
              <AlertTriangle class="w-4 h-4 text-[var(--warning)]" />
              <h3 class="text-sm font-semibold text-[var(--text-primary)]">
                Sensitive Permissions Required
              </h3>
            </div>
            <p class="text-xs text-[var(--text-secondary)]">
              <strong>{plugin().name}</strong> requests the following permissions:
            </p>
            <div class="flex flex-wrap gap-1.5">
              <For each={plugin().permissions ?? []}>
                {(perm) => {
                  const meta = PLUGIN_PERMISSION_META[perm as PluginPermission]
                  return (
                    <span
                      class="inline-flex items-center gap-1 px-2 py-1 text-[var(--settings-text-badge)] font-medium rounded-full border"
                      style={{
                        color: permissionColor(perm as PluginPermission),
                        'border-color': permissionColor(perm as PluginPermission),
                        'background-color': `color-mix(in srgb, ${permissionColor(perm as PluginPermission)} 10%, transparent)`,
                      }}
                    >
                      <Shield class="w-2.5 h-2.5" />
                      {meta?.label ?? perm}
                    </span>
                  )
                }}
              </For>
            </div>
            <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)]">
              This plugin can access sensitive system resources. Only install plugins from sources
              you trust.
            </p>
            <div class="flex gap-2 justify-end">
              <button
                type="button"
                onClick={props.onCancel}
                class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
              >
                Cancel
              </button>
              <button
                type="button"
                onClick={props.onConfirm}
                class="px-3 py-1.5 text-xs font-medium bg-[var(--warning)] text-white rounded-[var(--radius-md)] hover:bg-[color-mix(in_srgb,var(--warning)_90%,white_10%)] transition-colors"
              >
                Install Anyway
              </button>
            </div>
          </div>
        </div>
      )}
    </Show>
  )
}
