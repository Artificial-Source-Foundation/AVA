/**
 * Workspace Item Component
 *
 * Displays a single workspace entry with folder icon, name, path, git branch,
 * favorite toggle, and remove button.
 */

import { Check, Folder, GitBranch, Star, StarOff, X } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import type { Workspace } from '../WorkspaceSelectorDialog'
import { formatRelativeTime, getParentPath } from './helpers'

export interface WorkspaceItemProps {
  workspace: Workspace
  isSelected?: boolean
  onSelect: () => void
  onToggleFavorite?: () => void
  onRemove?: () => void
}

export const WorkspaceItem: Component<WorkspaceItemProps> = (props) => {
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
