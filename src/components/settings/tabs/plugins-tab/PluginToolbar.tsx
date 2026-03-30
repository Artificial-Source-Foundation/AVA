/**
 * Plugin Toolbar
 *
 * Header bar with Create, Publish, Git, Link, Refresh, and Sync buttons.
 */

import { FolderSymlink, GitBranch, Puzzle, RefreshCw, Upload } from 'lucide-solid'
import type { Component } from 'solid-js'
import { usePlugins } from '../../../../stores/plugins'
import { SETTINGS_TOOLBAR_BUTTON_CLASS } from '../../settings-constants'

export interface PluginToolbarProps {
  onShowWizard: () => void
  onShowPublish: () => void
  onShowGitDialog: () => void
  onShowLinkDialog: () => void
}

export const PluginToolbar: Component<PluginToolbarProps> = (props) => {
  const plugins = usePlugins()

  return (
    <div class="flex items-center justify-between">
      <span class="text-[var(--settings-text-description)] text-[var(--text-muted)]">
        {plugins.installedCount()} / {plugins.plugins.length} installed
      </span>
      <div class="flex items-center gap-1.5">
        <button
          type="button"
          onClick={() => props.onShowWizard()}
          class={SETTINGS_TOOLBAR_BUTTON_CLASS}
          title="Create a new plugin"
        >
          <Puzzle class="w-3 h-3" />
          Create
        </button>
        <button
          type="button"
          onClick={() => props.onShowPublish()}
          class={SETTINGS_TOOLBAR_BUTTON_CLASS}
          title="Publish a plugin"
        >
          <Upload class="w-3 h-3" />
          Publish
        </button>
        <button
          type="button"
          onClick={() => props.onShowGitDialog()}
          class={SETTINGS_TOOLBAR_BUTTON_CLASS}
          title="Install from Git repository"
        >
          <GitBranch class="w-3 h-3" />
          Git
        </button>
        <button
          type="button"
          onClick={() => props.onShowLinkDialog()}
          class={SETTINGS_TOOLBAR_BUTTON_CLASS}
          title="Link local plugin directory"
        >
          <FolderSymlink class="w-3 h-3" />
          Link
        </button>
        <button
          type="button"
          onClick={() => {
            void plugins.refresh()
          }}
          class="flex items-center gap-1.5 px-2 py-1 text-[var(--settings-text-button)] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)]"
        >
          <RefreshCw class="w-3 h-3" />
          Refresh
        </button>
        <button
          type="button"
          onClick={() => {
            void plugins.syncCatalog()
          }}
          disabled={plugins.catalogStatus() === 'syncing'}
          class="flex items-center gap-1.5 px-2 py-1 text-[var(--settings-text-button)] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] disabled:opacity-50"
        >
          <RefreshCw
            class={`w-3 h-3 ${plugins.catalogStatus() === 'syncing' ? 'animate-spin' : ''}`}
          />
          Sync
        </button>
      </div>
    </div>
  )
}
