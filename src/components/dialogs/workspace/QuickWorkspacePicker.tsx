/**
 * Quick Workspace Picker
 *
 * Compact dropdown for the sidebar/header that shows recent workspaces
 * and a link to open the full workspace selector dialog.
 */

import { Check, Folder, GitBranch, Plus } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { Workspace } from '../WorkspaceSelectorDialog'

export interface QuickWorkspacePickerProps {
  currentWorkspace?: Workspace
  recentWorkspaces: Workspace[]
  onSelect: (workspace: Workspace) => void
  onOpenFull: () => void
  class?: string
}

export const QuickWorkspacePicker: Component<QuickWorkspacePickerProps> = (props) => {
  const [isOpen, setIsOpen] = createSignal(false)

  return (
    <div class={`relative ${props.class ?? ''}`}>
      <button
        type="button"
        onClick={() => setIsOpen(!isOpen())}
        class="
          flex items-center gap-2 w-full
          px-3 py-2
          bg-[var(--surface-raised)]
          hover:bg-[var(--surface-sunken)]
          border border-[var(--border-subtle)]
          rounded-[var(--radius-lg)]
          text-left
          transition-colors duration-[var(--duration-fast)]
        "
      >
        <Folder class="w-4 h-4 text-[var(--text-muted)]" />
        <span class="flex-1 text-sm font-medium text-[var(--text-primary)] truncate">
          {props.currentWorkspace?.name ?? 'No workspace'}
        </span>
        <Show when={props.currentWorkspace?.gitBranch}>
          <span class="flex items-center gap-1 text-xs text-[var(--text-muted)]">
            <GitBranch class="w-3 h-3" />
            {props.currentWorkspace!.gitBranch}
          </span>
        </Show>
      </button>

      {/* Dropdown */}
      <Show when={isOpen()}>
        <div
          class="
            absolute top-full left-0 right-0 mt-1
            bg-[var(--surface-overlay)]
            border border-[var(--border-default)]
            rounded-[var(--radius-lg)]
            shadow-lg
            z-50
            overflow-hidden
          "
        >
          <div class="max-h-60 overflow-y-auto py-1">
            <For each={props.recentWorkspaces.slice(0, 5)}>
              {(workspace) => (
                <button
                  type="button"
                  onClick={() => {
                    props.onSelect(workspace)
                    setIsOpen(false)
                  }}
                  class={`
                    w-full flex items-center gap-3 px-3 py-2
                    hover:bg-[var(--surface-raised)]
                    text-left
                    transition-colors duration-[var(--duration-fast)]
                    ${
                      props.currentWorkspace?.id === workspace.id ? 'bg-[var(--accent-subtle)]' : ''
                    }
                  `}
                >
                  <Folder class="w-4 h-4 text-[var(--text-muted)]" />
                  <div class="flex-1 min-w-0">
                    <div class="text-sm text-[var(--text-primary)] truncate">{workspace.name}</div>
                    <div class="text-xs text-[var(--text-muted)] truncate">{workspace.path}</div>
                  </div>
                  <Show when={props.currentWorkspace?.id === workspace.id}>
                    <Check class="w-4 h-4 text-[var(--accent)]" />
                  </Show>
                </button>
              )}
            </For>
          </div>
          <div class="border-t border-[var(--border-subtle)] p-2">
            <button
              type="button"
              onClick={() => {
                props.onOpenFull()
                setIsOpen(false)
              }}
              class="
                w-full flex items-center justify-center gap-2
                px-3 py-2
                text-sm text-[var(--accent)]
                hover:bg-[var(--accent-subtle)]
                rounded-[var(--radius-md)]
                transition-colors duration-[var(--duration-fast)]
              "
            >
              <Plus class="w-4 h-4" />
              More workspaces...
            </button>
          </div>
        </div>
      </Show>

      {/* Click outside to close */}
      <Show when={isOpen()}>
        {/* biome-ignore lint/a11y/useKeyWithClickEvents: click-outside-to-close backdrop */}
        {/* biome-ignore lint/a11y/noStaticElementInteractions: backdrop overlay element */}
        <div class="fixed inset-0 z-40" onClick={() => setIsOpen(false)} />
      </Show>
    </div>
  )
}
