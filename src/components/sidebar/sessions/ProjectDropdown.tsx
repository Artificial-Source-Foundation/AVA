/**
 * Project Dropdown
 *
 * Quick-switch dropdown for changing projects from the sidebar header.
 */

import { ChevronDown, Compass, FolderOpen } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, onCleanup, Show } from 'solid-js'
import { logError } from '../../../services/logger'
import { useLayout } from '../../../stores/layout'
import { useProject } from '../../../stores/project'
import { useSession } from '../../../stores/session'
import type { ProjectId, ProjectWithStats } from '../../../types'

export const ProjectDropdown: Component = () => {
  const { loadSessionsForCurrentProject, restoreForCurrentProject } = useSession()
  const { currentProject, favoriteProjects, recentProjects, switchProject, openDirectory } =
    useProject()
  const { openProjectHub, closeProjectHub } = useLayout()
  const [dropdownOpen, setDropdownOpen] = createSignal(false)
  let dropdownRef: HTMLDivElement | undefined

  const quickProjects = createMemo(() => {
    const seen = new Set<string>()
    const combined: Array<ProjectWithStats | null> = [
      currentProject() as ProjectWithStats | null,
      ...favoriteProjects(),
      ...recentProjects(),
    ]
    return combined.filter((project): project is ProjectWithStats => {
      if (!project) return false
      if (seen.has(project.id)) return false
      seen.add(project.id)
      return true
    })
  })

  const handleOutsideClick = (e: MouseEvent): void => {
    if (dropdownOpen() && dropdownRef && !dropdownRef.contains(e.target as Node)) {
      setDropdownOpen(false)
    }
  }

  const handleEscapeKey = (e: KeyboardEvent): void => {
    if (e.key === 'Escape' && dropdownOpen()) {
      setDropdownOpen(false)
    }
  }

  document.addEventListener('mousedown', handleOutsideClick)
  document.addEventListener('keydown', handleEscapeKey)

  onCleanup(() => {
    document.removeEventListener('mousedown', handleOutsideClick)
    document.removeEventListener('keydown', handleEscapeKey)
  })

  const handleProjectSwitch = async (projectId: string): Promise<void> => {
    if (!projectId || projectId === currentProject()?.id) {
      setDropdownOpen(false)
      return
    }
    try {
      await switchProject(projectId as ProjectId)
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
      closeProjectHub()
    } catch (err) {
      logError('ProjectDropdown', 'Failed to switch project from sidebar', err)
    }
    setDropdownOpen(false)
  }

  const handleOpenProject = async (): Promise<void> => {
    setDropdownOpen(false)
    try {
      const { open } = await import('@tauri-apps/plugin-dialog')
      const selected = await open({ directory: true, title: 'Select Project Folder' })
      if (!selected || typeof selected !== 'string') return
      await openDirectory(selected)
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
      closeProjectHub()
    } catch (err) {
      logError('ProjectDropdown', 'Failed to open project from sidebar', err)
    }
  }

  const handleBrowseHub = (): void => {
    setDropdownOpen(false)
    openProjectHub()
  }

  return (
    <div ref={dropdownRef} class="relative">
      <button
        type="button"
        onClick={() => setDropdownOpen(!dropdownOpen())}
        class={`
          inline-flex items-center gap-1 max-w-[120px]
          rounded-[var(--radius-md)] px-2.5 py-1
          text-[11px] font-medium transition-colors
          bg-[var(--gray-3)]
          ${
            dropdownOpen()
              ? 'text-[var(--text-primary)]'
              : 'text-[var(--gray-9)] hover:text-[var(--text-primary)]'
          }
        `}
        title="Switch project"
      >
        <span class="truncate">{currentProject()?.name ?? 'No project'}</span>
        <ChevronDown
          class={`w-3 h-3 flex-shrink-0 transition-transform ${dropdownOpen() ? 'rotate-180' : ''}`}
        />
      </button>

      <Show when={dropdownOpen()}>
        <div
          class="fixed min-w-[180px] max-w-[240px] py-1 bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-lg)] shadow-lg z-50"
          style={{
            top: `${(dropdownRef?.getBoundingClientRect().bottom ?? 0) + 4}px`,
            left: `${dropdownRef?.getBoundingClientRect().left ?? 0}px`,
          }}
        >
          <div class="max-h-[200px] overflow-y-auto scrollbar-none">
            <For each={quickProjects()}>
              {(project) => {
                const isCurrent = () => project.id === currentProject()?.id
                return (
                  <button
                    type="button"
                    onClick={() => void handleProjectSwitch(project.id)}
                    class={`
                      w-full flex items-center gap-2 px-3 py-1.5 text-xs transition-colors
                      ${
                        isCurrent()
                          ? 'text-[var(--accent)] bg-[var(--alpha-white-5)]'
                          : 'text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)]'
                      }
                    `}
                  >
                    <span class="truncate">{project.name}</span>
                    <Show when={project.isFavorite}>
                      <span class="text-[9px] text-[var(--text-muted)]">*</span>
                    </Show>
                  </button>
                )
              }}
            </For>
          </div>

          <div class="border-t border-[var(--border-subtle)] mt-1 pt-1">
            <button
              type="button"
              onClick={handleBrowseHub}
              class="w-full flex items-center gap-2 px-3 py-1.5 text-xs text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
            >
              <Compass class="w-3 h-3 flex-shrink-0" />
              <span>Browse Hub</span>
            </button>
            <button
              type="button"
              onClick={() => void handleOpenProject()}
              class="w-full flex items-center gap-2 px-3 py-1.5 text-xs text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
            >
              <FolderOpen class="w-3 h-3 flex-shrink-0" />
              <span>Open Folder...</span>
            </button>
          </div>
        </div>
      </Show>
    </div>
  )
}
