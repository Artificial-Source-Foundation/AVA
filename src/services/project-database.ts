/**
 * Project Database Service
 * SQLite operations for projects
 */

import type {
  CreateProjectInput,
  Project,
  ProjectId,
  ProjectWithStats,
  UpdateProjectInput,
} from '../types'
import { initDatabase } from './database'

// ============================================================================
// Project Operations
// ============================================================================

/**
 * Create a new project
 */
export async function createProject(input: CreateProjectInput): Promise<Project> {
  const database = await initDatabase()
  const id = crypto.randomUUID() as ProjectId
  const now = Date.now()

  // Extract directory name as default name
  const name = input.name || input.directory.split('/').pop() || 'Project'

  await database.execute(
    `INSERT INTO projects (id, name, directory, icon, created_at, updated_at, last_opened_at)
     VALUES (?, ?, ?, ?, ?, ?, ?)`,
    [id, name, input.directory, input.icon ? JSON.stringify(input.icon) : null, now, now, now]
  )

  return {
    id,
    name,
    directory: input.directory,
    icon: input.icon,
    createdAt: now,
    updatedAt: now,
    lastOpenedAt: now,
  }
}

/**
 * Get project by ID
 */
export async function getProject(id: ProjectId): Promise<Project | null> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    'SELECT * FROM projects WHERE id = ?',
    [id]
  )

  if (rows.length === 0) return null
  return mapDbProject(rows[0])
}

/**
 * Get project by directory path
 */
export async function getProjectByDirectory(directory: string): Promise<Project | null> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    'SELECT * FROM projects WHERE directory = ?',
    [directory]
  )

  if (rows.length === 0) return null
  return mapDbProject(rows[0])
}

/**
 * Get or create project for a directory
 */
export async function getOrCreateProject(directory: string, name?: string): Promise<Project> {
  const existing = await getProjectByDirectory(directory)
  if (existing) {
    // Update last opened
    await updateProject(existing.id, { lastOpenedAt: Date.now() })
    return { ...existing, lastOpenedAt: Date.now() }
  }
  return createProject({ directory, name })
}

/**
 * Get all projects with computed stats
 */
export async function getProjectsWithStats(): Promise<ProjectWithStats[]> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(`
    SELECT
      p.*,
      COUNT(DISTINCT s.id) as session_count,
      COALESCE(
        (SELECT COUNT(*) FROM messages m
         JOIN sessions s2 ON m.session_id = s2.id
         WHERE s2.project_id = p.id), 0
      ) as total_messages
    FROM projects p
    LEFT JOIN sessions s ON s.project_id = p.id AND s.status != 'archived'
    GROUP BY p.id
    ORDER BY
      p.is_favorite DESC,
      p.last_opened_at DESC NULLS LAST,
      p.updated_at DESC
  `)

  return rows.map((row) => ({
    ...mapDbProject(row),
    sessionCount: (row.session_count as number) || 0,
    totalMessages: (row.total_messages as number) || 0,
  }))
}

/**
 * Update project
 */
export async function updateProject(id: ProjectId, updates: UpdateProjectInput): Promise<void> {
  const database = await initDatabase()
  const setClauses: string[] = ['updated_at = ?']
  const values: unknown[] = [Date.now()]

  if (updates.name !== undefined) {
    setClauses.push('name = ?')
    values.push(updates.name)
  }
  if (updates.icon !== undefined) {
    setClauses.push('icon = ?')
    values.push(updates.icon ? JSON.stringify(updates.icon) : null)
  }
  if (updates.git !== undefined) {
    setClauses.push('git_branch = ?', 'git_root_commit = ?')
    values.push(updates.git.branch || null, updates.git.rootCommit || null)
  }
  if (updates.isFavorite !== undefined) {
    setClauses.push('is_favorite = ?')
    values.push(updates.isFavorite ? 1 : 0)
  }
  if (updates.lastOpenedAt !== undefined) {
    setClauses.push('last_opened_at = ?')
    values.push(updates.lastOpenedAt)
  }

  values.push(id)
  await database.execute(`UPDATE projects SET ${setClauses.join(', ')} WHERE id = ?`, values)
}

/**
 * Delete project
 * Sessions are moved to default project (via FK ON DELETE SET NULL behavior
 * is overridden here to use default project instead)
 */
export async function deleteProject(id: ProjectId): Promise<void> {
  const database = await initDatabase()

  // Don't allow deleting the default project
  if (id === 'default-project') {
    throw new Error('Cannot delete the default project')
  }

  // Move sessions to default project
  await database.execute(
    "UPDATE sessions SET project_id = 'default-project' WHERE project_id = ?",
    [id]
  )

  // Delete the project
  await database.execute('DELETE FROM projects WHERE id = ?', [id])
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Map database row to Project object
 */
function mapDbProject(row: Record<string, unknown>): Project {
  return {
    id: row.id as ProjectId,
    name: row.name as string,
    directory: row.directory as string,
    icon: row.icon ? JSON.parse(row.icon as string) : undefined,
    git: row.git_branch
      ? {
          branch: row.git_branch as string,
          rootCommit: (row.git_root_commit as string) || undefined,
        }
      : undefined,
    createdAt: row.created_at as number,
    updatedAt: row.updated_at as number,
    lastOpenedAt: (row.last_opened_at as number) || undefined,
    isFavorite: Boolean(row.is_favorite),
  }
}
