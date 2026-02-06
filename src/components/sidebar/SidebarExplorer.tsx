/**
 * Sidebar Explorer View
 *
 * Project file tree browser. Shows the current project's directory structure.
 * Placeholder for Tauri FS integration — displays project info for now.
 */

import { File, FolderOpen, FolderTree } from 'lucide-solid'
import type { Component } from 'solid-js'
import { Show } from 'solid-js'
import { useProject } from '../../stores/project'

export const SidebarExplorer: Component = () => {
  const { currentProject } = useProject()

  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center justify-between px-3 h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
        <span class="text-xs font-semibold text-[var(--text-secondary)] uppercase tracking-wider">
          Explorer
        </span>
      </div>

      <Show
        when={currentProject()}
        fallback={
          <div class="flex-1 flex flex-col items-center justify-center px-4 text-center">
            <FolderOpen class="w-8 h-8 text-[var(--text-muted)] mb-3 opacity-50" />
            <p class="text-xs text-[var(--text-muted)] mb-3">No project open</p>
            <button
              type="button"
              class="
                px-3 py-1.5 text-xs
                bg-[var(--surface-raised)] hover:bg-[var(--alpha-white-10)]
                border border-[var(--border-default)]
                rounded-[var(--radius-md)]
                text-[var(--text-secondary)]
                transition-colors
              "
            >
              Open Folder
            </button>
          </div>
        }
      >
        <div class="flex-1 overflow-y-auto px-1.5 py-1 scrollbar-none">
          {/* Project info */}
          <div class="px-2 py-1.5 mb-1">
            <div class="flex items-center gap-2">
              <FolderTree class="w-3.5 h-3.5 text-[var(--accent)]" />
              <span class="text-xs font-medium text-[var(--text-primary)] truncate">
                {currentProject()?.name}
              </span>
            </div>
            <Show when={currentProject()?.directory}>
              <div class="mt-1 text-[10px] text-[var(--text-muted)] truncate pl-5">
                {currentProject()?.directory}
              </div>
            </Show>
          </div>

          {/* File tree placeholder */}
          <div class="px-2 py-4 text-center">
            <File class="w-5 h-5 mx-auto mb-2 text-[var(--text-muted)] opacity-50" />
            <p class="text-[10px] text-[var(--text-muted)]">File tree will connect to Tauri FS</p>
          </div>
        </div>
      </Show>
    </div>
  )
}
