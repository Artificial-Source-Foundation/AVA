/**
 * Database Agent Operations
 *
 * CRUD for agent records within sessions.
 */

import type { Agent } from '../types'
import { initDatabase } from './db-init'

/**
 * Save a new agent
 */
export async function saveAgent(agent: Agent): Promise<void> {
  const database = await initDatabase()
  await database.execute(
    `INSERT INTO agents (id, session_id, type, status, model, created_at, completed_at, assigned_files, task_description, result)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
    [
      agent.id,
      agent.sessionId,
      agent.type,
      agent.status,
      agent.model,
      agent.createdAt,
      agent.completedAt || null,
      agent.assignedFiles ? JSON.stringify(agent.assignedFiles) : null,
      agent.taskDescription || null,
      agent.result ? JSON.stringify(agent.result) : null,
    ]
  )
}

/**
 * Get all agents for a session
 */
export async function getAgents(sessionId: string): Promise<Agent[]> {
  const database = await initDatabase()
  const rows = await database.select<Array<Record<string, unknown>>>(
    'SELECT * FROM agents WHERE session_id = ? ORDER BY created_at ASC',
    [sessionId]
  )
  return rows.map((row) => ({
    id: row.id as string,
    sessionId: row.session_id as string,
    type: row.type as Agent['type'],
    status: row.status as Agent['status'],
    model: row.model as string,
    createdAt: row.created_at as number,
    completedAt: (row.completed_at as number | null) ?? undefined,
    assignedFiles: row.assigned_files
      ? (JSON.parse(row.assigned_files as string) as string[])
      : undefined,
    taskDescription: (row.task_description as string | null) ?? undefined,
    result: row.result ? (JSON.parse(row.result as string) as Agent['result']) : undefined,
  }))
}

/**
 * Update an agent's properties
 */
export async function updateAgentInDb(
  id: string,
  updates: Partial<Pick<Agent, 'status' | 'completedAt' | 'result' | 'taskDescription'>>
): Promise<void> {
  const database = await initDatabase()
  const setClauses: string[] = []
  const values: unknown[] = []

  if (updates.status !== undefined) {
    setClauses.push('status = ?')
    values.push(updates.status)
  }
  if (updates.completedAt !== undefined) {
    setClauses.push('completed_at = ?')
    values.push(updates.completedAt)
  }
  if (updates.result !== undefined) {
    setClauses.push('result = ?')
    values.push(JSON.stringify(updates.result))
  }
  if (updates.taskDescription !== undefined) {
    setClauses.push('task_description = ?')
    values.push(updates.taskDescription)
  }

  if (setClauses.length === 0) return

  values.push(id)
  await database.execute(`UPDATE agents SET ${setClauses.join(', ')} WHERE id = ?`, values)
}
