/**
 * Workspace Selector Dialog
 *
 * Dialog for selecting and managing workspace/project directories.
 * Supports recent workspaces, favorites, and folder browsing.
 */

import {
  Check,
  Clock,
  Folder,
  FolderOpen,
  FolderPlus,
  GitBranch,
  Plus,
  Search,
  Star,
  StarOff,
  X,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { Button } from '../ui/Button'
import { Dialog } from '../ui/Dialog'

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
// Helper Functions
// ============================================================================

const formatRelativeTime = (date: Date): string => {
  const now = new Date()
  const diffMs = now.getTime() - date.getTime()
  const diffMins = Math.floor(diffMs / 60000)
  const diffHours = Math.floor(diffMs / 3600000)
  const diffDays = Math.floor(diffMs / 86400000)

  if (diffMins < 1) return 'Just now'
  if (diffMins < 60) return `${diffMins}m ago`
  if (diffHours < 24) return `${diffHours}h ago`
  if (diffDays < 7) return `${diffDays}d ago`
  return date.toLocaleDateString()
}

const getParentPath = (path: string): string => {
  const parts = path.split('/')
  return parts.slice(0, -1).join('/') || '/'
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

// ============================================================================
// Workspace Item Component
// ============================================================================

interface WorkspaceItemProps {
  workspace: Workspace
  isSelected?: boolean
  onSelect: () => void
  onToggleFavorite?: () => void
  onRemove?: () => void
}

const WorkspaceItem: Component<WorkspaceItemProps> = (props) => {
  const [showActions, setShowActions] = createSignal(false)

  return (
    <button
      type="button"
      class={`
        w-full text-left
        group flex items-center gap-3 p-2.5
        rounded-[var(--radius-lg)]
        cursor-pointer
        transition-colors duration-[var(--duration-fast)]
        ${props.isSelected ? 'bg-[var(--accent-subtle)]' : 'hover:bg-[var(--surface-raised)]'}
      `}
      onClick={() => props.onSelect()}
      onMouseEnter={() => setShowActions(true)}
      onMouseLeave={() => setShowActions(false)}
    >
      {/* Icon */}
      <div
        class={`
          p-2 rounded-[var(--radius-md)]
          ${
            props.isSelected
              ? 'bg-[var(--accent)] text-white'
              : 'bg-[var(--surface-sunken)] text-[var(--text-muted)]'
          }
        `}
      >
        <Folder class="w-4 h-4" />
      </div>

      {/* Info */}
      <div class="flex-1 min-w-0">
        <div class="flex items-center gap-2">
          <span
            class={`text-sm font-medium ${
              props.isSelected ? 'text-[var(--accent)]' : 'text-[var(--text-primary)]'
            }`}
          >
            {props.workspace.name}
          </span>
          <Show when={props.workspace.gitBranch}>
            <span class="flex items-center gap-1 text-xs text-[var(--text-muted)]">
              <GitBranch class="w-3 h-3" />
              {props.workspace.gitBranch}
            </span>
          </Show>
        </div>
        <div class="flex items-center gap-2 text-xs text-[var(--text-muted)]">
          <span class="truncate">{getParentPath(props.workspace.path)}</span>
          <Show when={props.workspace.lastOpened}>
            <span>•</span>
            <span>{formatRelativeTime(props.workspace.lastOpened!)}</span>
          </Show>
        </div>
      </div>

      {/* Actions */}
      <div
        class={`
          flex items-center gap-1
          transition-opacity duration-[var(--duration-fast)]
          ${showActions() ? 'opacity-100' : 'opacity-0'}
        `}
      >
        <Show when={props.onToggleFavorite}>
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              props.onToggleFavorite?.()
            }}
            class={`
              p-1.5 rounded-[var(--radius-md)]
              transition-colors duration-[var(--duration-fast)]
              ${
                props.workspace.isFavorite
                  ? 'text-[var(--warning)] hover:bg-[var(--warning-subtle)]'
                  : 'text-[var(--text-muted)] hover:text-[var(--warning)] hover:bg-[var(--surface-raised)]'
              }
            `}
            title={props.workspace.isFavorite ? 'Remove from favorites' : 'Add to favorites'}
          >
            <Show when={props.workspace.isFavorite} fallback={<StarOff class="w-4 h-4" />}>
              <Star class="w-4 h-4 fill-current" />
            </Show>
          </button>
        </Show>
        <Show when={props.onRemove}>
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              props.onRemove?.()
            }}
            class="
              p-1.5 rounded-[var(--radius-md)]
              text-[var(--text-muted)]
              hover:text-[var(--error)] hover:bg-[var(--error-subtle)]
              transition-colors duration-[var(--duration-fast)]
            "
            title="Remove from recents"
          >
            <X class="w-4 h-4" />
          </button>
        </Show>
      </div>

      {/* Selected indicator */}
      <Show when={props.isSelected}>
        <Check class="w-4 h-4 text-[var(--accent)] flex-shrink-0" />
      </Show>
    </button>
  )
}

// ============================================================================
// Quick Workspace Picker (for sidebar/header)
// ============================================================================

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
