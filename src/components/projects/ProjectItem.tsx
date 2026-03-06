/**
 * Project Item Sub-component
 *
 * Renders a single project row in the ProjectSelector dropdown
 * with hover actions for favorite toggle and removal.
 */

import { Check, Folder, FolderOpen, GitBranch, Star, Trash2 } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import type { ProjectWithStats } from '../../types'

export interface ProjectItemProps {
  project: ProjectWithStats
  isSelected: boolean
  onSelect: () => void
  onToggleFavorite: () => void
  onRemove: () => void
}

export const ProjectItem: Component<ProjectItemProps> = (props) => {
  const [showActions, setShowActions] = createSignal(false)

  return (
    <div
      role="option"
      tabIndex={0}
      onClick={() => props.onSelect()}
      onKeyDown={(e) => e.key === 'Enter' && props.onSelect()}
      onMouseEnter={() => setShowActions(true)}
      onMouseLeave={() => setShowActions(false)}
      class={`
        w-full flex items-center gap-3 px-2.5 py-2
        rounded-[var(--radius-md)]
        text-left cursor-pointer
        transition-all duration-[var(--duration-fast)]
        group
        ${
          props.isSelected
            ? 'bg-[var(--accent-subtle)] border border-[var(--accent)]/30'
            : 'hover:bg-[var(--surface-raised)] border border-transparent'
        }
      `}
    >
      {/* Icon */}
      <div
        class={`
          w-8 h-8 rounded-[var(--radius-sm)]
          flex items-center justify-center
          transition-colors duration-[var(--duration-fast)]
          ${
            props.isSelected
              ? 'bg-[var(--accent)] text-white'
              : 'bg-[var(--surface-sunken)] text-[var(--text-muted)] group-hover:bg-[var(--surface-raised)] group-hover:text-[var(--text-secondary)]'
          }
        `}
      >
        <Show when={props.isSelected} fallback={<Folder class="w-3.5 h-3.5" />}>
          <FolderOpen class="w-3.5 h-3.5" />
        </Show>
      </div>

      {/* Info */}
      <div class="flex-1 min-w-0">
        <div class="flex items-center gap-2">
          <span
            class={`text-sm truncate ${
              props.isSelected ? 'text-[var(--accent)] font-medium' : 'text-[var(--text-primary)]'
            }`}
          >
            {props.project.name}
          </span>
          <Show when={props.isSelected}>
            <Check class="w-3.5 h-3.5 text-[var(--accent)] flex-shrink-0" />
          </Show>
        </div>
        <div class="flex items-center gap-2 text-xs text-[var(--text-muted)]">
          <span>{props.project.sessionCount} sessions</span>
          <Show when={props.project.git?.branch}>
            <span class="flex items-center gap-1">
              <GitBranch class="w-3 h-3" />
              {props.project.git!.branch}
            </span>
          </Show>
        </div>
      </div>

      {/* Actions */}
      <div
        class={`
          flex items-center gap-0.5
          transition-opacity duration-[var(--duration-fast)]
          ${showActions() ? 'opacity-100' : 'opacity-0'}
        `}
      >
        {/* Favorite toggle */}
        <button
          type="button"
          onClick={(e) => {
            e.stopPropagation()
            props.onToggleFavorite()
          }}
          class={`
            p-1.5 rounded-[var(--radius-sm)]
            transition-all duration-[var(--duration-fast)]
            ${
              props.project.isFavorite
                ? 'text-[var(--warning)]'
                : 'text-[var(--text-muted)] hover:text-[var(--warning)]'
            }
            hover:bg-[var(--surface-raised)]
          `}
          title={props.project.isFavorite ? 'Remove from favorites' : 'Add to favorites'}
        >
          <Star class={`w-3.5 h-3.5 ${props.project.isFavorite ? 'fill-current' : ''}`} />
        </button>

        {/* Remove */}
        <Show when={props.project.id !== 'default-project'}>
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              props.onRemove()
            }}
            class="
              p-1.5 rounded-[var(--radius-sm)]
              text-[var(--text-muted)]
              hover:text-[var(--error)]
              hover:bg-[var(--error-subtle)]
              transition-all duration-[var(--duration-fast)]
            "
            title="Remove from list"
          >
            <Trash2 class="w-3.5 h-3.5" />
          </button>
        </Show>
      </div>
    </div>
  )
}
