/**
 * Project Selector Component
 *
 * A polished workspace selector with dropdown for switching between projects.
 * Shows project icon, name, and git branch. Supports favorites and recent projects.
 */

import { open } from '@tauri-apps/plugin-dialog'
import { Check, ChevronDown, Folder, FolderOpen, GitBranch, Plus, Star, Trash2 } from 'lucide-solid'
import { type Component, createSignal, For, onCleanup, onMount, Show } from 'solid-js'
import { logError } from '../../services/logger'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import type { ProjectId, ProjectWithStats } from '../../types'

// ============================================================================
// Project Selector Component
// ============================================================================

export const ProjectSelector: Component = () => {
  const {
    currentProject,
    favoriteProjects,
    recentProjects,
    hasProjects,
    switchProject,
    openDirectory,
    toggleFavorite,
    removeProject,
  } = useProject()
  const { loadSessionsForCurrentProject, restoreForCurrentProject } = useSession()

  const [isOpen, setIsOpen] = createSignal(false)
  let containerRef: HTMLDivElement | undefined

  // Close dropdown when clicking outside
  const handleClickOutside = (e: MouseEvent) => {
    if (containerRef && !containerRef.contains(e.target as Node)) {
      setIsOpen(false)
    }
  }

  onMount(() => {
    document.addEventListener('mousedown', handleClickOutside)
  })

  onCleanup(() => {
    document.removeEventListener('mousedown', handleClickOutside)
  })

  // Open folder dialog
  const handleBrowse = async () => {
    try {
      const selected = await open({
        directory: true,
        title: 'Select Project Folder',
      })

      if (selected && typeof selected === 'string') {
        await openDirectory(selected)
        await loadSessionsForCurrentProject()
        await restoreForCurrentProject()
        setIsOpen(false)
      }
    } catch (err) {
      logError('ProjectSelector', 'Failed to open folder', err)
    }
  }

  const handleSelectProject = async (projectId: ProjectId) => {
    try {
      await switchProject(projectId)
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
      setIsOpen(false)
    } catch (err) {
      logError('ProjectSelector', 'Failed to switch project', err)
    }
  }

  return (
    // biome-ignore lint/suspicious/noAssignInExpressions: SolidJS ref pattern
    <div ref={(el) => (containerRef = el)} class="relative">
      {/* Trigger Button */}
      <button
        type="button"
        onClick={() => setIsOpen(!isOpen())}
        class="
          w-full flex items-center gap-3
          px-3 py-2.5
          bg-[var(--surface-raised)]
          hover:bg-[var(--sidebar-item-hover)]
          border border-[var(--border-subtle)]
          hover:border-[var(--border-default)]
          rounded-[var(--radius-lg)]
          transition-all duration-[var(--duration-fast)]
          group
        "
      >
        {/* Project Icon */}
        <div
          class="
            w-9 h-9 rounded-[var(--radius-md)]
            bg-gradient-to-br from-[var(--accent)] to-[var(--accent-hover)]
            flex items-center justify-center
            text-white
            shadow-sm
            transition-transform duration-[var(--duration-fast)]
            group-hover:scale-105
          "
        >
          <Show when={currentProject()?.icon?.override} fallback={<Folder class="w-4 h-4" />}>
            <span class="text-sm">{currentProject()!.icon!.override}</span>
          </Show>
        </div>

        {/* Project Info */}
        <div class="flex-1 min-w-0 text-left">
          <div class="text-sm font-medium text-[var(--text-primary)] truncate">
            {currentProject()?.name ?? 'Select Project'}
          </div>
          <Show
            when={currentProject()?.git?.branch}
            fallback={
              <div class="text-xs text-[var(--text-muted)]">
                {currentProject()?.directory === '~' ? 'Default workspace' : 'No git repository'}
              </div>
            }
          >
            <div class="flex items-center gap-1.5 text-xs text-[var(--text-muted)]">
              <GitBranch class="w-3 h-3 text-[var(--success)]" />
              <span class="truncate">{currentProject()!.git!.branch}</span>
            </div>
          </Show>
        </div>

        {/* Chevron */}
        <ChevronDown
          class={`
            w-4 h-4 text-[var(--text-muted)]
            transition-transform duration-[var(--duration-fast)]
            ${isOpen() ? 'rotate-180' : ''}
          `}
        />
      </button>

      {/* Dropdown */}
      <Show when={isOpen()}>
        <div
          class="
            absolute top-full left-0 right-0 mt-2
            bg-[var(--surface-overlay)]
            border border-[var(--border-default)]
            rounded-[var(--radius-lg)]
            shadow-xl
            z-50
            overflow-hidden
            animate-slide-down
          "
        >
          {/* Favorites Section */}
          <Show when={favoriteProjects().length > 0}>
            <div class="px-3 pt-3 pb-1">
              <div class="flex items-center gap-1.5 text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider">
                <Star class="w-3 h-3 fill-[var(--warning)] text-[var(--warning)]" />
                Favorites
              </div>
            </div>
            <div class="px-1.5 pb-1">
              <For each={favoriteProjects()}>
                {(project) => (
                  <ProjectItem
                    project={project}
                    isSelected={currentProject()?.id === project.id}
                    onSelect={() => handleSelectProject(project.id as ProjectId)}
                    onToggleFavorite={() => toggleFavorite(project.id as ProjectId)}
                    onRemove={() => removeProject(project.id as ProjectId)}
                  />
                )}
              </For>
            </div>
            <div class="mx-3 border-t border-[var(--border-subtle)]" />
          </Show>

          {/* Recent Section */}
          <Show when={recentProjects().length > 0}>
            <div class="px-3 pt-3 pb-1">
              <div class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider">
                Recent
              </div>
            </div>
            <div class="px-1.5 pb-1 max-h-52 overflow-y-auto">
              <For each={recentProjects()}>
                {(project) => (
                  <ProjectItem
                    project={project}
                    isSelected={currentProject()?.id === project.id}
                    onSelect={() => handleSelectProject(project.id as ProjectId)}
                    onToggleFavorite={() => toggleFavorite(project.id as ProjectId)}
                    onRemove={() => removeProject(project.id as ProjectId)}
                  />
                )}
              </For>
            </div>
          </Show>

          {/* Empty State */}
          <Show when={!hasProjects()}>
            <div class="px-4 py-6 text-center">
              <FolderOpen class="w-10 h-10 mx-auto mb-2 text-[var(--text-muted)]" />
              <p class="text-sm text-[var(--text-secondary)]">No projects yet</p>
              <p class="text-xs text-[var(--text-muted)] mt-1">Open a folder to get started</p>
            </div>
          </Show>

          {/* Open Folder Action */}
          <div class="p-2 border-t border-[var(--border-subtle)] bg-[var(--surface-sunken)]">
            <button
              type="button"
              onClick={handleBrowse}
              class="
                w-full flex items-center justify-center gap-2
                px-3 py-2.5
                text-sm font-medium text-[var(--accent)]
                bg-[var(--accent-subtle)]
                hover:bg-[var(--accent)]
                hover:text-white
                rounded-[var(--radius-md)]
                transition-all duration-[var(--duration-fast)]
              "
            >
              <Plus class="w-4 h-4" />
              Open Folder
            </button>
          </div>
        </div>
      </Show>
    </div>
  )
}

// ============================================================================
// Project Item Sub-component
// ============================================================================

interface ProjectItemProps {
  project: ProjectWithStats
  isSelected: boolean
  onSelect: () => void
  onToggleFavorite: () => void
  onRemove: () => void
}

const ProjectItem: Component<ProjectItemProps> = (props) => {
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

export default ProjectSelector
