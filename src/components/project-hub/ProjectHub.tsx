/**
 * Project Hub Landing Page
 *
 * Full-screen landing page with time-based greeting, quick actions,
 * and recent projects grid. Replaces the old project hub with a
 * polished, design-spec-aligned layout.
 */

import { open } from '@tauri-apps/plugin-dialog'
import { Settings } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { logError } from '../../services/logger'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import type { ProjectId } from '../../types'
import { ProjectCard } from './ProjectCard'
import { QuickActions } from './QuickActions'

/** Return a time-based greeting based on the current hour. */
function getGreeting(): string {
  const hour = new Date().getHours()
  if (hour < 12) return 'Good morning'
  if (hour < 18) return 'Good afternoon'
  return 'Good evening'
}

export const ProjectHub: Component = () => {
  const { closeProjectHub } = useLayout()
  const { currentProject, recentProjects, favoriteProjects, openDirectory, switchProject } =
    useProject()
  const { createNewSession, loadSessionsForCurrentProject, restoreForCurrentProject } = useSession()

  /** Deduplicated ordered list: favorites first, then recent. */
  const orderedProjects = () => {
    const seen = new Set<string>()
    const combined = [...favoriteProjects(), ...recentProjects()]
    return combined.filter((project) => {
      if (seen.has(project.id)) return false
      seen.add(project.id)
      return true
    })
  }

  // ── Handlers ───────────────────────────────────────────────────

  const handleNewSession = async (): Promise<void> => {
    try {
      await createNewSession()
      closeProjectHub()
    } catch (error) {
      logError('ProjectHub', 'Failed to create new session', error)
    }
  }

  const handleOpenProject = async (): Promise<void> => {
    let selected: string | string[] | null = null
    try {
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

  const handleResumeLast = async (): Promise<void> => {
    if (!currentProject()) return
    try {
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
      closeProjectHub()
    } catch (error) {
      logError('ProjectHub', 'Failed to resume last session', error)
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

  const handleSettingsClick = (): void => {
    // Dispatch event that AppDialogs / layout can listen to
    const { openSettings } = useLayout()
    openSettings()
  }

  // ── Render ─────────────────────────────────────────────────────

  return (
    <div class="h-screen w-full flex flex-col bg-[#09090B] overflow-hidden">
      {/* ── Top Bar (56px) ──────────────────────────────────── */}
      <div class="flex items-center justify-between h-14 px-5 flex-shrink-0 border-b border-[#27272A]">
        {/* Left: logo + app name */}
        <div class="flex items-center gap-3">
          <div class="w-8 h-8 rounded-lg bg-[#251538] flex items-center justify-center">
            <span class="text-[14px] font-bold text-[#A78BFA]">A</span>
          </div>
          <span class="text-[16px] font-bold text-white tracking-tight">AVA</span>
        </div>

        {/* Right: settings gear */}
        <button
          type="button"
          onClick={handleSettingsClick}
          class="p-2 rounded-lg text-[#52525B] hover:text-[#A1A1AA] hover:bg-[#18181B] transition-colors"
          title="Settings"
          aria-label="Settings"
        >
          <Settings class="w-5 h-5" />
        </button>
      </div>

      {/* ── Body ────────────────────────────────────────────── */}
      <div class="flex-1 overflow-y-auto px-20 pt-10">
        {/* Welcome Section */}
        <div class="mb-8">
          <h1 class="text-2xl font-bold text-white">{getGreeting()}</h1>
          <p class="mt-1.5 text-sm text-[#71717A]">
            Pick up where you left off, or start something new
          </p>
        </div>

        {/* Quick Actions */}
        <div class="mb-10">
          <QuickActions
            onNewSession={handleNewSession}
            onOpenProject={handleOpenProject}
            onResumeLast={handleResumeLast}
            hasLastSession={currentProject() !== null}
          />
        </div>

        {/* Recent Projects */}
        <Show when={orderedProjects().length > 0}>
          <div>
            <h2
              class="text-[10px] font-semibold text-[#3F3F46] uppercase mb-4"
              style={{ 'letter-spacing': '0.8px' }}
            >
              Recent Projects
            </h2>

            <div class="flex flex-wrap gap-4">
              <For each={orderedProjects()}>
                {(project) => (
                  <ProjectCard
                    project={project}
                    isActive={currentProject()?.id === project.id}
                    onClick={() => handleProjectClick(project.id as ProjectId)}
                  />
                )}
              </For>
            </div>
          </div>
        </Show>
      </div>
    </div>
  )
}
