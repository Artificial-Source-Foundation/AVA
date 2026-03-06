/**
 * Workspace Selector Dialog
 *
 * Dialog for selecting and managing workspace/project directories.
 * Supports recent workspaces, favorites, and folder browsing.
 */

import { Clock, Folder, FolderOpen, FolderPlus, GitBranch, Search, Star } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { Button } from '../ui/Button'
import { Dialog } from '../ui/Dialog'
import { WorkspaceItem } from './workspace/WorkspaceItem'

export type { QuickWorkspacePickerProps } from './workspace/QuickWorkspacePicker'
// Re-export QuickWorkspacePicker for backward compat with index.ts
export { QuickWorkspacePicker } from './workspace/QuickWorkspacePicker'

// ============================================================================
// Types
// ============================================================================

export interface Workspace {
  id: string
  name: string
  path: string
  lastOpened?: Date
  isFavorite?: boolean
  gitBranch?: string
}

export interface WorkspaceSelectorDialogProps {
  /** Whether dialog is open */
  open: boolean
  /** Called when open state changes */
  onOpenChange: (open: boolean) => void
  /** Currently selected workspace */
  currentWorkspace?: Workspace
  /** Recent workspaces */
  recentWorkspaces: Workspace[]
  /** Called when workspace is selected */
  onSelect: (workspace: Workspace) => void
  /** Called when "Browse" is clicked to open folder picker */
  onBrowse?: () => void
  /** Called when workspace is toggled as favorite */
  onToggleFavorite?: (id: string) => void
  /** Called when workspace is removed from recents */
  onRemove?: (id: string) => void
}

// ============================================================================
// Workspace Selector Dialog
// ============================================================================

export const WorkspaceSelectorDialog: Component<WorkspaceSelectorDialogProps> = (props) => {
  const [searchQuery, setSearchQuery] = createSignal('')

  // Filter workspaces by search query
  const filteredWorkspaces = () => {
    const query = searchQuery().toLowerCase()
    if (!query) return props.recentWorkspaces

    return props.recentWorkspaces.filter(
      (w) => w.name.toLowerCase().includes(query) || w.path.toLowerCase().includes(query)
    )
  }

  // Group into favorites and recent
  const groupedWorkspaces = () => {
    const workspaces = filteredWorkspaces()
    const favorites = workspaces.filter((w) => w.isFavorite)
    const recent = workspaces.filter((w) => !w.isFavorite)

    return { favorites, recent }
  }

  const handleSelect = (workspace: Workspace) => {
    props.onSelect(workspace)
    props.onOpenChange(false)
  }

  return (
    <Dialog
      open={props.open}
      onOpenChange={props.onOpenChange}
      title="Select Workspace"
      description="Choose a project folder to work with"
      size="md"
    >
      <div class="space-y-4">
        {/* Search */}
        <div class="relative">
          <Search class="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-muted)]" />
          <input
            type="text"
            placeholder="Search workspaces..."
            value={searchQuery()}
            onInput={(e) => setSearchQuery(e.currentTarget.value)}
            class="
              w-full pl-10 pr-4 py-2.5
              bg-[var(--input-background)]
              border border-[var(--input-border)]
              rounded-[var(--radius-lg)]
              text-sm text-[var(--text-primary)]
              placeholder:text-[var(--text-muted)]
              focus:outline-none focus:border-[var(--accent)]
              transition-colors duration-[var(--duration-fast)]
            "
          />
        </div>

        {/* Current Workspace */}
        <Show when={props.currentWorkspace}>
          <div class="p-3 bg-[var(--accent-subtle)] border border-[var(--accent)] rounded-[var(--radius-lg)]">
            <div class="flex items-center gap-3">
              <div class="p-2 bg-[var(--accent)] rounded-[var(--radius-md)]">
                <FolderOpen class="w-4 h-4 text-white" />
              </div>
              <div class="flex-1 min-w-0">
                <div class="flex items-center gap-2">
                  <span class="text-sm font-medium text-[var(--accent)]">
                    {props.currentWorkspace!.name}
                  </span>
                  <span class="px-1.5 py-0.5 text-[10px] font-medium rounded-full bg-[var(--accent)] text-white">
                    Current
                  </span>
                </div>
                <div class="text-xs text-[var(--text-muted)] truncate">
                  {props.currentWorkspace!.path}
                </div>
              </div>
              <Show when={props.currentWorkspace!.gitBranch}>
                <div class="flex items-center gap-1 text-xs text-[var(--text-muted)]">
                  <GitBranch class="w-3 h-3" />
                  {props.currentWorkspace!.gitBranch}
                </div>
              </Show>
            </div>
          </div>
        </Show>

        {/* Workspace List */}
        <div class="max-h-80 overflow-y-auto space-y-4 -mx-4 px-4">
          {/* Favorites */}
          <Show when={groupedWorkspaces().favorites.length > 0}>
            <div>
              <div class="flex items-center gap-2 mb-2 text-xs font-medium text-[var(--text-muted)]">
                <Star class="w-3 h-3" />
                Favorites
              </div>
              <div class="space-y-1">
                <For each={groupedWorkspaces().favorites}>
                  {(workspace) => (
                    <WorkspaceItem
                      workspace={workspace}
                      isSelected={props.currentWorkspace?.id === workspace.id}
                      onSelect={() => handleSelect(workspace)}
                      onToggleFavorite={() => props.onToggleFavorite?.(workspace.id)}
                      onRemove={() => props.onRemove?.(workspace.id)}
                    />
                  )}
                </For>
              </div>
            </div>
          </Show>

          {/* Recent */}
          <Show when={groupedWorkspaces().recent.length > 0}>
            <div>
              <div class="flex items-center gap-2 mb-2 text-xs font-medium text-[var(--text-muted)]">
                <Clock class="w-3 h-3" />
                Recent
              </div>
              <div class="space-y-1">
                <For each={groupedWorkspaces().recent}>
                  {(workspace) => (
                    <WorkspaceItem
                      workspace={workspace}
                      isSelected={props.currentWorkspace?.id === workspace.id}
                      onSelect={() => handleSelect(workspace)}
                      onToggleFavorite={() => props.onToggleFavorite?.(workspace.id)}
                      onRemove={() => props.onRemove?.(workspace.id)}
                    />
                  )}
                </For>
              </div>
            </div>
          </Show>

          {/* Empty State */}
          <Show when={filteredWorkspaces().length === 0}>
            <div class="py-8 text-center">
              <Folder class="w-12 h-12 mx-auto mb-3 text-[var(--text-muted)]" />
              <Show
                when={searchQuery()}
                fallback={
                  <>
                    <p class="text-sm text-[var(--text-secondary)]">No recent workspaces</p>
                    <p class="text-xs text-[var(--text-muted)] mt-1">
                      Open a folder to get started
                    </p>
                  </>
                }
              >
                <p class="text-sm text-[var(--text-secondary)]">
                  No workspaces found matching "{searchQuery()}"
                </p>
              </Show>
            </div>
          </Show>
        </div>

        {/* Actions */}
        <div class="flex items-center gap-3 pt-2 border-t border-[var(--border-subtle)]">
          <Button
            variant="primary"
            class="flex-1"
            onClick={props.onBrowse}
            icon={<FolderPlus class="w-4 h-4" />}
          >
            Open Folder
          </Button>
          <Button variant="ghost" onClick={() => props.onOpenChange(false)}>
            Cancel
          </Button>
        </div>
      </div>
    </Dialog>
  )
}
