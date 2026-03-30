/**
 * Project Hub Landing Page
 *
 * Full-screen view with header (search + open project button),
 * current project card, and recent projects grid.
 * Matches the Pencil design spec (KVAdT).
 */

import { FolderOpen, Plus, Search } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { logError } from '../../services/logger'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import type { ProjectId } from '../../types'
import { ProjectCard } from './ProjectCard'

export const ProjectHub: Component = () => {
  const { closeProjectHub } = useLayout()
  const {
    currentProject,
    recentProjects,
    favoriteProjects,
    openDirectory,
    switchProject,
    isLoadingProjects,
  } = useProject()
  const { loadSessionsForCurrentProject, restoreForCurrentProject } = useSession()

  const [searchQuery, setSearchQuery] = createSignal('')

  /** Deduplicated ordered list: favorites first, then recent, excluding current project. */
  const orderedProjects = () => {
    const current = currentProject()
    const seen = new Set<string>()
    if (current) seen.add(current.id)
    const combined = [...favoriteProjects(), ...recentProjects()]
    return combined.filter((project) => {
      if (seen.has(project.id)) return false
      seen.add(project.id)
      return true
    })
  }

  /** Filtered recent projects based on search query. */
  const filteredProjects = () => {
    const q = searchQuery().toLowerCase().trim()
    if (!q) return orderedProjects()
    return orderedProjects().filter(
      (p) => p.name.toLowerCase().includes(q) || p.directory.toLowerCase().includes(q)
    )
  }

  /** Total project count (current + recent). */
  const projectCount = () => {
    const count = orderedProjects().length
    return currentProject() ? count + 1 : count
  }

  // ── Handlers ───────────────────────────────────────────────────

  const handleOpenProject = async (): Promise<void> => {
    let selected: string | string[] | null = null
    try {
      const { open } = await import('@tauri-apps/plugin-dialog')
      selected = await open({ directory: true, title: 'Select Project Folder' })
    } catch (error) {
      logError('ProjectHub', 'Failed to open folder dialog', error)
      return
    }
    if (!selected || typeof selected !== 'string') return

    try {
      await openDirectory(selected)
    } catch (error) {
      logError('ProjectHub', 'Failed to open directory', error)
      return
    }

    closeProjectHub()

    try {
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
    } catch (error) {
      logError('ProjectHub', 'Session restore failed after open', error)
    }
  }

  const handleProjectClick = async (projectId: ProjectId): Promise<void> => {
    try {
      await switchProject(projectId)
    } catch (error) {
      logError('ProjectHub', 'Failed to switch project', error)
      return
    }

    closeProjectHub()

    try {
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
    } catch (error) {
      logError('ProjectHub', 'Session restore failed after switch', error)
    }
  }

  const handleCurrentProjectClick = async (): Promise<void> => {
    const current = currentProject()
    if (!current) return
    try {
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
      closeProjectHub()
    } catch (error) {
      logError('ProjectHub', 'Failed to resume current project', error)
    }
  }

  // ── Render ─────────────────────────────────────────────────────

  return (
    <div class="ph-root">
      {/* ── Header (56px) ──────────────────────────────────── */}
      <div class="ph-header">
        <div class="ph-header-left">
          <FolderOpen class="ph-header-icon" />
          <span class="ph-header-title">Projects</span>
          <Show when={projectCount() > 0}>
            <span class="ph-header-count">{projectCount()}</span>
          </Show>
        </div>

        <div class="ph-header-right">
          <div class="ph-search">
            <Search class="ph-search-icon" />
            <input
              type="text"
              class="ph-search-input"
              placeholder="Search projects..."
              value={searchQuery()}
              onInput={(e) => setSearchQuery(e.currentTarget.value)}
            />
          </div>

          <button type="button" class="ph-open-btn" onClick={handleOpenProject}>
            <Plus class="ph-open-btn-icon" />
            <span>Open Project</span>
          </button>
        </div>
      </div>

      {/* ── Divider ────────────────────────────────────────── */}
      <div class="ph-divider" />

      {/* ── Body ───────────────────────────────────────────── */}
      <div class="ph-body">
        {/* Loading state */}
        <Show when={isLoadingProjects()}>
          <div class="ph-loading">
            <div class="ph-spinner" />
            <span>Loading projects...</span>
          </div>
        </Show>

        <Show when={!isLoadingProjects()}>
          {/* ── Current Project ─────────────────────────────── */}
          <Show when={currentProject()}>
            {(project) => (
              <div class="ph-section">
                <span class="ph-section-label">CURRENT PROJECT</span>
                <ProjectCard
                  project={project()}
                  variant="active"
                  onClick={handleCurrentProjectClick}
                />
              </div>
            )}
          </Show>

          {/* ── Recent Projects ─────────────────────────────── */}
          <Show when={filteredProjects().length > 0}>
            <div class="ph-section">
              <span class="ph-section-label">RECENT PROJECTS</span>
              <div class="ph-grid">
                <For each={filteredProjects()}>
                  {(project) => (
                    <ProjectCard
                      project={project}
                      variant="default"
                      onClick={() => handleProjectClick(project.id as ProjectId)}
                    />
                  )}
                </For>
              </div>
            </div>
          </Show>

          {/* Empty state (no projects at all) */}
          <Show when={!currentProject() && orderedProjects().length === 0}>
            <div class="ph-empty">
              <FolderOpen class="ph-empty-icon" />
              <p class="ph-empty-title">No projects yet</p>
              <p class="ph-empty-sub">Open a folder to get started</p>
            </div>
          </Show>
        </Show>
      </div>
    </div>
  )
}
