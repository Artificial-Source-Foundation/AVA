/**
 * Workflows Service
 *
 * CRUD operations for workflow/recipe templates.
 * Workflows are reusable prompts extracted from completed sessions.
 */

import type { Workflow } from '../types'
import { initDatabase } from './database'

/** List workflows, optionally filtered by project */
export async function getWorkflows(projectId?: string): Promise<Workflow[]> {
  const db = await initDatabase()
  let query = 'SELECT * FROM workflows'
  const params: unknown[] = []

  if (projectId) {
    query += ' WHERE project_id = ? OR project_id IS NULL'
    params.push(projectId)
  }

  query += ' ORDER BY usage_count DESC, updated_at DESC'

  const rows = await db.select<Array<Record<string, unknown>>>(query, params)
  return rows.map(mapRow)
}

/** Get a single workflow by ID */
export async function getWorkflow(id: string): Promise<Workflow | null> {
  const db = await initDatabase()
  const rows = await db.select<Array<Record<string, unknown>>>(
    'SELECT * FROM workflows WHERE id = ?',
    [id]
  )
  return rows.length > 0 ? mapRow(rows[0]) : null
}

/** Save a new workflow */
export async function saveWorkflow(
  w: Omit<Workflow, 'id' | 'createdAt' | 'updatedAt' | 'usageCount'>
): Promise<Workflow> {
  const db = await initDatabase()
  const id = crypto.randomUUID()
  const now = Date.now()

  await db.execute(
    `INSERT INTO workflows (id, project_id, name, description, tags, prompt, created_at, updated_at, usage_count, source_session_id)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0, ?)`,
    [
      id,
      w.projectId || null,
      w.name,
      w.description,
      JSON.stringify(w.tags),
      w.prompt,
      now,
      now,
      w.sourceSessionId || null,
    ]
  )

  return { ...w, id, createdAt: now, updatedAt: now, usageCount: 0 }
}

/** Delete a workflow */
export async function deleteWorkflow(id: string): Promise<void> {
  const db = await initDatabase()
  await db.execute('DELETE FROM workflows WHERE id = ?', [id])
}

/** Increment usage count when a workflow is applied */
export async function incrementUsageCount(id: string): Promise<void> {
  const db = await initDatabase()
  await db.execute(
    'UPDATE workflows SET usage_count = usage_count + 1, updated_at = ? WHERE id = ?',
    [Date.now(), id]
  )
}

/** Extract a workflow from a session's user messages */
export async function createWorkflowFromSession(
  sessionId: string,
  name: string,
  description: string,
  tags: string[],
  projectId?: string
): Promise<Workflow> {
  const db = await initDatabase()
  const rows = await db.select<Array<{ content: string }>>(
    "SELECT content FROM messages WHERE session_id = ? AND role = 'user' ORDER BY created_at ASC",
    [sessionId]
  )

  const prompt = rows.map((r) => r.content).join('\n\n---\n\n')

  return saveWorkflow({
    projectId,
    name,
    description,
    tags,
    prompt,
    sourceSessionId: sessionId,
  })
}

function mapRow(row: Record<string, unknown>): Workflow {
  let tags: string[] = []
  if (row.tags) {
    try {
      tags = JSON.parse(row.tags as string)
    } catch {
      tags = []
    }
  }

  return {
    id: row.id as string,
    projectId: (row.project_id as string) || undefined,
    name: row.name as string,
    description: (row.description as string) || '',
    tags,
    prompt: row.prompt as string,
    createdAt: row.created_at as number,
    updatedAt: row.updated_at as number,
    usageCount: (row.usage_count as number) || 0,
    sourceSessionId: (row.source_session_id as string) || undefined,
  }
}
