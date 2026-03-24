/**
 * Project Helpers
 * Shared logic for detecting, creating, and activating projects.
 */

import { invoke } from '@tauri-apps/api/core'
import { STORAGE_KEYS } from '../config/constants'
import { updateProject as dbUpdateProject, getOrCreateProject } from '../services/project-database'
import { detectProject } from '../services/project-detector'
import type { Project, ProjectId } from '../types'

/**
 * Detect git info for a directory, get-or-create the project record,
 * and update git metadata if applicable. Returns a fully resolved Project.
 */
export async function resolveProjectFromDirectory(directory: string): Promise<Project> {
  const detected = await detectProject(directory)
  const project = await getOrCreateProject(detected.rootDirectory, detected.suggestedName)

  if (detected.isGitRepo) {
    await dbUpdateProject(project.id as ProjectId, {
      git: { branch: detected.branch, rootCommit: detected.rootCommit },
    })
  }

  return {
    ...project,
    git: detected.isGitRepo
      ? { branch: detected.branch, rootCommit: detected.rootCommit }
      : undefined,
  }
}

/**
 * Expand the Tauri FS scope for the project directory, change the
 * Rust process working directory, and persist the project ID as
 * the last-active project in localStorage.
 */
export function activateProject(project: Project): void {
  invoke('allow_project_path', { path: project.directory }).catch(() => {})
  invoke('set_cwd', { path: project.directory }).catch(() => {})
  localStorage.setItem(STORAGE_KEYS.LAST_PROJECT, project.id)
}
