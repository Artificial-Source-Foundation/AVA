/**
 * Project Dropdown
 *
 * Quick-switch dropdown for changing projects from the sidebar header.
 */

import { ChevronsUpDown, Compass, FolderOpen, GitBranch } from 'lucide-solid'
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

  const gitBranch = () => currentProject()?.git?.branch

  return (
    <div ref={dropdownRef} class="relative">
      <button
        type="button"
        onClick={() => setDropdownOpen(!dropdownOpen())}
        class="flex items-center gap-2 w-full px-2 py-1.5 rounded-[var(--radius-md)] transition-colors hover:bg-[var(--alpha-white-5)]"
        title="Switch project"
      >
        <FolderOpen class="w-3.5 h-3.5 flex-shrink-0" style={{ color: 'var(--text-secondary)' }} />
        <span
          class="font-medium truncate flex-1 text-left"
          style={{ 'font-size': '13px', color: 'var(--text-primary)' }}
        >
          {currentProject()?.name ?? 'No project'}
        </span>
        <Show when={gitBranch()}>
          <span
            class="inline-flex items-center gap-1 px-1.5 py-0.5 rounded-full flex-shrink-0"
            style={{
              background: 'var(--alpha-white-5)',
              border: '1px solid var(--alpha-white-8)',
            }}
          >
            <GitBranch class="w-2.5 h-2.5" style={{ color: 'var(--text-muted)' }} />
            <span
              class="font-mono truncate"
              style={{
                'font-size': '9px',
                color: 'var(--text-muted)',
                'max-width': '80px',
              }}
            >
              {gitBranch()}
            </span>
          </span>
        </Show>
        <ChevronsUpDown class="w-3 h-3 flex-shrink-0" style={{ color: 'var(--text-muted)' }} />
      </button>

      <Show when={dropdownOpen()}>
        <div
          class="fixed min-w-[180px] max-w-[240px] py-1 z-50 animate-dropdown-in"
          style={{
            top: `${(dropdownRef?.getBoundingClientRect().bottom ?? 0) + 4}px`,
            left: `${dropdownRef?.getBoundingClientRect().left ?? 0}px`,
            background: 'var(--dropdown-surface)',
            border: '1px solid var(--dropdown-border)',
            'border-radius': 'var(--dropdown-radius)',
            'box-shadow': 'var(--modal-shadow)',
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
                    class="w-full flex items-center gap-2 px-3 transition-colors"
                    style={{
                      height: '32px',
                      'font-size': '13px',
                      'font-family': 'var(--font-sans)',
                      color: isCurrent() ? 'var(--dropdown-item-active-text)' : 'var(--gray-9)',
                      background: isCurrent() ? 'var(--dropdown-item-active-bg)' : 'transparent',
                    }}
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

          <div class="mt-1 pt-1" style={{ 'border-top': '1px solid var(--dropdown-separator)' }}>
            <button
              type="button"
              onClick={handleBrowseHub}
              class="w-full flex items-center gap-2 px-3 transition-colors hover:bg-[var(--dropdown-item-hover)]"
              style={{ height: '32px', 'font-size': '13px', color: 'var(--gray-9)' }}
            >
              <Compass class="w-3.5 h-3.5 flex-shrink-0" style={{ color: 'currentColor' }} />
              <span>Browse Hub</span>
            </button>
            <button
              type="button"
              onClick={() => void handleOpenProject()}
              class="w-full flex items-center gap-2 px-3 transition-colors hover:bg-[var(--dropdown-item-hover)]"
              style={{ height: '32px', 'font-size': '13px', color: 'var(--gray-9)' }}
            >
              <FolderOpen class="w-3.5 h-3.5 flex-shrink-0" style={{ color: 'currentColor' }} />
              <span>Open Folder...</span>
            </button>
          </div>
        </div>
      </Show>
    </div>
  )
}
