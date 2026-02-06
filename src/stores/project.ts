/**
 * Project Store
 * Global state management for projects/workspaces
 */

import { createMemo, createSignal } from 'solid-js'
import { STORAGE_KEYS } from '../config/constants'
import { logError } from '../services/logger'
import {
  deleteProject as dbDeleteProject,
  updateProject as dbUpdateProject,
  getOrCreateProject,
  getProject,
  getProjectsWithStats,
} from '../services/project-database'
import { detectProject, getCurrentBranch } from '../services/project-detector'
import type { Project, ProjectId, ProjectWithStats } from '../types'

// ============================================================================
// Project State
// ============================================================================

// Current active project
const [currentProject, setCurrentProject] = createSignal<Project | null>(null)

// All projects (for sidebar/picker)
const [projects, setProjects] = createSignal<ProjectWithStats[]>([])
const [isLoadingProjects, setIsLoadingProjects] = createSignal(false)

// ============================================================================
// Computed Values
// ============================================================================

// Favorite projects
const favoriteProjects = createMemo(() => projects().filter((p) => p.isFavorite))

// Recent projects (non-favorites, sorted by last opened)
const recentProjects = createMemo(() =>
  projects()
    .filter((p) => !p.isFavorite && p.id !== 'default-project')
    .slice(0, 10)
)

// Check if we have any real projects (not just default)
const hasProjects = createMemo(() => projects().some((p) => p.id !== 'default-project'))

// ============================================================================
// Project Store Hook
// ============================================================================

export function useProject() {
  return {
    // ========================================================================
    // State Accessors
    // ========================================================================
    currentProject,
    projects,
    isLoadingProjects,
    favoriteProjects,
    recentProjects,
    hasProjects,

    // ========================================================================
    // Project Loading
    // ========================================================================

    /**
     * Load all projects from database
     */
    loadAllProjects: async () => {
      setIsLoadingProjects(true)
      try {
        const dbProjects = await getProjectsWithStats()
        setProjects(dbProjects)
      } catch (err) {
        logError('Project', 'Failed to load projects', err)
        setProjects([])
      } finally {
        setIsLoadingProjects(false)
      }
    },

    /**
     * Initialize project state on app start
     * Tries to restore last project or use default
     */
    initializeProjects: async () => {
      setIsLoadingProjects(true)
      try {
        // Load all projects
        const dbProjects = await getProjectsWithStats()
        setProjects(dbProjects)

        // Try to restore last project
        const lastProjectId = localStorage.getItem(STORAGE_KEYS.LAST_PROJECT)
        if (lastProjectId) {
          const project = dbProjects.find((p) => p.id === lastProjectId)
          if (project) {
            // Refresh git info
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
            return
          }
        }

        // Fall back to most recently opened project (not default)
        const recentProject = dbProjects.find((p) => p.id !== 'default-project')
        if (recentProject) {
          setCurrentProject(recentProject)
          localStorage.setItem(STORAGE_KEYS.LAST_PROJECT, recentProject.id)
        }
      } catch (err) {
        logError('Project', 'Failed to initialize projects', err)
        setProjects([])
      } finally {
        setIsLoadingProjects(false)
      }
    },

    // ========================================================================
    // Project Actions
    // ========================================================================

    /**
     * Open a directory as a project (detect git root, create if needed)
     */
    openDirectory: async (directory: string): Promise<Project> => {
      const detected = await detectProject(directory)
      const project = await getOrCreateProject(detected.rootDirectory, detected.suggestedName)

      // Update git info if available
      if (detected.isGitRepo) {
        await dbUpdateProject(project.id as ProjectId, {
          git: {
            branch: detected.branch,
            rootCommit: detected.rootCommit,
          },
        })
      }

      // Update local state
      const updatedProject: Project = {
        ...project,
        git: detected.isGitRepo
          ? { branch: detected.branch, rootCommit: detected.rootCommit }
          : undefined,
      }
      setCurrentProject(updatedProject)

      // Persist last project
      localStorage.setItem(STORAGE_KEYS.LAST_PROJECT, project.id)

      // Refresh project list
      const dbProjects = await getProjectsWithStats()
      setProjects(dbProjects)

      return updatedProject
    },

    /**
     * Switch to a different project
     */
    switchProject: async (projectId: ProjectId): Promise<void> => {
      const project = projects().find((p) => p.id === projectId)
      if (!project) {
        console.warn(`Project ${projectId} not found`)
        return
      }

      // Update last opened
      await dbUpdateProject(projectId, { lastOpenedAt: Date.now() })

      // Refresh git info if it's a git repo
      let gitInfo = project.git
      if (project.directory !== '~') {
        const branch = await getCurrentBranch(project.directory)
        if (branch) {
          gitInfo = { ...gitInfo, branch }
          await dbUpdateProject(projectId, { git: gitInfo })
        }
      }

      setCurrentProject({
        ...project,
        lastOpenedAt: Date.now(),
        git: gitInfo,
      })

      localStorage.setItem(STORAGE_KEYS.LAST_PROJECT, projectId)

      // Refresh list to update ordering
      const dbProjects = await getProjectsWithStats()
      setProjects(dbProjects)
    },

    /**
     * Update project name
     */
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

    /**
     * Toggle project favorite status
     */
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

    /**
     * Remove project from list (soft delete - keeps sessions in default project)
     */
    removeProject: async (id: ProjectId): Promise<void> => {
      if (id === 'default-project') {
        console.warn('Cannot remove default project')
        return
      }

      await dbDeleteProject(id)
      setProjects((prev) => prev.filter((p) => p.id !== id))

      // If this was the current project, switch to another
      if (currentProject()?.id === id) {
        const remaining = projects().filter((p) => p.id !== id && p.id !== 'default-project')
        if (remaining.length > 0) {
          setCurrentProject(remaining[0])
          localStorage.setItem(STORAGE_KEYS.LAST_PROJECT, remaining[0].id)
        } else {
          // Fall back to default project
          const defaultProject = await getProject('default-project' as ProjectId)
          setCurrentProject(defaultProject)
          if (defaultProject) {
            localStorage.setItem(STORAGE_KEYS.LAST_PROJECT, defaultProject.id)
          }
        }
      }
    },

    /**
     * Refresh git branch info for current project
     */
    refreshGitInfo: async (): Promise<void> => {
      const project = currentProject()
      if (!project || project.directory === '~') return

      const detected = await detectProject(project.directory)
      if (detected.isGitRepo) {
        await dbUpdateProject(project.id as ProjectId, {
          git: { branch: detected.branch, rootCommit: detected.rootCommit },
        })

        setCurrentProject((prev) =>
          prev
            ? { ...prev, git: { branch: detected.branch, rootCommit: detected.rootCommit } }
            : null
        )

        // Update in projects list too
        setProjects((prev) =>
          prev.map((p) =>
            p.id === project.id
              ? { ...p, git: { branch: detected.branch, rootCommit: detected.rootCommit } }
              : p
          )
        )
      }
    },

    // ========================================================================
    // Utility Methods
    // ========================================================================

    /**
     * Get the last project ID from localStorage
     */
    getLastProjectId: (): ProjectId | null => {
      return localStorage.getItem(STORAGE_KEYS.LAST_PROJECT) as ProjectId | null
    },

    /**
     * Clear current project selection
     */
    clearCurrentProject: () => {
      setCurrentProject(null)
      localStorage.removeItem(STORAGE_KEYS.LAST_PROJECT)
    },
  }
}
