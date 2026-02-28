/**
 * Trusted Folders Tab
 *
 * Allow/deny directory list with manual path entry and glob support.
 */

import { FolderOpen, Plus, ShieldCheck, ShieldX, X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'

export const TrustedFoldersTab: Component = () => {
  const { settings, updateSettings } = useSettings()
  const [newAllowed, setNewAllowed] = createSignal('')
  const [newDenied, setNewDenied] = createSignal('')

  const addAllowed = () => {
    const path = newAllowed().trim()
    if (!path) return
    const current = settings().trustedFolders
    if (current.allowed.includes(path)) return
    updateSettings({ trustedFolders: { ...current, allowed: [...current.allowed, path] } })
    setNewAllowed('')
  }

  const addDenied = () => {
    const path = newDenied().trim()
    if (!path) return
    const current = settings().trustedFolders
    if (current.denied.includes(path)) return
    updateSettings({ trustedFolders: { ...current, denied: [...current.denied, path] } })
    setNewDenied('')
  }

  const removeAllowed = (path: string) => {
    const current = settings().trustedFolders
    updateSettings({
      trustedFolders: { ...current, allowed: current.allowed.filter((p) => p !== path) },
    })
  }

  const removeDenied = (path: string) => {
    const current = settings().trustedFolders
    updateSettings({
      trustedFolders: { ...current, denied: current.denied.filter((p) => p !== path) },
    })
  }

  return (
    <div class="space-y-6">
      <div>
        <h2 class="text-base font-semibold text-[var(--text-primary)] mb-1">Trusted Folders</h2>
        <p class="text-xs text-[var(--text-muted)]">
          Control which directories the agent can access. Supports glob patterns (e.g.,
          /home/user/projects/*).
        </p>
      </div>

      {/* Allowed */}
      <div class="p-4 rounded-[var(--radius-lg)] border border-[var(--border-subtle)] bg-[var(--surface-raised)]">
        <div class="flex items-center gap-2 mb-3">
          <ShieldCheck class="w-4 h-4 text-[var(--success)]" />
          <h3 class="text-sm font-medium text-[var(--text-primary)]">Allowed Directories</h3>
        </div>
        <p class="text-[11px] text-[var(--text-muted)] mb-3">
          The agent can read and write in these directories.
        </p>

        <div class="space-y-1.5 mb-3">
          <For each={settings().trustedFolders.allowed}>
            {(path) => (
              <div class="flex items-center gap-2 px-2.5 py-1.5 rounded-[var(--radius-md)] bg-[var(--surface-sunken)] group">
                <FolderOpen class="w-3.5 h-3.5 text-[var(--success)] flex-shrink-0" />
                <span class="text-xs text-[var(--text-secondary)] flex-1 truncate font-mono">
                  {path}
                </span>
                <button
                  type="button"
                  onClick={() => removeAllowed(path)}
                  class="p-0.5 rounded text-[var(--text-muted)] hover:text-[var(--error)] opacity-0 group-hover:opacity-100 transition-opacity"
                >
                  <X class="w-3 h-3" />
                </button>
              </div>
            )}
          </For>
          <Show when={settings().trustedFolders.allowed.length === 0}>
            <p class="text-[11px] text-[var(--text-muted)] italic py-2">
              No allowed directories configured.
            </p>
          </Show>
        </div>

        <div class="flex gap-1.5">
          <input
            type="text"
            placeholder="/path/to/directory or glob..."
            value={newAllowed()}
            onInput={(e) => setNewAllowed(e.currentTarget.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') addAllowed()
            }}
            class="flex-1 px-2.5 py-1.5 text-xs bg-[var(--input-background)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-primary)] placeholder:text-[var(--text-muted)] focus-glow font-mono"
          />
          <button
            type="button"
            onClick={addAllowed}
            class="px-2.5 py-1.5 text-xs bg-[var(--success)] text-white rounded-[var(--radius-md)] hover:opacity-90 transition-opacity flex items-center gap-1"
          >
            <Plus class="w-3 h-3" /> Add
          </button>
        </div>
      </div>

      {/* Denied */}
      <div class="p-4 rounded-[var(--radius-lg)] border border-[var(--border-subtle)] bg-[var(--surface-raised)]">
        <div class="flex items-center gap-2 mb-3">
          <ShieldX class="w-4 h-4 text-[var(--error)]" />
          <h3 class="text-sm font-medium text-[var(--text-primary)]">Denied Directories</h3>
        </div>
        <p class="text-[11px] text-[var(--text-muted)] mb-3">
          The agent will be blocked from accessing these directories.
        </p>

        <div class="space-y-1.5 mb-3">
          <For each={settings().trustedFolders.denied}>
            {(path) => (
              <div class="flex items-center gap-2 px-2.5 py-1.5 rounded-[var(--radius-md)] bg-[var(--surface-sunken)] group">
                <FolderOpen class="w-3.5 h-3.5 text-[var(--error)] flex-shrink-0" />
                <span class="text-xs text-[var(--text-secondary)] flex-1 truncate font-mono">
                  {path}
                </span>
                <button
                  type="button"
                  onClick={() => removeDenied(path)}
                  class="p-0.5 rounded text-[var(--text-muted)] hover:text-[var(--error)] opacity-0 group-hover:opacity-100 transition-opacity"
                >
                  <X class="w-3 h-3" />
                </button>
              </div>
            )}
          </For>
          <Show when={settings().trustedFolders.denied.length === 0}>
            <p class="text-[11px] text-[var(--text-muted)] italic py-2">
              No denied directories configured.
            </p>
          </Show>
        </div>

        <div class="flex gap-1.5">
          <input
            type="text"
            placeholder="/path/to/sensitive or glob..."
            value={newDenied()}
            onInput={(e) => setNewDenied(e.currentTarget.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') addDenied()
            }}
            class="flex-1 px-2.5 py-1.5 text-xs bg-[var(--input-background)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-primary)] placeholder:text-[var(--text-muted)] focus-glow font-mono"
          />
          <button
            type="button"
            onClick={addDenied}
            class="px-2.5 py-1.5 text-xs bg-[var(--error)] text-white rounded-[var(--radius-md)] hover:opacity-90 transition-opacity flex items-center gap-1"
          >
            <Plus class="w-3 h-3" /> Add
          </button>
        </div>
      </div>
    </div>
  )
}
