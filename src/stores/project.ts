/**
 * Project Store
 * Global state management for projects/workspaces
 */

import { invoke } from '@tauri-apps/api/core'
import { createMemo, createSignal } from 'solid-js'
import { STORAGE_KEYS } from '../config/constants'
import { logError, logWarn } from '../services/logger'
import {
  deleteProject as dbDeleteProject,
  updateProject as dbUpdateProject,
  getProject,
  getProjectsWithStats,
} from '../services/project-database'
import { detectProject, getCurrentBranch } from '../services/project-detector'
import type { Project, ProjectId, ProjectWithStats } from '../types'
import { activateProject, resolveProjectFromDirectory } from './project-helpers'

// ============================================================================
// State
// ============================================================================

const [currentProject, setCurrentProject] = createSignal<Project | null>(null)
const [projects, setProjects] = createSignal<ProjectWithStats[]>([])
const [isLoadingProjects, setIsLoadingProjects] = createSignal(false)

// ============================================================================
// Computed
// ============================================================================

const favoriteProjects = createMemo(() => projects().filter((p) => p.isFavorite))

const recentProjects = createMemo(() =>
  projects()
    .filter((p) => !p.isFavorite && p.id !== 'default-project')
    .slice(0, 10)
)

const hasProjects = createMemo(() => projects().some((p) => p.id !== 'default-project'))

/** Reload the projects list from the database into the signal. */
async function refreshProjectsList(): Promise<void> {
  const dbProjects = await getProjectsWithStats()
  setProjects(dbProjects)
}

// ============================================================================
// Hook
// ============================================================================

/**
 * Set a lightweight virtual project for web (non-Tauri) mode.
 * Bypasses the database and Tauri FS scope — just sets the signal
 * so the AppShell has a working directory context.
 */
export function setWebProject(directory: string): void {
  const name = directory.split('/').pop() || 'Web Project'
  setCurrentProject({
    id: 'web-project' as ProjectId,
    name,
    directory,
    createdAt: Date.now(),
    updatedAt: Date.now(),
    lastOpenedAt: Date.now(),
  })
}

export function useProject() {
  return {
    // State
    currentProject,
    projects,
    isLoadingProjects,
    favoriteProjects,
    recentProjects,
    hasProjects,

    loadAllProjects: async (): Promise<void> => {
      setIsLoadingProjects(true)
      try {
        await refreshProjectsList()
      } catch (err) {
        logError('Project', 'Failed to load projects', err)
        setProjects([])
      } finally {
        setIsLoadingProjects(false)
      }
    },

    initializeProjects: async (): Promise<void> => {
      setIsLoadingProjects(true)
      try {
        const dbProjects = await getProjectsWithStats()
        setProjects(dbProjects)

        // Try to restore last project
        const lastProjectId = localStorage.getItem(STORAGE_KEYS.LAST_PROJECT)
        if (lastProjectId) {
          const project = dbProjects.find((p) => p.id === lastProjectId)
          if (project) {
            const detected = await detectProject(project.directory)
            if (detected.isGitRepo) {
              await dbUpdateProject(project.id as ProjectId, {
                git: { branch: detected.branch, rootCommit: detected.rootCommit },
              })
            }
            setCurrentProject({
              ...project,
              git: detected.isGitRepo
                ? { branch: detected.branch, rootCommit: detected.rootCommit }
                : undefined,
            })
            invoke('allow_project_path', { path: project.directory }).catch(() => {})
            return
          }
        }

        // Fall back to most recently opened project (not default)
        const recentProject = dbProjects.find((p) => p.id !== 'default-project')
        if (recentProject) {
          setCurrentProject(recentProject)
          activateProject(recentProject)
        }
      } catch (err) {
        logError('Project', 'Failed to initialize projects', err)
        setProjects([])
      } finally {
        setIsLoadingProjects(false)
      }
    },

    openDirectory: async (directory: string): Promise<Project> => {
      const updatedProject = await resolveProjectFromDirectory(directory)
      setCurrentProject(updatedProject)
      activateProject(updatedProject)
      await refreshProjectsList()
      return updatedProject
    },

    switchProject: async (projectId: ProjectId): Promise<void> => {
      const project = projects().find((p) => p.id === projectId)
      if (!project) {
        logWarn('project', 'Project not found', { projectId })
        return
      }

      await dbUpdateProject(projectId, { lastOpenedAt: Date.now() })

      let gitInfo = project.git
      if (project.directory !== '~') {
        const branch = await getCurrentBranch(project.directory)
        if (branch) {
          gitInfo = { ...gitInfo, branch }
          await dbUpdateProject(projectId, { git: gitInfo })
        }
      }

      const updated: Project = { ...project, lastOpenedAt: Date.now(), git: gitInfo }
      setCurrentProject(updated)
      activateProject(updated)
      await refreshProjectsList()
    },

    renameProject: async (id: ProjectId, newName: string): Promise<void> => {
      const trimmedName = newName.trim()
      if (!trimmedName) return

      await dbUpdateProject(id, { name: trimmedName })
      setProjects((prev) =>
        prev.map((p) => (p.id === id ? { ...p, name: trimmedName, updatedAt: Date.now() } : p))
      )
      if (currentProject()?.id === id) {
        setCurrentProject((prev) => (prev ? { ...prev, name: trimmedName } : null))
      }
    },

    toggleFavorite: async (id: ProjectId): Promise<void> => {
      const project = projects().find((p) => p.id === id)
      if (!project) return

      const isFavorite = !project.isFavorite
      await dbUpdateProject(id, { isFavorite })
      setProjects((prev) => prev.map((p) => (p.id === id ? { ...p, isFavorite } : p)))
      if (currentProject()?.id === id) {
        setCurrentProject((prev) => (prev ? { ...prev, isFavorite } : null))
      }
    },

    removeProject: async (id: ProjectId): Promise<void> => {
      if (id === 'default-project') {
        logWarn('project', 'Cannot remove default project')
        return
      }

      await dbDeleteProject(id)
      setProjects((prev) => prev.filter((p) => p.id !== id))

      if (currentProject()?.id === id) {
        const remaining = projects().filter((p) => p.id !== id && p.id !== 'default-project')
        if (remaining.length > 0) {
          setCurrentProject(remaining[0])
          localStorage.setItem(STORAGE_KEYS.LAST_PROJECT, remaining[0].id)
        } else {
          const defaultProject = await getProject('default-project' as ProjectId)
          setCurrentProject(defaultProject)
          if (defaultProject) {
            localStorage.setItem(STORAGE_KEYS.LAST_PROJECT, defaultProject.id)
          }
        }
      }
    },

    refreshGitInfo: async (): Promise<void> => {
      const project = currentProject()
      if (!project || project.directory === '~') return

      const detected = await detectProject(project.directory)
      if (detected.isGitRepo) {
        const git = { branch: detected.branch, rootCommit: detected.rootCommit }
        await dbUpdateProject(project.id as ProjectId, { git })
        setCurrentProject((prev) => (prev ? { ...prev, git } : null))
        setProjects((prev) => prev.map((p) => (p.id === project.id ? { ...p, git } : p)))
      }
    },

    getLastProjectId: (): ProjectId | null => {
      return localStorage.getItem(STORAGE_KEYS.LAST_PROJECT) as ProjectId | null
    },

    setCurrentDirectory: async (path: string): Promise<void> => {
      const updatedProject = await resolveProjectFromDirectory(path)
      setCurrentProject(updatedProject)
      activateProject(updatedProject)
      await refreshProjectsList()
    },

    clearCurrentProject: (): void => {
      setCurrentProject(null)
      localStorage.removeItem(STORAGE_KEYS.LAST_PROJECT)
    },
  }
}
