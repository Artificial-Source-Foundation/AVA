import { open } from '@tauri-apps/plugin-dialog'
import { Clock3, FolderOpen, FolderPlus, Play, Star, Trash2 } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { logError } from '../../services/logger'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import type { ProjectId, ProjectWithStats } from '../../types'
import { ConfirmDialog } from '../ui/ConfirmDialog'

export const ProjectHub: Component = () => {
  const { closeProjectHub } = useLayout()
  const {
    currentProject,
    favoriteProjects,
    recentProjects,
    openDirectory,
    switchProject,
    removeProject,
    toggleFavorite,
  } = useProject()
  const { loadSessionsForCurrentProject, restoreForCurrentProject } = useSession()

  const [removeTarget, setRemoveTarget] = createSignal<ProjectWithStats | null>(null)

  const handleRemove = (projectId: ProjectId) => {
    const project = orderedProjects().find((p) => p.id === projectId)
    if (project) setRemoveTarget(project)
  }

  const confirmRemove = async () => {
    const target = removeTarget()
    if (!target) return
    setRemoveTarget(null)
    await removeProject(target.id as ProjectId)
  }

  const orderedProjects = () => {
    const seen = new Set<string>()
    const combined = [...favoriteProjects(), ...recentProjects()]
    return combined.filter((project) => {
      if (seen.has(project.id)) return false
      seen.add(project.id)
      return true
    })
  }

  const enterProject = async (projectId: ProjectId) => {
    try {
      await switchProject(projectId)
    } catch (error) {
      console.error('[ProjectHub] switchProject failed:', error)
      logError('ProjectHub', 'Failed to switch project', error)
      return
    }

    closeProjectHub()

    try {
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
    } catch (error) {
      console.error('[ProjectHub] Session restore failed:', error)
      logError('ProjectHub', 'Failed to restore sessions', error)
    }
  }

  const handleOpenProject = async () => {
    let selected: string | string[] | null = null
    try {
      selected = await open({
        directory: true,
        title: 'Select Project Folder',
      })
    } catch (error) {
      console.error('[ProjectHub] Dialog failed:', error)
      logError('ProjectHub', 'Failed to open folder dialog', error)
      return
    }

    if (!selected || typeof selected !== 'string') {
      return
    }

    try {
      await openDirectory(selected)
    } catch (error) {
      console.error('[ProjectHub] openDirectory failed:', error)
      logError('ProjectHub', 'Failed to open directory', error)
      return
    }

    // Project is opened — close hub, then load sessions in background
    closeProjectHub()

    try {
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
    } catch (error) {
      console.error('[ProjectHub] Session restore failed:', error)
      logError('ProjectHub', 'Failed to restore sessions', error)
    }
  }

  const resumeCurrent = async () => {
    if (!currentProject()) {
      return
    }

    try {
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
      closeProjectHub()
    } catch (error) {
      logError('ProjectHub', 'Failed to resume current project', error)
    }
  }

  return (
    <div class="h-screen w-full overflow-y-auto bg-[var(--background)]">
      <div class="mx-auto w-full max-w-5xl px-6 py-10 lg:py-16">
        <div class="mb-8 rounded-2xl border border-[var(--border-default)] bg-[var(--surface-raised)] p-6 lg:p-8">
          <p class="text-xs font-semibold uppercase tracking-wider text-[var(--accent)]">
            Project Hub
          </p>
          <h1 class="mt-2 text-3xl font-semibold text-[var(--text-primary)] lg:text-4xl">
            Pick up where you left off
          </h1>
          <p class="mt-3 max-w-2xl text-sm text-[var(--text-secondary)]">
            Sessions stay scoped to one project per window. Switch projects any time from the
            sidebar quick switcher.
          </p>

          <div class="mt-6 flex flex-wrap gap-3">
            <button
              type="button"
              onClick={handleOpenProject}
              class="inline-flex items-center gap-2 rounded-[var(--radius-lg)] bg-[var(--accent)] px-4 py-2.5 text-sm font-semibold text-white transition-colors hover:bg-[var(--accent-hover)]"
            >
              <FolderPlus class="h-4 w-4" />
              Open project
            </button>

            <Show when={currentProject()}>
              <button
                type="button"
                onClick={resumeCurrent}
                class="inline-flex items-center gap-2 rounded-[var(--radius-lg)] border border-[var(--border-default)] bg-[var(--surface-sunken)] px-4 py-2.5 text-sm font-semibold text-[var(--text-primary)] transition-colors hover:bg-[var(--surface-overlay)]"
              >
                <Play class="h-4 w-4" />
                Resume {currentProject()?.name}
              </button>
            </Show>
          </div>
        </div>

        <div class="rounded-2xl border border-[var(--border-subtle)] bg-[var(--surface-overlay)] p-4 lg:p-5">
          <div class="mb-3 flex items-center gap-2 text-xs font-semibold uppercase tracking-wider text-[var(--text-muted)]">
            <Clock3 class="h-3.5 w-3.5" />
            Recent projects
          </div>

          <Show
            when={orderedProjects().length > 0}
            fallback={
              <div class="rounded-[var(--radius-lg)] border border-dashed border-[var(--border-default)] px-4 py-8 text-center text-sm text-[var(--text-secondary)]">
                Open a project to start creating project-scoped sessions.
              </div>
            }
          >
            <div class="grid gap-2">
              <For each={orderedProjects()}>
                {(project) => (
                  <ProjectRow
                    project={project}
                    onOpen={enterProject}
                    onRemove={(id) => handleRemove(id)}
                    onToggleFavorite={(id) => toggleFavorite(id)}
                  />
                )}
              </For>
            </div>
          </Show>
        </div>
      </div>

      <ConfirmDialog
        open={removeTarget() !== null}
        onOpenChange={(open) => {
          if (!open) setRemoveTarget(null)
        }}
        title="Remove project?"
        message={`"${removeTarget()?.name}" will be removed from your recent projects. Your files and sessions won't be deleted.`}
        confirmText="Remove"
        cancelText="Cancel"
        variant="danger"
        onConfirm={confirmRemove}
      />
    </div>
  )
}

interface ProjectRowProps {
  project: ProjectWithStats
  onOpen: (projectId: ProjectId) => Promise<void>
  onRemove: (projectId: ProjectId) => void
  onToggleFavorite: (projectId: ProjectId) => void
}

const ProjectRow: Component<ProjectRowProps> = (props) => {
  const formatOpened = (timestamp: number) => {
    const minutes = Math.max(1, Math.floor((Date.now() - timestamp) / 60000))
    if (minutes < 60) return `${minutes}m ago`
    const hours = Math.floor(minutes / 60)
    if (hours < 24) return `${hours}h ago`
    const days = Math.floor(hours / 24)
    return `${days}d ago`
  }

  return (
    <div
      role="option"
      tabIndex={0}
      onClick={() => props.onOpen(props.project.id as ProjectId)}
      onKeyDown={(e) => e.key === 'Enter' && props.onOpen(props.project.id as ProjectId)}
      class="flex items-center justify-between rounded-[var(--radius-lg)] border border-[var(--border-subtle)] bg-[var(--surface-raised)] px-3 py-3 text-left cursor-pointer transition-colors hover:border-[var(--border-default)] hover:bg-[var(--surface-sunken)] group"
    >
      <div class="min-w-0 flex-1">
        <div class="flex items-center gap-2">
          <FolderOpen class="h-4 w-4 text-[var(--accent)]" />
          <span class="truncate text-sm font-medium text-[var(--text-primary)]">
            {props.project.name}
          </span>
          <Show when={props.project.isFavorite}>
            <Star class="h-3.5 w-3.5 fill-[var(--warning)] text-[var(--warning)]" />
          </Show>
        </div>
        <p class="mt-1 truncate text-xs text-[var(--text-muted)]">{props.project.directory}</p>
      </div>

      <div class="ml-4 flex items-center gap-2">
        <span class="text-xs text-[var(--text-secondary)]">
          {formatOpened(props.project.lastOpenedAt ?? Date.now())}
        </span>

        <div class="flex items-center gap-0.5 opacity-0 group-hover:opacity-100 transition-opacity">
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              props.onToggleFavorite(props.project.id as ProjectId)
            }}
            class={`p-1.5 rounded-[var(--radius-sm)] transition-colors ${
              props.project.isFavorite
                ? 'text-[var(--warning)]'
                : 'text-[var(--text-muted)] hover:text-[var(--warning)]'
            } hover:bg-[var(--alpha-white-8)]`}
            title={props.project.isFavorite ? 'Remove from favorites' : 'Add to favorites'}
          >
            <Star class={`h-3.5 w-3.5 ${props.project.isFavorite ? 'fill-current' : ''}`} />
          </button>

          <Show when={props.project.id !== 'default-project'}>
            <button
              type="button"
              onClick={(e) => {
                e.stopPropagation()
                props.onRemove(props.project.id as ProjectId)
              }}
              class="p-1.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--error)] hover:bg-[var(--error-subtle)] transition-colors"
              title="Remove from list"
            >
              <Trash2 class="h-3.5 w-3.5" />
            </button>
          </Show>
        </div>
      </div>
    </div>
  )
}
